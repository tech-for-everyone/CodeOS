#!/usr/bin/env python3
"""Package NetBeam as a .xora, a .deb and an .apk from one metadata source.

NetBeam is a CodeOS userspace ELF, so the native format (.xora) is the one
that can actually be installed and run on CodeOS. The .deb and .apk wrap the
same binary for other worlds:

  .xora  gzip'd tar, the format `xora install` understands. This is the only
         one of the three that CodeOS can install.
  .deb   ar archive with debian-binary / control.tar.gz / data.tar.gz, built
         by scripts/debpack.py. Lands the ELF at /usr/bin/netbeam.
  .apk   ZIP with a binary AndroidManifest.xml, a DEX, a resource table and a
         real v1 JAR signature from jarsigner. Lands the ELF at
         lib/x86_64/netbeam.

All three read their name, version and description out of
pkgs/extra/netbeam/manifest.json so they cannot disagree.

What each format is NOT:

  * CodeOS has no .deb or .apk installer. Only .xora installs on CodeOS; the
    other two are for real Debian/Android systems.
  * The APK declares android:hasCode="false" because the payload is a native
    ELF with no Java entry point. It is a well-formed, signed APK, but it
    will not install-and-run on a stock Android device, and it carries no
    Dalvik code (the DEX exists so the container is structurally complete).
  * The DEX and the binary manifest are written from their format specs and
    re-parsed by scripts/apkpack.py's own readers. There is no aapt, d8 or
    apksigner on this host, so nothing here has been cross-checked against a
    reference implementation. The JAR signature *is* real: jarsigner produces
    it and `jarsigner -verify` accepts it.

Usage:
  scripts/mkpkg_netbeam.py [--out DIR] [--binary PATH]
                            [--keystore PATH --key-alias NAME]
                            [--password PW] [--formats xora,deb,apk]
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))

import apkpack  # noqa: E402
from debpack import build_deb, installed_size, verify_deb  # noqa: E402
from xora import pack  # noqa: E402

PACKAGE = REPO / "pkgs" / "extra" / "netbeam"
DEFAULT_BINARY = REPO / "kernel" / "userspace" / "netbeam"
DEFAULT_OUT = REPO / "pkgs" / "dist"

ANDROID_ABI = "x86_64"
MIN_SDK = 21
ANDROID_PERMISSIONS = ["android.permission.INTERNET"]


def load_manifest() -> dict:
    path = PACKAGE / "manifest.json"
    if not path.is_file():
        raise SystemExit(f"mkpkg: missing {path}")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    for key in ("name", "version", "description", "license"):
        if not manifest.get(key):
            raise SystemExit(f"mkpkg: manifest.json is missing '{key}'")
    return manifest


# ---------------------------------------------------------------- .xora

def build_xora(manifest: dict, binary: Path, output: Path) -> Path:
    """Stage payload/ + manifest.json and hand it to the real packer."""
    install_path = manifest.get("install_path", f"/bin/{manifest['name']}")
    with tempfile.TemporaryDirectory(prefix="netbeam-xora-") as temporary:
        stage = Path(temporary)
        (stage / "payload").mkdir()
        target = stage / "payload" / install_path.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(binary, target)
        target.chmod(0o755)
        # xora.pack rewrites format/archive/name/version from the staged
        # manifest, so the staged copy is the same dict plus those keys.
        (stage / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        output.parent.mkdir(parents=True, exist_ok=True)
        pack(argparse.Namespace(source=str(stage), output=str(output),
                                name=manifest["name"], version=manifest["version"]))
    return output


# ----------------------------------------------------------------- .deb

def build_deb_package(manifest: dict, binary: Path, output: Path) -> Path:
    name, version = manifest["name"], manifest["version"]
    files = {
        f"/usr/bin/{name}": (binary.read_bytes(), 0o755),
        f"/usr/share/doc/{name}/copyright": (
            f"{name} is distributed under the {manifest['license']} license.\n"
            .encode("utf-8"), 0o644),
        f"/usr/share/man/man1/{name}.1": (
            man_page(manifest).encode("utf-8"), 0o644),
    }
    control = {
        "Package": name,
        "Version": version,
        # The payload is an x86-64 CodeOS ELF, so amd64 is the architecture it
        # will actually run on. Claiming "all" would be a claim nothing checks.
        "Architecture": "amd64",
        "Maintainer": manifest.get("maintainer",
                                    "CodeOS Packaging <packages@codeos.invalid>"),
        "Installed-Size": str(max(1, installed_size(files))),
        "Section": manifest.get("section", "net"),
        "Priority": "optional",
        "Homepage": manifest.get("homepage", "https://codeos.dev"),
        "Description": manifest["description"],
    }
    return build_deb(control, files, output)


def man_page(manifest: dict) -> str:
    name = manifest["name"]
    return (f'.TH {name.upper()} 1 "" "{manifest["version"]}" "CodeOS"\n'
            f'.SH NAME\n{name} \\- {manifest["description"].split(" (")[0]}\n'
            '.SH SYNOPSIS\n'
            f'.B {name}\n'
            f'.B {name} serve\\ [\\fItransfers\\fR]\n'
            f'.B {name} send\\ \\fIip\\fR\\ \\fIfile\\fR\n'
            f'.B {name} discover\\ [\\fIseconds\\fR]\n'
            f'.B {name} selftest\n'
            '.SH DESCRIPTION\n'
            'Peer-to-peer file transfer over the local network. A sender\n'
            'broadcasts a UDP beacon on port 54917 and listens on TCP 54918;\n'
            'a receiver that answers the beacon is sent the file, which is\n'
            'written to /NetBeam/Incoming.\n'
            '.SH FILES\n'
            '/NetBeam/Incoming\n'
            '    Directory received files are written to.\n'
            '.SH SEE ALSO\n'
            'The NetBeam panel in the CodeOS desktop.\n')


# ----------------------------------------------------------------- .apk

def build_apk_package(manifest: dict, binary: Path, output: Path,
                      keystore: Path | None, key_alias: str | None,
                      password: str | None) -> Path:
    name, version = manifest["name"], manifest["version"]
    # Android application ids are Java package names and may not contain a
    # dash, so the CodeOS name is mapped rather than reused verbatim.
    package_id = "com.codeos." + name.replace("-", "").replace("_", "")
    entries = [
        ("AndroidManifest.xml",
         apkpack.manifest_xml(package_id, 1, version,
                              manifest.get("label", name),
                              ANDROID_PERMISSIONS), False),
        ("classes.dex",
         apkpack.dex_file(f"L{package_id.replace('.', '/')}/NetBeamApplication;"), True),
        ("resources.arsc",
         apkpack.res_table([manifest.get("label", name), package_id]), True),
        (f"lib/{ANDROID_ABI}/{name}", binary.read_bytes(), False),
        # assets/ keeps a copy of the plain-text metadata so the native binary
        # can report its own version without a resource parser.
        ("assets/netbeam.json",
         json.dumps({
             "name": name,
             "version": version,
             "description": manifest["description"],
             "license": manifest["license"],
             "install_path": f"/bin/{name}",
             "native": {"abi": ANDROID_ABI, "entry": f"lib/{ANDROID_ABI}/{name}"},
             "network": {"discovery_port": 54917, "transfer_port": 54918},
             "inbox": "/NetBeam/Incoming",
         }, indent=2, sort_keys=True).encode("utf-8") + b"\n", True),
    ]
    return apkpack.build_apk(entries, output, keystore=keystore,
                             key_alias=key_alias, store_password=password,
                             key_password=password)


# ---------------------------------------------------------------- verify

def verify(output: Path, kind: str, keystore: Path | None = None,
           password: str | None = None) -> None:
    if kind == "deb":
        report = verify_deb(output)
        control = report["control"]
        for key in ("Package", "Version", "Architecture"):
            if key not in control:
                raise SystemExit(f"mkpkg: {output.name}: control is missing {key}")
        # `ar t` is an independent reader: it shares none of debpack's code.
        listing = subprocess.run(["ar", "t", str(output)], capture_output=True, text=True)
        if listing.returncode != 0:
            raise SystemExit(f"mkpkg: {output.name}: ar t failed: {listing.stderr}")
        members = listing.stdout.split()
        if members != report["members"]:
            raise SystemExit(f"mkpkg: {output.name}: ar t says {members}")
        print(f"  ar t        {members}")
        print(f"  control     {control['Package']} {control['Version']} "
              f"[{control['Architecture']}]")
        print(f"  payload     {', '.join(report['payload'])}")

    elif kind == "apk":
        report = apkpack.verify_apk(output)
        print(f"  entries     {', '.join(report['entries'])}")
        print(f"  dex         {report['dex']['classes']} "
              f"{report['dex']['counts']}")
        tree = report["manifest"]["tree"]
        print(f"  manifest    <{tree['name']} "
              f"package={tree['attrs']['package'][0]} "
              f"versionName={tree['attrs']['versionName'][0]}>")
        # jarsigner is a real signature verifier. The zip cannot be tampered
        # with without breaking it, which is the whole point of signing.
        verify_cmd = ["jarsigner", "-verify"]
        if keystore is not None:
            verify_cmd += ["-keystore", str(keystore), "-storepass", str(password or "")]
        signed = subprocess.run(verify_cmd + [str(output)],
                                capture_output=True, text=True)
        lines = [l.strip() for l in (signed.stdout + signed.stderr).splitlines()
                 if l.strip()]
        # jarsigner prints the verdict first and then a block of hints
        # ("Re-run with -verbose..."), so the hint must not be mistaken for
        # the result. The verdict is the only line that starts with "jar".
        verdict = next((l for l in lines if l.startswith("jar")), "")
        if "unsigned" in verdict.lower():
            # An unsigned APK is a legitimate artifact -- it just has no
            # integrity guarantee. Say so rather than letting the hint text
            # masquerade as a pass, and rather than failing the build.
            print(f"  signature   {verdict} (expected: no keystore was given)")
            return
        if signed.returncode != 0 or "verified" not in verdict.lower():
            raise SystemExit(
                f"mkpkg: {output.name}: jarsigner -verify did not verify the "
                f"signature (rc={signed.returncode}): {verdict or lines[:2]}")
        print(f"  signature   {verdict}")

    elif kind == "xora":
        listing = subprocess.run(
            [sys.executable, str(REPO / "scripts" / "xora.py"), "inspect", str(output)],
            capture_output=True, text=True)
        if listing.returncode != 0:
            raise SystemExit(f"mkpkg: {output.name}: xora inspect failed:\n{listing.stderr}")
        print("  " + "\n  ".join(listing.stdout.strip().splitlines()))


# ------------------------------------------------------------------ main

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(DEFAULT_OUT), type=Path)
    parser.add_argument("--binary", default=str(DEFAULT_BINARY), type=Path)
    parser.add_argument("--formats", default="xora,deb,apk",
                        help="comma-separated subset of xora,deb,apk")
    parser.add_argument("--keystore", type=Path,
                        help="PKCS12 keystore for the APK's v1 JAR signature")
    parser.add_argument("--key-alias", default="netbeam")
    parser.add_argument("--password", help="keystore and key password")
    args = parser.parse_args()

    manifest = load_manifest()
    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"mkpkg: no built binary at {binary}\n"
                         f"      build it with: make -C kernel/userspace netbeam")

    kinds = [k.strip() for k in args.formats.split(",") if k.strip()]
    unknown = [k for k in kinds if k not in ("xora", "deb", "apk")]
    if unknown:
        raise SystemExit(f"mkpkg: unknown format(s): {', '.join(unknown)}")

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    stem = f"{manifest['name']}-{manifest['version']}"

    print(f"netbeam {manifest['version']} from {binary}")
    for kind in kinds:
        print(f"\n== {kind}")
        if kind == "xora":
            path = build_xora(manifest, binary, out / f"{stem}.xora")
        elif kind == "deb":
            path = build_deb_package(manifest, binary,
                                     out / f"{stem}_amd64.deb")
        else:
            path = build_apk_package(manifest, binary, out / f"{stem}.apk",
                                     args.keystore, args.key_alias, args.password)
        print(f"  wrote       {path} ({path.stat().st_size} bytes)")
        verify(path, kind, args.keystore, args.password)
    print("\nall requested formats built and verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())