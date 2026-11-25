#!/usr/bin/env python3
"""Boot-time check for the ArkUI/ACE layout engine.

A green CMake build is not proof that CodeOS uses the engine.  The engine ELF
has to be copied into the image, the kernel has to load it, the raw-syscall
runtime has to survive CodeOS's personality, and the engine's own layout
assertions have to pass.  `kernel/kernel/ace_probe.c` runs /bin/ace-layout on
every boot; this script reads the serial log and asserts on what it printed.

It also types `ace layout` into the shell afterwards, so a user-visible path
to the same engine is covered, not just the boot probe.

Usage:
    ace_boot_check.py [--iso ISO] [--disk DISK] [--timeout N] [--negate]

`--negate` inverts the verdict, which is useful for proving the check can fail
when the engine is not in the image.
"""
import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

# The probe's own lines, then the engine's report.  These are literal strings
# from ace-host/codeos_probe.cpp and kernel/kernel/ace_probe.c; if either file
# changes its output, this list has to change with it.
EXPECTED = [
    r"ACE: spawned tid=\d+",
    r"ACE: load /bin/ace-layout",
    r"ACE: start entry=0x[0-9a-f]+ rsp=0x[0-9a-f]+",
    r"ACEOS layout begin",
    r"\[ok  \] root width got 360 want 360",
    r"\[ok  \] d.x == 80 \(c width\) got 80 want 80",
    r"ACEOS PASS: 0 failure\(s\)",
    r"ACE: done status=0",
]

REJECT = [
    r"ACE: FAIL",
    r"ACE: layout probe timed out",
    r"ACE: no ArkUI layout engine",
    r"!!! PF at RIP",
]

SHELL_COMMAND = "ace layout"
SHELL_NEEDLES = ["ACEOS layout begin", "ACEOS PASS: 0 failure(s)"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default="codeos-1.0.iso")
    ap.add_argument("--disk", default="disk.img")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--negate", action="store_true")
    args = ap.parse_args()

    sock = os.path.join(tempfile.mkdtemp(prefix="aceboot"), "serial")
    qemu = [
        "qemu-system-x86_64", "-machine", "pc", "-m", "1G", "-smp", "2",
        "-vga", "none", "-nographic", "-monitor", "none",
        "-boot", "order=d", "-cdrom", args.iso,
        "-drive", f"file={args.disk},format=raw,if=ide",
        "-no-reboot",
        "-serial", f"unix:{sock},server,nowait",
    ]
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    chunks = []
    failures = []
    try:
        deadline = time.time() + 30
        while True:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(sock)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)

        def text():
            return "".join(chunks)

        def pump(until):
            while time.time() < until:
                try:
                    chunk = s.recv(65536)
                except socket.timeout:
                    continue
                except OSError:
                    return False
                if not chunk:
                    return False
                chunks.append(ANSI.sub("", chunk.decode("utf-8", "replace")))
            return True

        end = time.time() + args.timeout
        s.settimeout(5)
        while time.time() < end:
            pump(min(time.time() + 1.0, end))
            if "ACE: done" in text() and "root#" in text():
                break

        body = text()
        for pattern in EXPECTED:
            if not re.search(pattern, body):
                failures.append(f"missing boot line: {pattern}")
        for pattern in REJECT:
            if re.search(pattern, body):
                failures.append(f"rejected boot line: {pattern}")

        # The guest is at the shell now.  Assert against output that arrives
        # after this command; a whole-log search would be satisfied by the boot
        # probe even if `ace` were not wired up.
        mark = len(text())
        s.settimeout(0.5)
        try:
            s.sendall((SHELL_COMMAND + "\n").encode())
        except OSError:
            failures.append("could not type into the guest")
        shell_end = time.time() + 30
        while time.time() < shell_end:
            pump(min(time.time() + 1.0, shell_end))
            if all(n in text()[mark:] for n in SHELL_NEEDLES):
                break
        after = text()[mark:]
        for needle in SHELL_NEEDLES:
            if needle not in after:
                failures.append(f"missing shell line: {needle}")
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    ok = not failures
    if args.negate:
        ok = not ok
    if ok:
        print("PASS: ArkUI/ACE engine ran at boot and from the shell")
        return 0
    print("FAIL: ArkUI/ACE boot check")
    for failure in failures:
        print(f"  {failure}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
