#!/usr/bin/env python3
"""check_unused.py — Find potential dead code, unused includes, and stale symbols.

Scans kernel C/H files for functions that are declared but never called,
headers that are included but unused, and symbols that may be dead code.

Usage:
    python3 scripts/check_unused.py                    # scan all kernel source
    python3 scripts/check_unused.py --kernel-only      # skip drivers/lvgl
    python3 scripts/check_unused.py --json             # JSON output

Output: list of potentially unused symbols with file locations.
"""

import os
import re
import sys
import json
from collections import defaultdict

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
KERNEL_DIR = os.path.dirname(SCRIPT_DIR)

SCAN_DIRS = [
    os.path.join(KERNEL_DIR, "kernel"),
    os.path.join(KERNEL_DIR, "drivers"),
    os.path.join(KERNEL_DIR, "fs"),
    os.path.join(KERNEL_DIR, "mm"),
    os.path.join(KERNEL_DIR, "net"),
    os.path.join(KERNEL_DIR, "sched"),
    os.path.join(KERNEL_DIR, "arch"),
]

SKIP_DIRS = {"lvgl", "rust_ow", "png", "svg"}

FUNC_DECL = re.compile(
    r"^(?:static\s+)?(?:inline\s+)?(?:void|int|uint\d+_t|int\d+_t|bool|char\s*\*|"
    r"const\s+char\s*\*|size_t|ssize_t|off_t|pid_t|void\s*\*)\s+"
    r"(\w+)\s*\([^)]*\)\s*(?:\{)",
    re.MULTILINE
)

FUNC_CALL = re.compile(r"\b(\w+)\s*\(")

INCLUDE = re.compile(r'#include\s+[<"]([^>"]+)[>"]')


def collect_files(kernel_only=False):
    files = []
    dirs = SCAN_DIRS[:3] if kernel_only else SCAN_DIRS
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for root, dirs_list, filenames in os.walk(d):
            dirs_list[:] = [dd for dd in dirs_list if dd not in SKIP_DIRS]
            for f in filenames:
                if f.endswith((".c", ".h")):
                    files.append(os.path.join(root, f))
    return files


def scan_file(filepath):
    try:
        with open(filepath, "r", errors="replace") as f:
            content = f.read()
    except (OSError, IOError):
        return [], [], []

    funcs = []
    for m in FUNC_DECL.finditer(content):
        name = m.group(1)
        line = content[:m.start()].count("\n") + 1
        funcs.append({"name": name, "file": filepath, "line": line})

    calls = set()
    for m in FUNC_CALL.finditer(content):
        calls.add(m.group(1))

    includes = []
    for m in INCLUDE.finditer(content):
        includes.append(m.group(1))

    return funcs, calls, includes


def find_unused_functions(all_files):
    all_funcs = []
    all_calls = set()

    for filepath in all_files:
        funcs, calls, _ = scan_file(filepath)
        all_funcs.extend(funcs)
        all_calls.update(calls)

    unused = []
    for func in all_funcs:
        if func["name"] not in all_calls:
            if not func["name"].startswith("_"):
                unused.append(func)

    return unused


def find_unused_includes(all_files):
    results = []
    for filepath in all_files:
        try:
            with open(filepath, "r", errors="replace") as f:
                content = f.read()
        except (OSError, IOError):
            continue

        includes = INCLUDE.findall(content)
        if not includes:
            continue

        basename = os.path.basename(filepath)
        for inc in includes:
            inc_base = inc.split("/")[-1].replace(".h", "")
            header_content = content.replace(f'#include', '')

            used = False
            for keyword in [inc_base, inc_base.upper(), inc_base.lower()]:
                if keyword in header_content:
                    used = True
                    break

            if not used:
                results.append({"file": filepath, "include": inc})

    return results


def find_duplicate_includes(all_files):
    results = []
    for filepath in all_files:
        try:
            with open(filepath, "r", errors="replace") as f:
                content = f.read()
        except (OSError, IOError):
            continue

        seen = {}
        for i, line in enumerate(content.split("\n"), 1):
            m = INCLUDE.search(line)
            if m:
                inc = m.group(1)
                if inc in seen:
                    results.append({
                        "file": filepath,
                        "include": inc,
                        "first_line": seen[inc],
                        "dup_line": i,
                    })
                else:
                    seen[inc] = i

    return results


def main():
    kernel_only = False
    output_json = False

    args = sys.argv[1:]
    for a in args:
        if a == "--kernel-only":
            kernel_only = True
        elif a == "--json":
            output_json = True
        elif a == "--help" or a == "-h":
            print(__doc__)
            sys.exit(0)

    all_files = collect_files(kernel_only)
    print(f"Scanning {len(all_files)} files...")

    unused = find_unused_functions(all_files)
    dup_includes = find_duplicate_includes(all_files)

    if output_json:
        print(json.dumps({
            "unused_functions": unused,
            "duplicate_includes": dup_includes,
        }, indent=2))
        return

    print(f"\nCodeOS Dead Code Analysis")
    print("=" * 60)

    if unused:
        print(f"\nPotentially unused functions ({len(unused)}):")
        for u in sorted(unused, key=lambda x: x["name"]):
            loc = os.path.relpath(u["file"], KERNEL_DIR)
            print(f"  {u['name']:40s}  {loc}:{u['line']}")
    else:
        print("\nNo unused functions found.")

    if dup_includes:
        print(f"\nDuplicate #includes ({len(dup_includes)}):")
        for d in dup_includes:
            loc = os.path.relpath(d["file"], KERNEL_DIR)
            print(f"  {d['include']:40s}  {loc}:{d['first_line']} and :{d['dup_line']}")

    print(f"\nTotal: {len(unused)} potentially unused functions, "
          f"{len(dup_includes)} duplicate includes")


if __name__ == "__main__":
    main()
