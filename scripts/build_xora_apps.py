#!/usr/bin/env python3
"""Package every built CodeOS userspace application as a .xora archive."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import stat
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xora import pack  # noqa: E402


def install_path(name: str, manifest: dict) -> str:
    if isinstance(manifest.get("install_path"), str) and manifest["install_path"]:
        return manifest["install_path"]
    if name == "init":
        return "/init"
    if name == "ld-codeos":
        return "/lib/ld-codeos.so"
    return f"/bin/{name}"


def package_app(binary: Path, manifest_path: Path | None, output_dir: Path) -> Path:
    name = binary.name
    manifest = {}
    if manifest_path and manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    version = manifest.get("version", "1.0.0")
    manifest.update({
        "name": manifest.get("name", name),
        "version": version,
        "description": manifest.get("description", f"CodeOS userspace application: {name}"),
        "license": manifest.get("license", "MIT"),
        "type": "binary",
        "install_path": install_path(name, manifest),
        "source": "payload/",
    })
    output = output_dir / f"{manifest['name']}-{version}.xora"
    with tempfile.TemporaryDirectory(prefix=f"xora-{name}-") as temporary:
        stage = Path(temporary)
        (stage / "payload").mkdir(parents=True)
        (stage / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        target = stage / "payload" / install_path(name, manifest).lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(binary.read_bytes())
        target.chmod(stat.S_IMODE(binary.stat().st_mode) or 0o755)
        pack(argparse.Namespace(source=str(stage), output=str(output), name=None, version=None))
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--userspace", default="kernel/userspace")
    parser.add_argument("--packages", default="pkgs/core")
    parser.add_argument("--output", default="pkgs/xora")
    args = parser.parse_args()

    userspace = Path(args.userspace)
    packages = Path(args.packages)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)
    binaries = sorted(path for path in userspace.iterdir()
                      if path.is_file() and path.stat().st_mode & stat.S_IXUSR)
    if not binaries:
        raise SystemExit(f"no executable userspace apps found in {userspace}")

    for binary in binaries:
        manifest_path = packages / binary.name / "manifest.json"
        package_app(binary, manifest_path if manifest_path.is_file() else None, output_dir)
    print(f"packaged {len(binaries)} CodeOS apps into {output_dir}")


if __name__ == "__main__":
    main()
