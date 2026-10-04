#!/usr/bin/env python3
"""Boot-only check for the `no-desktop` flag: no pixels, just the serial log.

lgame_check.py asserts pixels and so needs the renderer fixes in the image.
This one exists to verify the *boot entry* on its own, before any of that
lands -- it asserts the same marker lines lgame_check.py does, and nothing
else. If this passes and lgame_check.py fails, the entry is fine and the
renderer is not; if this fails, nothing downstream means anything.

Reuses shot.py's QMP client rather than duplicating it.

Usage:
    python3 lgame_boot_check.py
"""

import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shot  # noqa: E402  (needs the sys.path tweak above)

ISO = os.path.join(shot.REPO, "kernel", "codeos-1.0.iso")
SER = "/tmp/lgameboot-ser.sock"
QMP = "/tmp/lgameboot-qmp.sock"


def qemu_argv(mem):
    return [
        "qemu-system-x86_64", "-machine", "q35", "-m", mem, "-smp", "2",
        "-vga", "std", "-display", "none",
        "-boot", "order=d", "-cdrom", ISO,
        "-serial", f"unix:{SER},server,nowait",
        "-qmp", f"unix:{QMP},server,nowait",
        "-nic", "user",
        "-no-reboot",
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot-entry", type=int, default=5, metavar="N",
                    help="limine entry N; 5 is 'CodeOS (Shell + Framebuffer)'")
    ap.add_argument("--boot-timeout", type=float, default=90.0)
    ap.add_argument("--dump-serial", default="/tmp/lgameboot-ser.log")
    args = ap.parse_args()

    if not os.path.exists(ISO):
        raise SystemExit(f"{ISO} not built -- run: make -C kernel codeos-1.0.iso")

    for p in (SER, QMP):
        if os.path.exists(p):
            os.unlink(p)

    # Read the serial back over a plain file: this check only *reads* the
    # guest's output, so -serial file: is enough and there is no reason to
    # carry a socket. lgame_check.py needs the socket because it types.
    serlog = "/tmp/lgameboot-raw.log"
    for p in (serlog,):
        if os.path.exists(p):
            os.unlink(p)

    # Swap the serial transport. Both the "-serial" flag and its value have to
    # go: dropping only the value leaves "-serial -qmp ...", which silently
    # renames -qmp into the serial backend and creates no monitor at all.
    argv = qemu_argv(args.mem)
    i = argv.index("-serial")
    del argv[i:i + 2]
    argv[i:i] = ["-serial", f"file:{serlog}"]

    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    buf = b""
    try:
        time.sleep(2.0)
        q = shot.Qmp(QMP)

        # limine's menu is up for `timeout:` (5s) -- see limine.conf. The keys
        # are timed, not deterministic, so entry 5 is *asserted* below from the
        # serial log rather than assumed from having sent the right number of
        # downs.
        time.sleep(2.0)
        for _ in range(args.boot_entry - 1):
            q.key("down")
            time.sleep(0.2)
        time.sleep(0.4)
        q.key("ret")

        end = time.time() + args.boot_timeout
        while time.time() < end:
            if os.path.exists(serlog):
                with open(serlog, "rb") as f:
                    buf = f.read()
            if b"root#" in buf:
                break
            time.sleep(0.25)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    with open(serlog, "rb") as f:
        raw = f.read()
    with open(args.dump_serial, "w") as f:
        f.write(raw.decode("latin-1", "replace"))
    text = raw.decode("latin-1", "replace")

    fails = []
    if "boot: no-desktop" not in text:
        fails.append(
            f"no 'boot: no-desktop' line: the limine menu keys either landed "
            f"outside the 5s window (so the default entry booted) or entry "
            f"{args.boot_entry} is not the shell+framebuffer entry")
    if "root#" not in text:
        fails.append("the shell prompt never appeared")
    # The whole point of the flag: the desktop must not have taken the machine.
    if "Qt6 Desktop" in text and "boot: no-desktop" in text:
        fails.append("'Qt6 Desktop' appears even though no-desktop was set")

    print(f"serial: {args.dump_serial}")
    if fails:
        print(f"FAIL no-desktop: {len(fails)} problem(s)")
        for f in fails:
            print(f"  - {f}")
        return 1
    print(f"PASS no-desktop: entry {args.boot_entry} reached the shell with the "
          f"framebuffer up and no desktop")
    return 0


if __name__ == "__main__":
    sys.exit(main())
