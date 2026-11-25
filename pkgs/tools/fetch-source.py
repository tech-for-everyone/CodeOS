#!/usr/bin/env python3
"""Fetch the upstream source archive named by a prebuilt package manifest.

    python3 pkgs/tools/fetch-source.py util/bzip2
    python3 pkgs/tools/fetch-source.py dev/gcc --force

Downloads are written only to pkgs/ports/<name>/upstream/. Nothing outside the
packaging tree is touched, and existing archives are never overwritten unless
--force is given.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.request
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")


def find_manifest(query: str) -> Path | None:
    matches = []
    for path in ROOT.glob("*/*/manifest.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        name = data.get("name", "")
        rel = path.parent.relative_to(ROOT).as_posix()
        if query in (name, rel):
            matches.append(path)
    return matches[0] if len(matches) == 1 else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", help="package name or group/name, e.g. gzip or dev/gcc")
    parser.add_argument("--force", action="store_true", help="overwrite an existing download")
    args = parser.parse_args()

    manifest_path = find_manifest(args.package)
    if manifest_path is None:
        print(f"error: no unique package matching {args.package!r}", file=sys.stderr)
        return 2

    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    name = data.get("name", "")
    if not NAME_RE.fullmatch(name):
        print(f"error: invalid package name {name!r}", file=sys.stderr)
        return 2
    if data.get("type") != "prebuilt":
        print(f"error: {name} is {data.get('type')!r}, not prebuilt", file=sys.stderr)
        return 2

    url = data.get("source", "")
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        print(f"error: {name} has no downloadable upstream URL", file=sys.stderr)
        return 2

    filename = unquote(Path(parsed.path).name) or "upstream.tar.gz"
    outdir = ROOT / "ports" / name / "upstream"
    dest = outdir / filename

    if not dest.resolve().is_relative_to(ROOT):
        print("error: resolved destination is outside pkgs/", file=sys.stderr)
        return 2
    if dest.exists() and not args.force:
        print(f"already downloaded: {dest.relative_to(ROOT)} (use --force to replace)")
        return 0

    outdir.mkdir(parents=True, exist_ok=True)
    print(f"fetching {url}")
    urllib.request.urlretrieve(url, dest)
    print(f"saved {dest.relative_to(ROOT)} ({dest.stat().st_size} bytes)")
    group = manifest_path.parent.parent.name
    print(f"next: vendor the needed files into {group}/{name}/src/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
