#!/usr/bin/env python3
"""Make a deliberately broken copy of rust_fallback.c for the negative control.

The compositor-frame check verifies that the Rust-free frame entry point still
composites surfaces and paints chrome. Reverting the one-line bridge call below
makes every frame assertion fail while the OpenWeb assertions stay intact.
Production source is never modified.
"""
import pathlib
import sys

OLD = "void windowland_frame(void *wm) { prs_compose_frame(wm); }"
NEW = "void windowland_frame(void *wm) { }"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: rust_fallback_revert_frame.py SOURCE DEST", file=sys.stderr)
        return 2
    src = pathlib.Path(sys.argv[1])
    dst = pathlib.Path(sys.argv[2])
    text = src.read_text()
    if text.count(OLD) != 1:
        print(f"expected exactly one occurrence of: {OLD}", file=sys.stderr)
        return 2
    dst.write_text(text.replace(OLD, NEW))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
