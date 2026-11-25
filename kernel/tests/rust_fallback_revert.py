#!/usr/bin/env python3
"""Make a deliberately broken copy of rust_fallback.c for the negative control.

The check target verifies that the Rust-free fallback maintains sane tab
accounting.  Reverting the one-line fix below makes the first tab_new report a
zero tab count, so the same assertions must fail.  Production source is never
modified.
"""
import pathlib
import sys

OLD = "if (idx >= s_tab_count) s_tab_count = idx + 1;"
NEW = "if (idx >= s_tab_count) s_tab_count = idx;"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: rust_fallback_revert.py SOURCE DEST", file=sys.stderr)
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
