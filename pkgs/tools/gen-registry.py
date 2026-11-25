#!/usr/bin/env python3
"""Regenerate packages.json from the descriptors stored in this tree.

Run from anywhere:

    python3 pkgs/tools/gen-registry.py

The index is written next to the `pkgs/` directory that contains this script,
so the generator never touches files outside the packaging tree.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Top-level package categories. Each has its own descriptor convention.
CATEGORIES = ("core", "extra", "ccp", "dev", "util")

# Some binaries shipped as artifacts are produced by a package with a
# different directory name. Keep the mapping explicit instead of guessing.
ARTIFACT_ALIASES = {
    "compositor": "panels",
}

ARTIFACT_RE = re.compile(
    r"^(?P<name>.+)-(?P<version>\d+\.\d+\.\d+)"
    r"(?:_(?P<arch>[^.]+))?(?P<ext>\.(?:xora|deb|apk))$"
)


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def read_manifest(path: Path) -> dict:
    with path.open(encoding="utf-8") as fh:
        data = json.load(fh)
    return {
        "name": data.get("name", path.parent.name),
        "version": data.get("version"),
        "description": data.get("description"),
        "license": data.get("license"),
        "install_path": data.get("install_path"),
        "dependencies": data.get("dependencies", []),
        "source": data.get("source"),
        "upstream": data.get("upstream"),
        "type": data.get("type", "source"),
        "category": data.get("category"),
        "size": data.get("size"),
        "format": data.get("format"),
        "port_status": data.get("port_status"),
        "provides": data.get("provides", []),
        "note": data.get("note"),
    }


def discover_packages() -> dict[str, dict]:
    packages: dict[str, dict] = {}
    for category in CATEGORIES:
        base = ROOT / category
        if not base.is_dir():
            continue
        for child in sorted(base.iterdir()):
            if child.is_dir():
                if child.name.startswith("."):
                    continue
                manifest = child / "manifest.json"
                if manifest.is_file():
                    meta = read_manifest(manifest)
                    status = "explicit"
                    descriptor = rel(manifest)
                else:
                    meta = {
                        "name": child.name,
                        "version": None,
                        "description": None,
                        "license": None,
                        "install_path": None,
                        "dependencies": [],
                        "source": "src/" if (child / "src").is_dir() else None,
                        "upstream": None,
                        "type": "source",
                        "category": None,
                        "size": None,
                        "format": None,
                        "port_status": None,
                        "provides": [],
                        "note": None,
                    }
                    status = "legacy"
                    descriptor = None
                meta.update(
                    group=category,
                    category=meta.get("category") or category,
                    path=rel(child),
                    descriptor=descriptor,
                    manifest_status=status,
                )
            else:
                continue
            packages[meta["name"]] = meta
    return packages


def discover_artifacts(packages: dict[str, dict]) -> list[dict]:
    artifacts = []
    for folder in ("dist", "xora"):
        base = ROOT / folder
        if not base.is_dir():
            continue
        for child in sorted(base.iterdir()):
            if not child.is_file():
                continue
            match = ARTIFACT_RE.match(child.name)
            if not match:
                continue
            name = match.group("name")
            owner = ARTIFACT_ALIASES.get(name, name)
            meta = {
                "file": rel(child),
                "format": match.group("ext").lstrip("."),
                "version": match.group("version"),
                "arch": match.group("arch"),
                "package": owner if owner in packages else None,
            }
            artifacts.append(meta)
            if owner in packages:
                packages[owner].setdefault("artifacts", []).append(meta["file"])
    return artifacts


def main() -> None:
    packages = discover_packages()
    artifacts = discover_artifacts(packages)

    for meta in packages.values():
        meta.setdefault("artifacts", [])
        meta["artifacts"].sort()

    index = {
        "schema_version": 1,
        "description": (
            "Registry of every package source, descriptor and built artifact "
            "in the CodeOS packaging tree. Regenerate with "
            "`python3 pkgs/tools/gen-registry.py`."
        ),
        "groups": {
            "core": "Userspace programs and libraries built from source in this tree.",
            "extra": "Additional applications and prebuilt tools.",
            "ccp": "Protocol definitions used by core packages.",
            "dev": "Development toolchain packages.",
            "util": "Utility packages.",
        },
        "descriptor_formats": {
            "manifest.json": "The only descriptor format; see manifest.schema.json.",
            "legacy": "Source directory with no descriptor yet; metadata is minimal.",
        },
        "packages": [packages[name] for name in sorted(packages)],
        "artifacts": artifacts,
    }

    out = ROOT / "packages.json"
    out.write_text(json.dumps(index, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(packages)} packages, {len(artifacts)} artifacts)")


if __name__ == "__main__":
    main()
