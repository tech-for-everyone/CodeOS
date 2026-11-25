#!/usr/bin/env python3
"""Inspect and validate .xora archives without modifying them.

Examples:

    python3 pkgs/tools/xora.py list
    python3 pkgs/tools/xora.py inspect pkgs/xora/terminal-1.0.0.xora
    python3 pkgs/tools/xora.py verify
"""

from __future__ import annotations

import argparse
import json
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def archive_paths() -> list[Path]:
    found = []
    for folder in ("xora", "dist"):
        base = ROOT / folder
        if base.is_dir():
            found.extend(sorted(base.glob("*.xora")))
    return found


def open_manifest(path: Path) -> tuple[dict, list[str]]:
    """Return the embedded manifest and the payload member names."""
    with tarfile.open(path, "r:gz") as tar:
        names = tar.getnames()
        if "manifest.json" not in names:
            raise ValueError("archive has no manifest.json")
        fh = tar.extractfile("manifest.json")
        if fh is None:
            raise ValueError("manifest.json is not a regular file")
        manifest = json.load(fh)
    return manifest, names


def expected_payload(manifest: dict) -> str | None:
    install = manifest.get("install_path")
    if not isinstance(install, str) or not install.startswith("/"):
        return None
    return "payload" + install


def validate(path: Path) -> list[str]:
    errors: list[str] = []
    try:
        manifest, names = open_manifest(path)
    except Exception as exc:  # unreadable/corrupt archive
        return [f"cannot read archive: {exc}"]

    for field in ("name", "version", "format", "type", "install_path"):
        if not manifest.get(field):
            errors.append(f"manifest is missing {field}")

    if manifest.get("format") not in (None, "xora-1"):
        errors.append(f"unexpected format {manifest['format']!r}")
    if manifest.get("archive") not in (None, "tar.gz"):
        errors.append(f"unexpected archive {manifest['archive']!r}")

    payload = expected_payload(manifest)
    if payload and payload not in names:
        errors.append(f"payload missing expected member {payload}")

    actual_payloads = [n for n in names if n.startswith("payload/")]
    if not actual_payloads:
        errors.append("archive has no payload/ members")
    if actual_payloads and not payload:
        errors.append("cannot map install_path to a payload member")

    return errors


def cmd_list(_args: argparse.Namespace) -> int:
    paths = archive_paths()
    if not paths:
        print("no .xora archives found")
        return 0
    print(f"{'archive':42} {'name':22} {'version':10} {'install_path'}")
    for path in paths:
        try:
            manifest, _ = open_manifest(path)
        except Exception as exc:
            print(f"{path.relative_to(ROOT)}: ERROR {exc}")
            continue
        print(
            f"{path.relative_to(ROOT).as_posix():42} "
            f"{str(manifest.get('name', '?')):22} "
            f"{str(manifest.get('version', '?')):10} "
            f"{manifest.get('install_path', '?')}"
        )
    return 0


def cmd_inspect(args: argparse.Namespace) -> int:
    path = Path(args.archive)
    if not path.is_absolute():
        path = ROOT.parent / path
    manifest, names = open_manifest(path)
    print(f"archive: {path}")
    print("manifest:")
    print(json.dumps(manifest, indent=2, ensure_ascii=False, sort_keys=True))
    print("members:")
    for name in names:
        print(f"  {name}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    paths = [Path(a) if Path(a).is_absolute() else ROOT.parent / a for a in args.archives]
    if not paths:
        paths = archive_paths()
    failures = 0
    for path in paths:
        errors = validate(path)
        label = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        if errors:
            failures += 1
            print(f"FAIL {label}")
            for error in errors:
                print(f"       - {error}")
        else:
            print(f"OK   {label}")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("list", help="summarize every .xora archive").set_defaults(func=cmd_list)

    inspect = sub.add_parser("inspect", help="show manifest and members of one archive")
    inspect.add_argument("archive")
    inspect.set_defaults(func=cmd_inspect)

    verify = sub.add_parser("verify", help="validate one or more archives (default: all)")
    verify.add_argument("archives", nargs="*")
    verify.set_defaults(func=cmd_verify)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
