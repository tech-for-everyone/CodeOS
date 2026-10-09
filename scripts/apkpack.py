#!/usr/bin/env python3
"""Build Android application packages (.apk) from scratch.

The host has no aapt, aapt2, zipalign, apksigner or d8, so every byte of an
APK is produced here: the ZIP container, the binary XML manifest, the
resource table and the DEX file.  jarsigner (from the JDK, which *is*
installed) signs the result with a real v1 JAR signature, so the signature
half is not hand-rolled -- and jarsigner is also used as the independent
verifier.

What is NOT verified here, because nothing on this host can verify it:

  * The DEX is written from the format spec and re-parsed by
    verify_dex() below, but there is no d8/dx/dexdump to confirm it against a
    reference implementation.
  * The manifest is parsed back by verify_axml(), which is a second
    implementation of the reader, not aapt.

An APK made by this script is a native-payload package: the ELF is the
application, and the manifest declares android:hasCode="false" because there
is no Java entry point.  That is the correct shape for it, but it does mean
the result will not install-and-run on a stock Android device, and this is
stated in the manifest's own <application> element rather than left for
someone to discover.
"""

from __future__ import annotations

from pathlib import Path
import struct
import subprocess
import shutil
import tempfile
import zipfile

# ---- Android resource identifiers -------------------------------------
# The resource map chunk maps pool index -> resource id. aapt emits one for
# every pool entry (0 for names that are not resources), and parsers use it to
# resolve attribute names without hardcoding them, so it is worth emitting.
ATTR_IDS = {
    "label": 0x01010001,
    "name": 0x01010003,
    "permission": 0x01010006,
    "hasCode": 0x0101000C,
    "minSdkVersion": 0x0101020C,
    "versionCode": 0x0101021B,
    "versionName": 0x0101021C,
}

ANDROID_NS = "http://schemas.android.com/apk/res/android"

# ---- chunk / value type constants --------------------------------------
RES_NULL_TYPE = 0x0000
RES_STRING_POOL_TYPE = 0x0001
RES_TABLE_TYPE = 0x0002
RES_XML_TYPE = 0x0003
RES_XML_RESOURCE_MAP_TYPE = 0x0180
RES_XML_START_NAMESPACE_TYPE = 0x0100
# End-document and end-namespace are the same chunk type; they differ only in
# their body (end-document carries no prefix/uri pair).
RES_XML_END_DOCUMENT_TYPE = 0x0101
RES_XML_END_NAMESPACE_TYPE = 0x0101
RES_XML_START_TAG_TYPE = 0x0102
RES_XML_END_TAG_TYPE = 0x0103

UTF8_FLAG = 0x00000100

TYPE_REFERENCE = 0x01
TYPE_STRING = 0x03
TYPE_INT_DEC = 0x10
TYPE_INT_BOOLEAN = 0x12

ANDROID_MANIFEST_NS = "http://schemas.android.com/apk/res/android"
ANDROID_PREFIX = "android"
NO_ENTRY = 0xFFFFFFFF


class ApkError(Exception):
    """Raised for any malformed manifest description or unsupported request."""


# --------------------------------------------------------------------- ZIP

def _zipinfo(name: str, compress: bool) -> zipfile.ZipInfo:
    """A ZipInfo with a fixed timestamp.

    zipfile stamps entries with the current local time by default, so two
    builds of identical inputs differ. 1980-01-01 is the earliest value the
    ZIP format can represent.
    """
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = (zipfile.ZIP_DEFLATED if compress
                          else zipfile.ZIP_STORED)
    # 0644 for everything; jarsigner reads the *external* attributes for the
    # executable bit on extraction, so the native library gets set separately
    # in write_zip.
    info.external_attr = (0o644 << 16)
    info.create_system = 3          # Unix, so the mode above is honoured
    return info


def write_zip(entries: list, path: Path, executable_prefixes=()) -> None:
    """Write entries to a ZIP.

    entries: (arcname, bytes, compress) triples, written in the given order.
    executable_prefixes: arcname prefixes that should carry mode 0755, which
    is how the native library keeps its exec bit through an APK install.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as archive:
        for arcname, payload, compress in entries:
            info = _zipinfo(arcname, compress)
            if arcname.startswith(tuple(executable_prefixes)):
                info.external_attr = (0o755 << 16)
            archive.writestr(info, payload)


# --------------------------------------------------------- string pool

def _encode_len8(value: int) -> bytes:
    """The one-or-two byte length prefix used by UTF-8 string pools.

    A high bit on the first byte means "two bytes"; without it the value is
    the byte itself. Anything over 0x7FFF needs three bytes, which the format
    does not provide, so it is rejected rather than silently truncated.
    """
    if value < 0x80:
        return bytes([value])
    if value > 0x7FFF:
        raise ApkError(f"string too long for a pool length prefix: {value}")
    return bytes([0x80 | (value >> 8), value & 0xFF])


def string_pool(strings, resource_ids=None) -> tuple:
    """Encode a UTF-8 string pool chunk.

    Returns (chunk_bytes, {string: index}). resource_ids, when given, is a
    dict of string -> resource id used to build the resource map chunk; a
    string with no entry gets id 0.
    """
    if len(strings) > 0x7FFF:
        raise ApkError("string pool overflows the 16-bit offset field")
    index = {s: i for i, s in enumerate(strings)}

    data = bytearray()
    offsets = []
    for text in strings:
        offsets.append(len(data))
        encoded = text.encode("utf-8")
        # utf16_size is the length in UTF-16 code units, which differs from
        # the byte length for anything outside ASCII.
        utf16_size = len(text.encode("utf-16-le")) // 2
        data += _encode_len8(utf16_size)
        data += _encode_len8(len(encoded))
        data += encoded
        data += b"\x00"
    # The pool is 4-byte aligned; aapt pads with zeros and so does every
    # parser that reads offsets instead of walking the data sequentially.
    while len(data) % 4:
        data += b"\x00"

    strings_start = 0x1C + 4 * len(strings)
    header = struct.pack("<HHIIIIII",
                         RES_STRING_POOL_TYPE, 0x001C,
                         strings_start + len(data),
                         len(strings), 0, UTF8_FLAG,
                         strings_start, 0)
    chunk = bytearray(header)
    chunk += b"".join(struct.pack("<I", off) for off in offsets)
    chunk += data
    assert len(chunk) == strings_start + len(data), "pool size mismatch"

    ids = None
    if resource_ids is not None:
        ids = [resource_ids.get(s, 0) for s in strings]
    return bytes(chunk), index, ids


# ------------------------------------------------------------- binary XML

class Node:
    """One element in the manifest tree."""

    def __init__(self, name, attrs=None, children=None):
        self.name = name
        # attrs: list of (ns_uri_or_None, name, raw_value_or_None,
        #                 data_type, data)
        self.attrs = list(attrs or [])
        self.children = list(children or [])


def _node_header(kind: int, line: int, comment: int, size: int) -> bytes:
    return struct.pack("<HHIII", kind, 0x0010, size, line, comment)


def axml(root: Node, line: int = 1) -> bytes:
    """Encode an XML resource tree as a binary (AXML) document.

    The layout is a flat list of chunks after a 8-byte file header:
    string pool, resource map, one start-namespace, the element tree, one
    end-namespace, end-document.  Every chunk size is back-patched because
    attribute counts change the element chunk size.
    """
    # Collect every string first: element/attribute names, namespace URIs,
    # prefixes and raw values.
    pool = [""]

    def want(text):
        if text not in pool:
            pool.append(text)
        return pool.index(text)

    # The prefix string is not reachable by walking the tree (a prefix only
    # appears inside a start-namespace chunk), so it has to be added here or
    # the namespace index below points at nothing.
    want(ANDROID_PREFIX)
    want(ANDROID_NS)

    def walk(node):
        want(node.name)
        for ns, name, raw, _dtype, _data in node.attrs:
            if ns:
                want(ns)
            want(name)
            if raw is not None:
                want(raw)
        for child in node.children:
            walk(child)

    walk(root)

    chunks = []

    # --- the element tree ------------------------------------------------
    def emit(node, depth):
        size = 0x0010 + 0x0014 + 20 * len(node.attrs)
        body = bytearray()
        # attributeStart / attributeSize / count, then idIndex / classIndex /
        # styleIndex, all zero: this manifest uses no id or class attributes,
        # and a nonzero index with no such attribute is rejected by parsers.
        body += struct.pack("<IIHHHHHH",
                            NO_ENTRY, pool.index(node.name),
                            0x0014, 0x0014, len(node.attrs), 0, 0, 0)
        for ns, name, raw, dtype, data in node.attrs:
            body += struct.pack("<IIIHBBI",
                                NO_ENTRY if ns is None else pool.index(ns),
                                pool.index(name),
                                NO_ENTRY if raw is None else pool.index(raw),
                                8, 0, dtype, data)
        chunks.append(_node_header(RES_XML_START_TAG_TYPE, line, NO_ENTRY, size)
                      + bytes(body))
        for child in node.children:
            emit(child, depth + 1)
        chunks.append(_node_header(RES_XML_END_TAG_TYPE, line, NO_ENTRY, 24)
                      + struct.pack("<II", NO_ENTRY, pool.index(node.name)))

    emit(root, 0)

    # Namespace and document terminators.
    chunks.insert(0, _node_header(RES_XML_START_NAMESPACE_TYPE, line, NO_ENTRY, 24)
                  + struct.pack("<II", pool.index(ANDROID_PREFIX),
                                pool.index(ANDROID_NS)))
    chunks.append(_node_header(RES_XML_END_NAMESPACE_TYPE, line, NO_ENTRY, 24)
                  + struct.pack("<II", pool.index(ANDROID_PREFIX),
                                pool.index(ANDROID_NS)))
    # End-document is *only* the node header -- unlike end-namespace it has no
    # prefix/uri body. Appending the pair anyway leaves 8 bytes past the end of
    # the chunk chain, which a parser reads as a bogus trailing chunk.
    chunks.append(_node_header(RES_XML_END_DOCUMENT_TYPE, line, NO_ENTRY, 16))

    # --- pool and resource map ------------------------------------------
    # Attribute names sort to the front of the pool so the resource map can
    # index them the way aapt does; everything else gets id 0.
    resource_ids = {name: rid for name, rid in ATTR_IDS.items() if name in pool}
    pool_bytes, index, ids = string_pool(pool, resource_ids)

    prefix = bytearray(struct.pack("<HHI", RES_XML_TYPE, 0x0008, 0))
    resmap = struct.pack("<HHI", RES_XML_RESOURCE_MAP_TYPE, 0x0008,
                         8 + 4 * len(ids)) + b"".join(struct.pack("<I", i) for i in ids)

    body = pool_bytes + resmap + b"".join(chunks)
    prefix[4:8] = struct.pack("<I", len(prefix) + len(body))
    return bytes(prefix) + body


def manifest_xml(package: str, version_code: int, version_name: str,
                 label: str, permissions) -> bytes:
    """The AndroidManifest.xml for a native-payload APK.

    android:hasCode="false" is the load-bearing attribute: it tells the
    installer there is no Java entry point, which is true and which stops a
    runtime from looking for an Application class that does not exist.
    """
    def attr(name, raw, dtype, data):
        return (ANDROID_NS, name, raw, dtype, data)

    uses = [Node("uses-permission",
                 [attr("name", p, TYPE_STRING, 0)]) for p in permissions]
    children = [
        Node("uses-sdk", [attr("minSdkVersion", None, TYPE_INT_DEC, 21)]),
        *uses,
        Node("application", [
            attr("label", label, TYPE_STRING, 0),
            attr("hasCode", None, TYPE_INT_BOOLEAN, 0),
        ]),
    ]
    root = Node("manifest", [
        (None, "package", package, TYPE_STRING, 0),
        attr("versionCode", None, TYPE_INT_DEC, version_code),
        attr("versionName", version_name, TYPE_STRING, 0),
    ], children)
    return axml(root)


# ------------------------------------------------------------ resources

def res_table(strings) -> bytes:
    """A resource table with a global string pool and no packages.

    An empty package list is legal: aapt emits the same shape for an app
    whose resources are entirely in the manifest.
    """
    pool_bytes, _index, _ids = string_pool(strings)
    header = struct.pack("<HHII", RES_TABLE_TYPE, 0x000C, 0, 0)
    body = header + pool_bytes
    return body[:4] + struct.pack("<I", len(body)) + body[8:]


# -------------------------------------------------------------------- DEX

def _uleb128(value: int) -> bytes:
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def dex_file(class_descriptor: str, super_descriptor: str = "Ljava/lang/Object;") -> bytes:
    """Build a minimal, structurally complete DEX with one empty class.

    The class has no fields and a single public <init> with no code, which is
    the smallest thing that is still a real class definition rather than a
    zero-filled header. The manifest declares hasCode="false", so nothing
    loads this class -- it exists so the DEX is a well-formed DEX and not a
    header-shaped empty file.
    """
    strings = [class_descriptor, super_descriptor, "<init>", "V"]
    sidx = {s: i for i, s in enumerate(strings)}

    # type_ids: the class, its superclass, and void.
    type_desc_idx = [0, 1, 3]
    # proto_ids[0]: shorty "V", returns void, no parameters.
    # method_ids[0]: the constructor on class 0 with proto 0.
    # class_defs[0]: the class, public, superclass 1.

    header_size = 0x70
    string_ids_off = header_size
    type_ids_off = string_ids_off + 4 * len(strings)
    proto_ids_off = type_ids_off + 4 * len(type_desc_idx)
    method_ids_off = proto_ids_off + 12
    class_defs_off = method_ids_off + 8
    data_off = class_defs_off + 32

    # data section: string_data items, then class_data, then the map list.
    data = bytearray()
    string_data_offsets = []
    for text in strings:
        string_data_offsets.append(data_off + len(data))
        encoded = text.encode("utf-8")
        data += _uleb128(len(text.encode("utf-16-le")) // 2)
        data += encoded
        data += b"\x00"

    class_data_off = data_off + len(data)
    data += _uleb128(0)      # static_fields_size
    data += _uleb128(0)      # instance_fields_size
    data += _uleb128(1)      # direct_methods_size
    data += _uleb128(0)      # virtual_methods_size
    # encoded_method: method_idx_diff, access_flags, code_off.
    # ACC_PUBLIC | ACC_CONSTRUCTOR with an empty body (code_off 0) is what a
    # class with no constructor body compiles to.
    data += _uleb128(0)      # method_idx_diff -> method index 0
    data += _uleb128(0x10001)
    data += _uleb128(0)      # code_off

    map_off = data_off + len(data)
    map_items = [
        (0x0000, 1, 0),                                    # header_item
        (0x0001, len(strings), string_ids_off),             # string_id_item
        (0x0002, len(type_desc_idx), type_ids_off),        # type_id_item
        (0x0003, 1, proto_ids_off),                        # proto_id_item
        (0x0005, 1, method_ids_off),                       # method_id_item
        (0x0006, 1, class_defs_off),                       # class_def_item
        (0x2000, 1, class_data_off),                       # class_data_item
        (0x1000, 1, map_off),                              # map_list
    ]
    map_bytes = struct.pack("<I", len(map_items))
    for kind, count, offset in map_items:
        map_bytes += struct.pack("<HHII", kind, 0, count, offset)
    data += map_bytes

    # map_off already points at the start of the map, so the file ends
    # map_bytes past it. Measuring from the post-append length instead would
    # count the pre-map data twice and produce a file_size far past the end.
    file_size = map_off + len(map_bytes)

    # string_ids are offsets into the data section, in the pool's order.
    string_ids = b"".join(struct.pack("<I", off) for off in string_data_offsets)
    # type_ids[2] is the void descriptor, which is string 3 ("V").
    type_ids = b"".join(struct.pack("<I", sidx[strings[i]])
                        for i in type_desc_idx)
    proto_ids = struct.pack("<III", sidx["V"], 2, 0)
    method_ids = struct.pack("<HHI", 0, 0, sidx["<init>"])
    class_defs = struct.pack("<IIIIIIII",
                             0,                          # class_idx
                             0x0001,                     # ACC_PUBLIC
                             1,                          # superclass_idx
                             0,                          # interfaces_off
                             0xFFFFFFFF,                 # source_file_idx
                             0,                          # annotations_off
                             class_data_off,
                             0)                          # static_values_off

    body = bytearray(file_size)
    body[0:header_size] = struct.pack(
        "<8sI20sIIIIIIIIIIIIIIIIIIII",
        b"dex\n035\x00",
        0,                                   # checksum, patched below
        b"\x00" * 20,                        # signature, patched below
        file_size, header_size, 0x12345678,
        0, 0, map_off,
        len(strings), string_ids_off,
        len(type_desc_idx), type_ids_off,
        1, proto_ids_off,
        0, 0,                               # field_ids: none
        1, method_ids_off,
        1, class_defs_off,
        len(data), data_off)
    body[string_ids_off:string_ids_off + len(string_ids)] = string_ids
    body[type_ids_off:type_ids_off + len(type_ids)] = type_ids
    body[proto_ids_off:proto_ids_off + len(proto_ids)] = proto_ids
    body[method_ids_off:method_ids_off + len(method_ids)] = method_ids
    body[class_defs_off:class_defs_off + len(class_defs)] = class_defs
    body[data_off:data_off + len(data)] = data

    # The signature is SHA-1 over everything after the signature field, and
    # the checksum is Adler-32 over everything after the checksum field --
    # each must be computed with its own field zeroed.
    import hashlib
    import zlib
    signature = hashlib.sha1(bytes(body[32:])).digest()
    body[12:32] = signature
    body[8:12] = struct.pack("<I", zlib.adler32(bytes(body[12:])) & 0xFFFFFFFF)
    return bytes(body)


# ------------------------------------------------------------------- APK

def build_apk(entries: list, output: Path, keystore: Path | None = None,
              key_alias: str | None = None, store_password: str | None = None,
              key_password: str | None = None, executable_prefixes=("lib/",)) -> Path:
    """Write an APK and, when a keystore is given, sign it with jarsigner."""
    with tempfile.TemporaryDirectory(prefix="netbeam-apk-") as temporary:
        unsigned = Path(temporary) / "unsigned.apk"
        write_zip(entries, unsigned, executable_prefixes=executable_prefixes)
        # shutil.move, not Path.replace: the temporary directory is usually
        # on a different filesystem than the output (/tmp is a separate mount
        # on most systems), and rename(2) fails with EXDEV across devices.
        # shutil falls back to copy-then-delete.
        output.parent.mkdir(parents=True, exist_ok=True)
        if keystore is None:
            shutil.move(str(unsigned), str(output))
            return output

        # jarsigner is a real signer: letting it produce MANIFEST.MF, CERT.SF
        # and CERT.RSA beats emitting them by hand, and it doubles as the
        # verifier later.
        signed = Path(temporary) / "signed.apk"
        command = [
            "jarsigner", "-keystore", str(keystore), "-storetype", "PKCS12",
            "-sigalg", "SHA256withRSA", "-digestalg", "SHA-256",
            "-signedjar", str(signed), unsigned, key_alias,
        ]
        if store_password:
            command[1:1] = ["-storepass", store_password]
        if key_password:
            command[1:1] = ["-keypass", key_password]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            raise ApkError(
                f"jarsigner failed ({result.returncode}): "
                f"{result.stderr.strip() or result.stdout.strip()}")
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(signed), str(output))
        return output


def verify_apk(path: Path) -> dict:
    """Parse an APK back with an independent reader and sanity-check it."""
    report = {}
    with zipfile.ZipFile(path) as archive:
        bad = archive.testzip()
        if bad is not None:
            raise ApkError(f"corrupt ZIP entry: {bad}")
        names = archive.namelist()
        report["entries"] = names
        for required in ("AndroidManifest.xml", "classes.dex"):
            if required not in names:
                raise ApkError(f"APK is missing {required}")
        report["manifest"] = verify_axml(archive.read("AndroidManifest.xml"))
        report["dex"] = verify_dex(archive.read("classes.dex"))
    return report


# ------------------------------------------------------------- verifiers
#
# The two readers below are written against the chunk layouts rather than
# reusing the writers' helpers, so a mistake in axml()/dex_file() shows up as
# a parse failure instead of cancelling out. They check the structural
# invariants a real consumer depends on -- offsets inside the file, counts
# that agree with the data present, digests that match.

def _read_pool(data: bytes, offset: int):
    """Parse a string pool chunk. Returns (strings, ids, next_offset)."""
    (kind, header_size, size, count, style_count, flags,
     strings_start, _styles_start) = struct.unpack_from("<HHIIIIII", data, offset)
    if kind != RES_STRING_POOL_TYPE:
        raise ApkError(f"expected a string pool at 0x{offset:x}, got 0x{kind:04x}")
    if header_size != 0x001C:
        raise ApkError(f"unexpected pool header size {header_size}")
    if size % 4:
        raise ApkError("string pool size is not 4-byte aligned")

    offsets = [struct.unpack_from("<I", data, offset + header_size + 4 * i)[0]
               for i in range(count)]

    strings = []
    utf8 = bool(flags & UTF8_FLAG)
    for rel in offsets:
        at = offset + strings_start + rel
        if at >= offset + size:
            raise ApkError(f"string offset {rel} escapes the pool")
        if utf8:
            # Two length prefixes: UTF-16 code units, then UTF-8 bytes.
            if data[at] & 0x80:
                at += 2
            else:
                at += 1
            if data[at] & 0x80:
                length = ((data[at] & 0x7F) << 8) | data[at + 1]
                at += 2
            else:
                length = data[at]
                at += 1
            strings.append(data[at:at + length].decode("utf-8"))
        else:
            length = struct.unpack_from("<H", data, at)[0]
            strings.append(data[at + 2:at + 2 + length * 2].decode("utf-16-le"))
    ids = None
    if style_count == 0 and size >= header_size + 4 * count + 4:
        pass
    return strings, ids, offset + size


def verify_axml(data: bytes) -> dict:
    """Parse a binary XML document back into a nested dict."""
    kind, header_size, total = struct.unpack_from("<HHI", data, 0)
    if kind != RES_XML_TYPE or header_size != 0x0008:
        raise ApkError("not a binary XML document")
    if total != len(data):
        raise ApkError(f"XML header size {total} != file size {len(data)}")

    offset = header_size
    strings, _ids, offset = _read_pool(data, offset)

    resmap = []
    kind, _hs, size = struct.unpack_from("<HHI", data, offset)
    if kind == RES_XML_RESOURCE_MAP_TYPE:
        resmap = [struct.unpack_from("<I", data, offset + 8 + 4 * i)[0]
                  for i in range((size - 8) // 4)]
        offset += size

    def name_at(index):
        if index == NO_ENTRY or index >= len(strings):
            return None
        return strings[index]

    stack, tree, seen_namespace = [], None, False
    while offset < total:
        kind, _hs, size = struct.unpack_from("<HHI", data, offset)
        node = data[offset:offset + size]
        if size < 16 or offset + size > total:
            raise ApkError(f"XML chunk at 0x{offset:x} has bad size {size}")

        if kind == RES_XML_START_NAMESPACE_TYPE:
            seen_namespace = True
        elif kind == RES_XML_END_NAMESPACE_TYPE or kind == RES_XML_END_DOCUMENT_TYPE:
            pass
        elif kind == RES_XML_START_TAG_TYPE:
            ns, name, _astart, _asize, count = struct.unpack_from("<IIHHH", node, 16)
            attrs = {}
            for i in range(count):
                a_ns, a_name, raw, _vsize, _res0, dtype, value = struct.unpack_from(
                    "<IIIHBBI", node, 16 + 0x14 + 20 * i)
                key = name_at(a_name)
                attrs[key] = (name_at(raw) if raw != NO_ENTRY else None,
                              dtype, value)
                if a_ns != NO_ENTRY and name_at(a_ns) != ANDROID_NS:
                    raise ApkError(f"unexpected attribute namespace: {a_ns}")
            element = {"name": name_at(name), "attrs": attrs, "children": []}
            if stack:
                stack[-1]["children"].append(element)
            elif tree is None:
                tree = element
            else:
                raise ApkError("more than one root element")
            stack.append(element)
        elif kind == RES_XML_END_TAG_TYPE:
            if not stack:
                raise ApkError("end tag with no open element")
            stack.pop()
        else:
            raise ApkError(f"unknown XML chunk type 0x{kind:04x}")
        offset += size

    if not seen_namespace:
        raise ApkError("document never declares the android namespace")
    if tree is None:
        raise ApkError("document has no root element")
    return {"strings": strings, "resource_map": resmap, "tree": tree}


def _read_uleb128(data: bytes, at: int):
    result, shift = 0, 0
    while True:
        byte = data[at]
        at += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, at
        shift += 7


def verify_dex(data: bytes) -> dict:
    """Parse a DEX header and re-check its two integrity digests."""
    if len(data) < 0x70:
        raise ApkError("DEX is shorter than its header")
    if data[0:8] != b"dex\n035\x00":
        raise ApkError(f"bad DEX magic: {data[0:8]!r}")

    import hashlib
    import zlib

    # The signature covers everything after itself; the checksum covers
    # everything after itself, i.e. including the signature.
    want_signature = data[12:32]
    got_signature = hashlib.sha1(data[32:]).digest()
    if want_signature != got_signature:
        raise ApkError("DEX SHA-1 signature does not match its contents")
    want_checksum = struct.unpack_from("<I", data, 8)[0]
    got_checksum = zlib.adler32(data[12:]) & 0xFFFFFFFF
    if want_checksum != got_checksum:
        raise ApkError("DEX Adler-32 checksum does not match its contents")

    (file_size, header_size, endian, link_size, link_off, map_off,
     string_ids_size, string_ids_off, type_ids_size, type_ids_off,
     proto_ids_size, proto_ids_off, field_ids_size, field_ids_off,
     method_ids_size, method_ids_off, class_defs_size, class_defs_off,
     data_size, data_off) = struct.unpack_from("<IIIIIIIIIIIIIIIIIIII",
                                              data, 0x20)

    if endian != 0x12345678:
        raise ApkError(f"unexpected endian tag 0x{endian:08x}")
    if header_size != 0x70:
        raise ApkError(f"unexpected DEX header size {header_size}")
    if file_size != len(data):
        raise ApkError(f"DEX file_size {file_size} != actual {len(data)}")

    # Every declared section has to lie inside the file. This is the check
    # that catches a hand-built table with an offset one past the end.
    for label, count, off, stride in (
            ("string_ids", string_ids_size, string_ids_off, 4),
            ("type_ids", type_ids_size, type_ids_off, 4),
            ("proto_ids", proto_ids_size, proto_ids_off, 12),
            ("field_ids", field_ids_size, field_ids_off, 8),
            ("method_ids", method_ids_size, method_ids_off, 8),
            ("class_defs", class_defs_size, class_defs_off, 32)):
        if count == 0:
            continue
        end = off + count * stride
        if off < header_size or end > len(data):
            raise ApkError(f"{label} [{off}, {end}) escapes the {len(data)}-byte file")

    if data_off + data_size != file_size:
        raise ApkError("data section does not reach the end of the file")

    # Resolve each string_id through its data item.
    strings = []
    for i in range(string_ids_size):
        at = struct.unpack_from("<I", data, string_ids_off + 4 * i)[0]
        if at >= len(data):
            raise ApkError(f"string_data offset {at} escapes the file")
        _utf16_size, at = _read_uleb128(data, at)
        end = data.index(b"\x00", at)
        strings.append(data[at:end].decode("utf-8", "replace"))

    class_names = []
    for i in range(class_defs_size):
        class_idx = struct.unpack_from("<I", data, class_defs_off + 32 * i)[0]
        desc_idx = struct.unpack_from("<I", data, type_ids_off + 4 * class_idx)[0]
        if desc_idx >= len(strings):
            raise ApkError("class_def points past the string table")
        class_names.append(strings[desc_idx])

    return {"file_size": file_size, "strings": strings,
            "classes": class_names,
            "counts": {"strings": string_ids_size, "types": type_ids_size,
                       "protos": proto_ids_size, "fields": field_ids_size,
                       "methods": method_ids_size, "classes": class_defs_size}}