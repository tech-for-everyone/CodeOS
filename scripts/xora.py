#!/usr/bin/env python3
"""Build and inspect CodeOS .xora application archives.

A .xora file is a gzip-compressed tar archive.  Its layout is deliberately
small and predictable:

    manifest.json       package metadata
    payload/<path>      files installed below the filesystem root

The extension is the application format; gzip keeps the artifact compact
while tar preserves executable bits and directory structure.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import tarfile


FORMAT = "xora-1"
MAX_MANIFEST_BYTES = 64 * 1024
CODE_SUFFIXES = {
    ".bin", ".elf", ".exe", ".dll", ".so", ".a", ".o",
    ".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".rb", ".go",
    ".rs", ".js", ".ts", ".sh", ".lua", ".wasm",
}


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"xora: error: {message}")


def load_manifest(source: Path, name: str | None, version: str | None) -> dict:
    manifest_path = source / "manifest.json"
    manifest: dict = {}
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            fail(f"invalid manifest.json: {exc}")
        if not isinstance(manifest, dict):
            fail("manifest.json must contain an object")

    package_name = name or manifest.get("name")
    package_version = version or manifest.get("version")
    if not isinstance(package_name, str) or not package_name:
        fail("package name is required (--name or manifest.json name)")
    if not isinstance(package_version, str) or not package_version:
        fail("package version is required (--version or manifest.json version)")

    manifest.update({
        "format": FORMAT,
        "name": package_name,
        "version": package_version,
        "archive": "tar.gz",
    })
    for key in ("name", "version", "format", "archive"):
        if not isinstance(manifest[key], str) or not manifest[key]:
            fail(f"manifest field '{key}' must be a non-empty string")
    return manifest


def safe_relative(path: Path, root: Path) -> str:
    relative = path.relative_to(root).as_posix()
    if relative == "manifest.json":
        fail("manifest.json is reserved and must stay at the archive root")
    if relative.startswith("../") or relative == "..":
        fail(f"path escapes source directory: {path}")
    return relative


def add_tree(archive: tarfile.TarFile, source: Path) -> int:
    count = 0
    for path in sorted(source.rglob("*")):
        if path.relative_to(source).as_posix() == "manifest.json":
            continue
        relative = safe_relative(path, source)
        if path.is_symlink():
            fail(f"symbolic links are not allowed: {relative}")
        if path.is_dir():
            continue
        if not path.is_file():
            fail(f"unsupported file type: {relative}")
        archive.add(path, arcname=f"payload/{relative}", recursive=False)
        count += 1
    return count


def pack(args: argparse.Namespace) -> None:
    source = Path(args.source).resolve()
    output = Path(args.output).resolve()
    if not source.is_dir():
        fail(f"source directory not found: {source}")
    if output.suffix != ".xora":
        fail("output file must use the .xora extension")
    if output == source or output.parent == source:
        fail("output archive must be outside the source directory")

    manifest = load_manifest(source, args.name, args.version)
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    payload_root = source / "payload" if (source / "payload").is_dir() else source
    with tarfile.open(output, mode="w:gz", compresslevel=9) as archive:
        info = tarfile.TarInfo("manifest.json")
        info.size = len(manifest_bytes)
        info.mode = 0o644
        archive.addfile(info, fileobj=__import__("io").BytesIO(manifest_bytes))
        count = add_tree(archive, payload_root)
    print(f"created {output} ({count} files, {output.stat().st_size} bytes)")


def read_manifest(archive: tarfile.TarFile) -> dict:
    try:
        member = archive.getmember("manifest.json")
    except KeyError:
        fail("archive has no root manifest.json")
    if not member.isfile() or member.size > MAX_MANIFEST_BYTES:
        fail("manifest.json is missing, not a regular file, or too large")
    handle = archive.extractfile(member)
    if handle is None:
        fail("cannot read manifest.json")
    try:
        manifest = json.loads(handle.read().decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        fail(f"invalid manifest.json: {exc}")
    if not isinstance(manifest, dict) or manifest.get("format") != FORMAT:
        fail(f"manifest format must be {FORMAT}")
    for key in ("name", "version", "archive"):
        if not isinstance(manifest.get(key), str) or not manifest[key]:
            fail(f"manifest field '{key}' must be a non-empty string")
    if manifest["archive"] != "tar.gz":
        fail("manifest archive must be tar.gz")
    return manifest


def validate_member(member: tarfile.TarInfo) -> None:
    path = PurePosixPath(member.name)
    if path.is_absolute() or ".." in path.parts:
        fail(f"unsafe archive path: {member.name}")
    if not (member.name == "manifest.json" or member.name.startswith("payload/")):
        fail(f"archive entry outside manifest.json or payload/: {member.name}")
    if member.issym() or member.islnk() or member.isdev():
        fail(f"unsupported archive entry: {member.name}")


def inspect(args: argparse.Namespace) -> None:
    with tarfile.open(args.archive, mode="r:gz") as archive:
        manifest = read_manifest(archive)
        members = archive.getmembers()
        for member in members:
            validate_member(member)
    print(json.dumps({
        "name": manifest.get("name"),
        "version": manifest.get("version"),
        "files": sum(1 for member in members if member.name.startswith("payload/") and member.isfile()),
        "compressed_bytes": Path(args.archive).stat().st_size,
        "format": manifest["format"],
    }, indent=2))


def unpack(args: argparse.Namespace) -> None:
    destination = Path(args.destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.archive, mode="r:gz") as archive:
        manifest = read_manifest(archive)
        members = archive.getmembers()
        for member in members:
            validate_member(member)
            if member.name == "manifest.json":
                continue
            relative = PurePosixPath(member.name).relative_to("payload")
            target = destination.joinpath(*relative.parts).resolve()
            if destination != target and destination not in target.parents:
                fail(f"unsafe extraction path: {member.name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, target.open("wb") as output:
                if source is not None:
                    output.write(source.read())
            target.chmod(stat.S_IMODE(member.mode) or 0o644)
    print(f"unpacked {manifest['name']} {manifest['version']} -> {destination}")


def install(args: argparse.Namespace) -> None:
    """Install payload files below a CodeOS filesystem root."""
    destination = Path(args.root).resolve()
    if not destination.is_dir():
        fail(f"install root does not exist: {destination}")
    unpack(argparse.Namespace(archive=args.archive, destination=str(destination)))
    with tarfile.open(args.archive, mode="r:gz") as archive:
        manifest = read_manifest(archive)
        paths = [
            PurePosixPath(member.name).relative_to("payload").as_posix()
            for member in archive.getmembers()
            if member.name.startswith("payload/") and member.isfile()
        ]
    records = []
    for relative in paths:
        target = destination.joinpath(*PurePosixPath(relative).parts)
        if not target.is_file():
            fail(f"installed payload is missing: {relative}")
        records.append({
            "path": relative,
            "sha256": sha256_file(target),
            "mode": stat.S_IMODE(target.stat().st_mode),
            "code": is_code_file(target),
        })
    save_baseline(destination, manifest, records)
    print(f"installed {args.archive} into {destination}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(64 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_code_file(path: Path) -> bool:
    return bool(stat.S_IMODE(path.stat().st_mode) & 0o111) or path.suffix.lower() in CODE_SUFFIXES


def baseline_dir(root: Path) -> Path:
    return root / ".xora" / "integrity"


def baseline_path(root: Path, manifest: dict) -> Path:
    name = manifest["name"].replace("/", "_").replace("\\", "_")
    return baseline_dir(root) / f"{name}.json"


def save_baseline(root: Path, manifest: dict, records: list[dict]) -> None:
    directory = baseline_dir(root)
    directory.mkdir(parents=True, exist_ok=True)
    state = {
        "format": "xora-integrity-1",
        "name": manifest["name"],
        "version": manifest["version"],
        "files": records,
    }
    baseline_path(root, manifest).write_text(
        json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def audit(args: argparse.Namespace) -> None:
    root = Path(args.root).resolve()
    directory = baseline_dir(root)
    if not directory.is_dir():
        fail(f"no .xora integrity baselines found below {root}")
    modified = 0
    missing = 0
    repaired = 0
    for state_path in sorted(directory.glob("*.json")):
        try:
            state = json.loads(state_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            fail(f"invalid integrity baseline {state_path}: {exc}")
        name = state.get("name", state_path.stem)
        for record in state.get("files", []):
            relative = record.get("path", "")
            target = root.joinpath(*PurePosixPath(relative).parts).resolve()
            if root != target and root not in target.parents:
                fail(f"unsafe baseline path: {relative}")
            if not target.is_file():
                missing += 1
                print(f"MISSING  {name}: {relative}")
                continue
            current = sha256_file(target)
            if current == record.get("sha256"):
                continue
            modified += 1
            code = bool(record.get("code"))
            action = "DELETE" if args.repair and code else "CHANGED"
            print(f"{action:<8} {name}: {relative}")
            if args.repair and code:
                target.unlink()
                repaired += 1
    print(f"xora audit: {modified} changed, {missing} missing, {repaired} deleted")
    if modified and not args.repair:
        print("xora audit: rerun with --repair to delete changed code files")


def main() -> None:
    parser = argparse.ArgumentParser(description="CodeOS .xora application archive tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    pack_parser = subparsers.add_parser("pack", help="create a compressed .xora archive")
    pack_parser.add_argument("source")
    pack_parser.add_argument("-o", "--output", required=True)
    pack_parser.add_argument("--name")
    pack_parser.add_argument("--version")
    pack_parser.set_defaults(function=pack)

    inspect_parser = subparsers.add_parser("inspect", help="validate and summarize an archive")
    inspect_parser.add_argument("archive")
    inspect_parser.set_defaults(function=inspect)

    unpack_parser = subparsers.add_parser("unpack", help="safely extract an archive")
    unpack_parser.add_argument("archive")
    unpack_parser.add_argument("destination")
    unpack_parser.set_defaults(function=unpack)

    install_parser = subparsers.add_parser("install", help="install payload into a CodeOS root")
    install_parser.add_argument("archive")
    install_parser.add_argument("--root", required=True, help="destination CodeOS filesystem root")
    install_parser.set_defaults(function=install)

    audit_parser = subparsers.add_parser("audit", help="check installed app files")
    audit_parser.add_argument("--root", required=True, help="CodeOS filesystem root")
    audit_parser.add_argument("--repair", action="store_true", help="delete changed code files")
    audit_parser.set_defaults(function=audit)

    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
