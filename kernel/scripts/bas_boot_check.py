#!/usr/bin/env python3
"""Runtime check for the bas cross-toolchain: boot CodeOS and read the log.

The proof that a bas integration works is NOT a green build.  The toolchain can
link an ELF, the disk image can ship it, and the kernel can still fail to load
it -- and every one of those failures is silent at build time.  So this boots
the ISO and asserts on what the bas boot probe actually printed.

The probe is `kernel/kernel/bas_probe.c`, which loads and runs
`/bin/bas-selftest` (built from pkgs/core/bas/src/selftest.ba) as a real
userspace process, exactly as ncvm_probe.c does for /bin/ncvm --selftest.

The probe only covers the *program*.  The ways a user actually reaches a bas
program are the shell's `bas` command and the appvm container path, so the
check drives those too: it types into the guest over the serial socket and
asserts on what came back.  That needs a bidirectional socket
(`-serial unix:...,server,nowait`); the write-only `-serial file:` that the
screenshot harness uses cannot type.

`-vga none` is required.  Without it the kernel initialises VBE, goes graphical
and the serial log goes quiet before the probe runs (see kernel/AGENTS.md).

Usage:
    bas_boot_check.py [--iso ISO] [--disk DISK] [--timeout N] [--negate]

`--negate` inverts the verdict, so a broken bas can be shown to fail on purpose
rather than the check being unable to fail at all.
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

# Every line pkgs/core/bas/src/selftest.ba prints that carries a value, with
# the value it must have.  These are the assertions: a selftest that starts,
# runs and ends while printing the wrong number is a failure, not a pass.
#
# This list is the machine-checkable copy of the comments in selftest.ba --
# change one and you must change the other, or the check asserts values the
# program no longer prints.
#
# Note what is NOT here: `bas-hello` is not run by the probe (only selftest is),
# and no assertion pins a specific errno.  Both were tried and both were wrong:
# the probe runs one program, and this kernel returns -1 or -2 for a missing
# path depending on which lookup answered, so a test hard-coding the value would
# be testing the kernel's error convention rather than the SDK.
EXPECTED = [
    "bas-selftest: start",
    "bas-selftest: vec len = 514",
    "bas-selftest: vec mismatches = 0",
    "bas-selftest: vec sum = 392449",
    "bas-selftest: vec sum correct: true",
    "bas-selftest: str len = 41",
    "bas-selftest: str ends with !: true",
    "bas-selftest: str starts with xxxxx: true",
    "bas-selftest: formatted = 2+3=5",
    "bas-selftest: assert/assert_eq survived",
    "bas-selftest: point dist2 = 25",
    "bas-selftest: rect area = 6",
    "bas-selftest: pid > 0: true",
    "bas-selftest: time > 0: true",
    "bas-selftest: access /bin/shell ok: true",
    "bas-selftest: /bin/shell size > 0: true",
    "bas-selftest: open /bin/shell >= 0: true",
    "bas-selftest: missing open is an error: true",
    "bas-selftest: missing stat is an error: true",
    "bas-selftest: missing access is an error: true",
    "bas-selftest: wrote bytes = 24",
    "bas-selftest: read len = 24",
    "bas-selftest: read matches: true",
    "bas-selftest: /bin listing non-empty: true",
    "bas-selftest: survived sleep",
    "bas-selftest: isqrt(0) = 0",
    "bas-selftest: isqrt(1) = 1",
    "bas-selftest: isqrt(25) = 5",
    "bas-selftest: isqrt(99) = 9",
    "bas-selftest: isqrt(10000) = 100",
    "bas-selftest: gcd(48,18) = 6",
    "bas-selftest: done",
]

# Kernel-side lines: the probe must have run, and it must have exited 0.
KERNEL = [
    r"BAS: spawned tid=\d+",
    r"BAS: load /bin/bas-selftest",
    r"BAS: start entry=0x[0-9a-f]+ rsp=0x[0-9a-f]+",
    r"BAS: done status=0",
]

# A failure anywhere in the log is a failure, even if every expected line is
# also present -- a later probe could fault after bas-selftest finished.
REJECT = [
    r"BAS: FAIL",
    r"BAS: selftest timed out",
    r"!!! PF at RIP",
]

# The shell phase: each entry is (command, expected substrings, timeout).
#
# These are the two ways a user actually reaches a bas program, and neither is
# covered by the boot probe:
#
#   `bas <name>`            the shell's own command (kernel/kernel/shell.c)
#   `appvm run <img> ...`  the container path (kernel/kernel/container.c)
#
# Every assertion is matched against the text that arrived AFTER its command
# was sent, never against the whole log.  `bas hello` and `bas run
# /bin/bas-hello` print the same line, so a whole-log search would let the
# first command satisfy the second and the check would pass with one of the
# two paths broken.
SHELL = [
    ("bas list", ["bas programs:", "/bin/bas-hello", "/bin/bas-selftest"], 30),
    ("bas hello", ["bas: hello from CodeOS"], 30),
    ("bas run /bin/bas-hello", ["bas: hello from CodeOS"], 30),
    ("appvm run debian-minimal /bin/bas-hello",
     ["appvm: exec '/bin/bas-hello'", "bas: hello from CodeOS"], 240),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default="codeos-1.0.iso")
    ap.add_argument("--disk", default="disk.img")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--negate", action="store_true")
    args = ap.parse_args()

    sock = os.path.join(tempfile.mkdtemp(prefix="basboot"), "serial")
    os.makedirs(os.path.dirname(sock), exist_ok=True)

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
    shell_failures = []
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

        def pump(until):
            """Read serial output until `until`; False if the socket died."""
            while time.time() < until:
                try:
                    chunk = s.recv(65536)
                except socket.timeout:
                    continue
                except OSError:
                    return False
                if not chunk:
                    return False
                # Strip per chunk: the offsets used as "since" marks must not
                # shift when a later chunk completes an escape sequence.
                chunks.append(ANSI.sub("", chunk.decode("utf-8", "replace")))
            return True

        def text():
            return "".join(chunks)

        end = time.time() + args.timeout
        s.settimeout(5)
        while time.time() < end:
            pump(min(time.time() + 1.0, end))
            # Stop as soon as the probe has reported, rather than always
            # burning the full timeout.
            if "BAS: done" in text() and "root#" in text():
                break

        # ── shell phase ──
        # The guest is at `root#` now, so the commands below can be typed.
        # The socket is bidirectional (`server,nowait`), which is what makes
        # this possible at all: a write-only serial file cannot be typed into.
        s.settimeout(0.5)
        for cmd, needles, tmo in SHELL:
            mark = len(text())
            try:
                s.sendall((cmd + "\n").encode())
            except OSError:
                shell_failures.append(f"shell `{cmd}`: could not type into the guest")
                break
            for needle in needles:
                until = time.time() + tmo
                found = False
                while time.time() < until:
                    pump(min(time.time() + 0.5, until))
                    if needle in text()[mark:]:
                        found = True
                        break
                if not found:
                    shell_failures.append(f"shell `{cmd}`: missing {needle!r}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    log = "".join(chunks)
    failures = list(shell_failures)
    for pat in KERNEL:
        if not re.search(pat, log):
            failures.append(f"kernel log missing: {pat}")
    for line in EXPECTED:
        if line not in log:
            failures.append(f"missing: {line}")
    for pat in REJECT:
        if re.search(pat, log):
            failures.append(f"rejected pattern present: {pat}")

    print("=== bas output from the serial log ===")
    for line in log.splitlines():
        if "BAS" in line or "bas" in line:
            print("   ", line.strip())
    print()

    if failures and args.negate:
        print(f"NEGATIVE CONTROL: {len(failures)} checks failed, as expected -- "
              "this is what a broken bas integration looks like")
        return 0
    if failures:
        print("FAIL")
        for f in failures:
            print("  -", f)
        return 1
    print(f"PASS: {len(EXPECTED)} bas program assertions, {len(KERNEL)} "
          f"kernel-probe lines and {len(SHELL)} shell commands verified on a "
          f"real CodeOS boot")
    return 0


if __name__ == "__main__":
    sys.exit(main())