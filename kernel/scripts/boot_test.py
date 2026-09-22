#!/usr/bin/env python3
"""boot_test.py — Automated CodeOS boot verification via QEMU.

Runs the kernel in QEMU, captures serial output, and checks for expected
boot messages. Exits 0 on success, 1 on failure.

Usage:
    python3 scripts/boot_test.py                           # default ISO test
    python3 scripts/boot_test.py --kernel ../flat.bin      # test flat binary
    python3 scripts/boot_test.py --timeout 90              # custom timeout
    python3 scripts/boot_test.py --verbose                 # show full serial log

Requires: qemu-system-x86_64
"""

import subprocess
import sys
import os
import tempfile
import time
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
KERNEL_DIR = os.path.dirname(SCRIPT_DIR)

DEFAULT_TIMEOUT = 75
QEMU_EXTRA_ARGS = ["-machine", "q35", "-vga", "std", "-m", "4G", "-smp", "5",
                    "-display", "none", "-global", "VGA.edid=on"]

REQUIRED_MESSAGES = [
    "CodeOS",
    "kernel",
    "Qt6: Full desktop running",
]

OPTIONAL_MESSAGES = [
    "audio",
    "timer",
    "network",
    "init",
    "mount",
    "userspace",
    "pci",
    "drivers",
]


def find_iso():
    candidates = [
        os.path.join(KERNEL_DIR, "codeos-1-kernel.iso"),
        os.path.join(KERNEL_DIR, "codeos-1-kernel-grub.iso"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def find_flat_bin():
    candidates = [
        os.path.join(KERNEL_DIR, "codeos-1-kernel.flat.bin"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def run_qemu(kernel_path, serial_log, timeout, boot_from="iso"):
    if boot_from == "iso":
        cmd = ["qemu-system-x86_64"] + QEMU_EXTRA_ARGS + [
            "-serial", f"file:{serial_log}",
            "-boot", "order=d",
            "-cdrom", kernel_path,
        ]
    else:
        cmd = ["qemu-system-x86_64"] + QEMU_EXTRA_ARGS + [
            "-serial", f"file:{serial_log}",
            "-kernel", kernel_path,
        ]

    print(f"  QEMU command: {' '.join(cmd[:8])}...")
    try:
        r = subprocess.run(cmd, timeout=timeout, capture_output=True)
        return r.returncode
    except subprocess.TimeoutExpired:
        return 0  # timeout is expected — kernel stays running


def parse_serial(log_path):
    if not os.path.exists(log_path):
        return ""
    with open(log_path, "r", errors="replace") as f:
        return f.read()


def check_messages(serial_text, required, optional):
    results = {"required_found": [], "required_missing": [],
                "optional_found": [], "optional_missing": []}

    for msg in required:
        if re.search(msg, serial_text, re.IGNORECASE):
            results["required_found"].append(msg)
        else:
            results["required_missing"].append(msg)

    for msg in optional:
        if re.search(msg, serial_text, re.IGNORECASE):
            results["optional_found"].append(msg)
        else:
            results["optional_missing"].append(msg)

    return results


def count_boot_phases(serial_text):
    phases = re.findall(r"Phase\s+(\d+)", serial_text)
    return len(set(phases))


def extract_timing(serial_text):
    matches = re.findall(r"(\d+)\s*ms", serial_text)
    if matches:
        return [int(m) for m in matches]
    return []


def main():
    timeout = DEFAULT_TIMEOUT
    verbose = False
    kernel_path = None
    boot_from = "iso"

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--timeout" and i + 1 < len(args):
            timeout = int(args[i + 1])
            i += 2
        elif args[i] == "--verbose" or args[i] == "-v":
            verbose = True
            i += 1
        elif args[i] == "--kernel" and i + 1 < len(args):
            kernel_path = args[i + 1]
            boot_from = "flat"
            i += 2
        elif args[i] == "--help" or args[i] == "-h":
            print(__doc__)
            sys.exit(0)
        else:
            kernel_path = args[i]
            i += 1

    if not kernel_path:
        kernel_path = find_iso()
        if not kernel_path:
            print("Error: no kernel found. Build first: make -C kernel codeos-1-kernel.iso")
            sys.exit(1)
        boot_from = "iso"

    print(f"CodeOS Boot Test")
    print(f"=" * 50)
    print(f"  Kernel: {kernel_path}")
    print(f"  Boot:   {boot_from}")
    print(f"  Timeout: {timeout}s")
    print()

    with tempfile.NamedTemporaryFile(suffix=".log", delete=False) as tmp:
        serial_log = tmp.name

    try:
        print(f"  Booting QEMU...")
        start = time.time()
        run_qemu(kernel_path, serial_log, timeout, boot_from)
        elapsed = time.time() - start
        print(f"  QEMU exited after {elapsed:.1f}s")

        serial_text = parse_serial(serial_log)
        line_count = len(serial_text.strip().split("\n"))
        print(f"  Serial output: {line_count} lines")
        print()

        if verbose:
            print("  --- Serial log ---")
            for line in serial_text.strip().split("\n"):
                print(f"  | {line}")
            print("  --- End log ---")
            print()

        results = check_messages(serial_text, REQUIRED_MESSAGES, OPTIONAL_MESSAGES)

        print(f"Required messages:")
        for msg in results["required_found"]:
            print(f"  [OK]  {msg}")
        for msg in results["required_missing"]:
            print(f"  [FAIL] {msg}")

        if results["optional_found"]:
            print(f"\nOptional messages found:")
            for msg in results["optional_found"]:
                print(f"  [OK]  {msg}")

        phases = count_boot_phases(serial_text)
        if phases > 0:
            print(f"\nBoot phases detected: {phases}")

        timings = extract_timing(serial_text)
        if timings:
            print(f"Timing values found: {len(timings)} ({min(timings)}-{max(timings)} ms)")

        all_required = len(results["required_missing"]) == 0
        print()
        if all_required:
            print("RESULT: PASS")
        else:
            print(f"RESULT: FAIL ({len(results['required_missing'])} required messages missing)")
            sys.exit(1)

    finally:
        os.unlink(serial_log)


if __name__ == "__main__":
    main()
