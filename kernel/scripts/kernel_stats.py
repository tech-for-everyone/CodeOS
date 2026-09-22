#!/usr/bin/env python3
"""kernel_stats.py — Analyze CodeOS kernel binary size and symbol breakdown.

Usage:
    python3 scripts/kernel_stats.py                          # analyze default kernel
    python3 scripts/kernel_stats.py ../codeos-1-kernel.bin   # analyze specific binary
    python3 scripts/kernel_stats.py --top 30                 # show top 30 symbols

Requires: x86_64-elf-nm, x86_64-elf-size (from cross toolchain)
"""

import subprocess
import sys
import os
import re
from collections import defaultdict

CROSS = "x86_64-elf-"
KERNEL_BIN = "codeos-1-kernel.bin"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
KERNEL_DIR = os.path.dirname(SCRIPT_DIR)


def run(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        return r.stdout
    except FileNotFoundError:
        return None
    except subprocess.TimeoutExpired:
        return None


def get_size(kernel):
    out = run([CROSS + "size", kernel])
    if not out:
        return None
    lines = out.strip().split("\n")
    if len(lines) < 2:
        return None
    parts = lines[1].split()
    if len(parts) >= 4:
        return {"text": int(parts[1]), "data": int(parts[2]), "bss": int(parts[3]),
                "dec": int(parts[1]) + int(parts[2]) + int(parts[3])}
    return None


def get_symbols(kernel):
    out = run([CROSS + "nm", "--print-size", "-S", "--size-sort", kernel])
    if not out:
        return []
    syms = []
    for line in out.strip().split("\n"):
        parts = line.split()
        if len(parts) >= 4:
            addr, sz, typ, name = parts[0], parts[1], parts[2], " ".join(parts[3:])
            try:
                size = int(sz, 16)
            except ValueError:
                continue
            syms.append({"addr": addr, "size": size, "type": typ, "name": name})
    return syms


def categorize(name):
    if name.startswith("."):
        return "section"
    if "init" in name.lower():
        return "init"
    if "driver" in name.lower() or "_init" in name:
        return "driver"
    if "syscall" in name.lower():
        return "syscall"
    if "net" in name.lower() or "tcp" in name.lower() or "udp" in name.lower() or "arp" in name.lower() or "dns" in name.lower():
        return "network"
    if "fs" in name.lower() or "ext2" in name.lower() or "vfs" in name.lower():
        return "filesystem"
    if "sched" in name.lower() or "proc" in name.lower():
        return "scheduler"
    if "alloc" in name.lower() or "malloc" in name.lower() or "slab" in name.lower() or "mm" in name.lower():
        return "memory"
    if "qt" in name.lower() or "panel" in name.lower() or "desktop" in name.lower():
        return "gui"
    if "panic" in name.lower() or "error" in name.lower() or "fault" in name.lower():
        return "error_handling"
    if "crypto" in name.lower() or "tls" in name.lower() or "ssl" in name.lower():
        return "crypto"
    if "audio" in name.lower() or "mixer" in name.lower() or "pcm" in name.lower():
        return "audio"
    if "security" in name.lower() or "cap" in name.lower() or "audit" in name.lower():
        return "security"
    return "other"


def format_size(n):
    if n >= 1024 * 1024:
        return f"{n / (1024*1024):.1f} MB"
    if n >= 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n} B"


def main():
    top_n = 20
    kernel = os.path.join(KERNEL_DIR, KERNEL_BIN)

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--top" and i + 1 < len(args):
            top_n = int(args[i + 1])
            i += 2
        elif args[i] == "--help" or args[i] == "-h":
            print(__doc__)
            sys.exit(0)
        else:
            kernel = args[i]
            i += 1

    if not os.path.exists(kernel):
        print(f"Error: kernel binary not found: {kernel}")
        print("Build the kernel first: make -C kernel all")
        sys.exit(1)

    print(f"CodeOS Kernel Analysis: {os.path.basename(kernel)}")
    print("=" * 60)

    sz = get_size(kernel)
    if sz:
        print(f"\nSection sizes:")
        print(f"  .text (code):  {format_size(sz['text'])}")
        print(f"  .data (data):  {format_size(sz['data'])}")
        print(f"  .bss  (zeros): {format_size(sz['bss'])}")
        print(f"  Total:         {format_size(sz['dec'])}")
    else:
        print("\nCould not read section sizes (is x86_64-elf-size installed?)")

    syms = get_symbols(kernel)
    if not syms:
        print("\nCould not read symbols (is x86_64-elf-nm installed?)")
        sys.exit(0)

    total_code = sum(s["size"] for s in syms if s["type"] in ("t", "T"))
    total_data = sum(s["size"] for s in syms if s["type"] in ("d", "D"))
    total_ro = sum(s["size"] for s in syms if s["type"] in ("r", "R"))
    total_bss = sum(s["size"] for s in syms if s["type"] in ("b", "B"))

    print(f"\nSymbol breakdown:")
    print(f"  Text symbols:  {len([s for s in syms if s['type'] in ('t','T')])}")
    print(f"  Data symbols:  {len([s for s in syms if s['type'] in ('d','D')])}")
    print(f"  RO symbols:    {len([s for s in syms if s['type'] in ('r','R')])}")
    print(f"  BSS symbols:   {len([s for s in syms if s['type'] in ('b','B')])}")
    print(f"  Total symbols: {len(syms)}")

    cats = defaultdict(lambda: {"count": 0, "size": 0})
    for s in syms:
        cat = categorize(s["name"])
        cats[cat]["count"] += 1
        cats[cat]["size"] += s["size"]

    print(f"\nCategory breakdown:")
    for cat, info in sorted(cats.items(), key=lambda x: -x[1]["size"]):
        pct = (info["size"] / total_code * 100) if total_code > 0 else 0
        print(f"  {cat:20s}  {format_size(info['size']):>10s}  ({info['count']:4d} syms, {pct:5.1f}%)")

    code_syms = [s for s in syms if s["type"] in ("t", "T") and s["size"] > 0]
    code_syms.sort(key=lambda s: -s["size"])
    print(f"\nTop {top_n} largest symbols:")
    for i, s in enumerate(code_syms[:top_n]):
        print(f"  {i+1:3d}. {format_size(s['size']):>10s}  {s['name'][:70]}")

    total_fn = len(code_syms)
    if total_fn > 0:
        avg = sum(s["size"] for s in code_syms) / total_fn
        median = sorted(s["size"] for s in code_syms)[total_fn // 2]
        print(f"\nFunction stats:")
        print(f"  Total functions: {total_fn}")
        print(f"  Average size:    {format_size(int(avg))}")
        print(f"  Median size:     {format_size(median)}")
        print(f"  Largest:         {format_size(code_syms[0]['size'])} ({code_syms[0]['name']})")

    print()


if __name__ == "__main__":
    main()
