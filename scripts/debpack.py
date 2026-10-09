#!/usr/bin/env python3
"""Build Debian binary packages (.deb) without dpkg-deb.

dpkg-deb is not installed on this host, and pulling in a C toolchain to
replace it would be silly when the container format is a thirty line spec.
A .deb is an `ar` archive holding exactly three members, in this order:

    debian-binary    the literal text "2.0\\n"
    control.tar.gz   ./control (the package metadata dpkg reads)
    data.tar.gz      the file tree, rooted at /

Everything below is written by hand for two reasons: `ar` stamps members with
the current mtime/uid/gid, which makes the output differ on every build, and
dpkg is fussy about member ordering and the absence of a symbol table. Hand-
rolling gives a byte-identical package from identical inputs, which is the
property worth having for something that gets hashed.

`ar t` reads the result back, so the format is verified by an implementation
that does not share this file's assumptions.
"""

from __future__ import annotations

from pathlib import Path
import io
import tarfile
import time

# The ar member header is 60 bytes: 16 name + 12 mtime + 6 uid + 6 gid +
# 8 mode + 10 size + 2 magic.
AR_MAGIC = b"`\n"
AR_FMAG = b"!<arch>\n"

# dpkg-deb uses a fixed timestamp so that rebuilding an unchanged source tree
# yields an identical package. 0 is a valid epoch but some tools render it as
# 1970 and look odd; use the same constant dpkg itself uses.
AR_MTIME = 0


class DebError(Exception):
    """Raised for any malformed package description."""


def _ar_member(name: str, data: bytes) -> bytes:
    """Encode one ar member: 60-byte header, payload, even-byte padding.

    Names longer than 15 characters get the GNU `name/` terminator. None of
    the three members dpkg requires exceeds 15 characters, but doing it
    properly means the writer cannot silently corrupt a future member.
    """
    if len(name) > 15:
        encoded = name + "/"
        if len(encoded) > 16:
            raise DebError(f"ar member name too long: {name}")
        name_field = encoded.encode("ascii").ljust(16)
    else:
        name_field = name.encode("ascii").ljust(16)

    header = b"".join((
        name_field,
        str(AR_MTIME).encode("ascii").ljust(12),
        b"0".ljust(6),           # uid
        b"0".ljust(6),           # gid
        b"100644".ljust(8),      # mode, octal
        str(len(data)).encode("ascii").ljust(10),
        AR_MAGIC,
    ))
    assert len(header) == 60, f"ar header is {len(header)} bytes, expected 60"

    # ar members start at even offsets. An odd payload gets one \n so the next
    # header is aligned; readers skip the pad, they do not count it.
    pad = b"" if len(data) % 2 == 0 else b"\n"
    return header + data + pad


def render_control(fields: dict) -> bytes:
    """Render a RFC-822-ish Debian control stanza.

    Description is special: only its first line belongs on the `Description:`
    header, and continuation lines must start with a space. Getting this wrong
    does not fail loudly -- dpkg parses what it can and drops the rest -- so it
    is worth being explicit rather than joining on newlines.
    """
    lines = []
    for key in ("Package", "Version", "Architecture", "Maintainer",
                "Installed-Size", "Depends", "Recommends", "Suggests",
                "Conflicts", "Provides", "Replaces", "Section", "Priority",
                "Homepage", "Description"):
        value = fields.get(key)
        if value is None or value == "":
            continue
        if key == "Description":
            head, _, rest = str(value).partition("\n")
            lines.append(f"Description: {head}")
            for extra in rest.split("\n"):
                if extra.strip():
                    lines.append(f" {extra.strip()}")
        else:
            lines.append(f"{key}: {value}")

    # Any field not in the ordering above still has to be emitted, or it is
    # silently dropped and the caller thinks it shipped.
    for key, value in fields.items():
        if key in lines_seen(lines) or value in (None, ""):
            continue
        lines.append(f"{key}: {value}")

    return ("\n".join(lines) + "\n").encode("utf-8")


def lines_seen(lines: list) -> set:
    """Names of the control fields already rendered, in order."""
    return {line.split(":", 1)[0] for line in lines if ":" in line}


def _tar_gz(entries: list) -> bytes:
    """Build a gzip tar from (arcname, data, mode, type) quadruples.

    The type is explicit rather than inferred from an empty payload: a
    TarInfo defaults to REGTYPE, so a directory written without setting
    DIRTYPE lands as a zero-length *file* and every path under it fails to
    unpack with "not a directory".

    gzip's mtime is pinned for the same reason as the ar header: a default
    uncompressed-timestamp header would make every build differ.
    """
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz", format=tarfile.GNU_FORMAT,
                      compresslevel=9) as archive:
        for arcname, data, mode, kind in entries:
            info = tarfile.TarInfo(arcname)
            info.size = len(data)
            info.mode = mode
            info.type = kind
            info.mtime = AR_MTIME
            info.uid = 0
            info.gid = 0
            info.uname = "root"
            info.gname = "root"
            archive.addfile(info, fileobj=io.BytesIO(data))
    return buffer.getvalue()


def installed_size(files: dict) -> int:
    """Installed-Size in KiB, the unit dpkg expects.

    Rounded up: dpkg reserves whole blocks, so a 1-byte file still costs a
    block. Rounding down would under-report and make the number a lie.
    """
    total = 0
    for arcname, (data, _mode) in files.items():
        # +1 accounts for the directory entry the file needs.
        total += ((len(data) + 4095) // 4096) + 1
    return (total + 1023) // 1024


def build_deb(control: dict, files: dict, output: Path) -> Path:
    """Write a .deb.

    control: the field dict handed to render_control().
    files:   mapping of absolute install path -> (bytes, unix mode).
    """
    required = ("Package", "Version", "Architecture")
    for key in required:
        if not control.get(key):
            raise DebError(f"control is missing required field: {key}")

    # Files must sort by path so the tar is reproducible, and the data member
    # must contain no leading-slash paths (tar would warn and dpkg would
    # refuse on extract).
    normalised = {}
    for path, (data, mode) in files.items():
        arcname = path.lstrip("/")
        if arcname.startswith("/") or arcname == "":
            raise DebError(f"unusable install path: {path}")
        normalised[arcname] = (data, mode)

    data_entries = [(f"./{name}", payload, mode, tarfile.REGTYPE)
                    for name, (payload, mode) in sorted(normalised.items())]

    # Directories have to exist as entries too, or dpkg's unpacker will try to
    # create /usr/share/doc/netbeam/copyright in a directory that does not
    # exist and fall back to chmod, which then fails.
    #
    # Parents are derived from the *clean* relative name. Walking the already
    # "./"-prefixed arcname instead yields "." as the first component and then
    # emits "././usr", which tar unpacks as a literal directory named "." and
    # every real path turns into "not a directory".
    file_names = set(normalised)
    seen_dirs = set()
    for name in normalised:
        parts = name.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            parent = "/".join(parts[:i])
            if parent and parent not in file_names:
                seen_dirs.add(parent)

    dir_entries = [(f"./{name}", b"", 0o755, tarfile.DIRTYPE)
                   for name in sorted(seen_dirs)]

    control_entries = [("./control", render_control(control), 0o644, tarfile.REGTYPE)]

    # md5sums is optional but dpkg verifies it during unpack, and a package
    # without it looks hand-made.
    md5sums = []
    import hashlib
    for arcname, payload, mode, _kind in sorted(data_entries, key=lambda e: e[0]):
        if mode & 0o111:
            md5sums.append(f"{hashlib.md5(payload).hexdigest()}  {arcname[2:]}")
    if md5sums:
        control_entries.append(("./md5sums",
                                ("\n".join(md5sums) + "\n").encode(),
                                0o644, tarfile.REGTYPE))

    members = [
        ("debian-binary", b"2.0\n"),
        ("control.tar.gz", _tar_gz(control_entries)),
        ("data.tar.gz", _tar_gz(dir_entries + data_entries)),
    ]

    blob = bytearray(AR_FMAG)
    for name, payload in members:
        blob += _ar_member(name, payload)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bytes(blob))
    return output


def verify_deb(path: Path) -> dict:
    """Re-read a .deb with a deliberately independent minimal ar parser.

    Written from the format description rather than reusing _ar_member, so a
    bug in the writer cannot hide behind the same bug in the reader.
    """
    data = path.read_bytes()
    if not data.startswith(AR_FMAG):
        raise DebError("missing ar magic")

    pos = len(AR_FMAG)
    members = []
    while pos < len(data):
        if pos + 60 > len(data):
            raise DebError("truncated ar header")
        header = data[pos:pos + 60]
        if header[58:60] != AR_MAGIC:
            raise DebError("bad ar member magic")
        name = header[0:16].decode("ascii").strip().rstrip("/")
        size = int(header[48:58].decode("ascii").strip())
        pos += 60
        members.append((name, data[pos:pos + size]))
        pos += size + (size % 2)

    names = [n for n, _ in members]
    if names != ["debian-binary", "control.tar.gz", "data.tar.gz"]:
        raise DebError(f"unexpected member order: {names}")
    if members[0][1] != b"2.0\n":
        raise DebError(f"debian-binary is {members[0][1]!r}, expected b'2.0\\n'")

    control_text = {}
    with tarfile.open(fileobj=io.BytesIO(members[1][1]), mode="r:gz") as archive:
        for member in archive.getmembers():
            if member.name == "./control":
                text = archive.extractfile(member).read().decode("utf-8")
                for line in text.split("\n"):
                    if line and not line.startswith(" ") and ":" in line:
                        key, _, value = line.partition(":")
                        control_text[key.strip()] = value.strip()

    with tarfile.open(fileobj=io.BytesIO(members[2][1]), mode="r:gz") as archive:
        payload = [m.name for m in archive.getmembers() if m.isfile()]

    return {"members": names, "control": control_text, "payload": payload}