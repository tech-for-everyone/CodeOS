#!/usr/bin/env python3
"""Boot the CodeOS ISO with a framebuffer and capture what the desktop looks like.

A visual change is only a change if you can see it, and HyperDE draws straight
into the framebuffer: there is no windowing layer to screenshot from the host.
So this boots the same ISO fs_test.py uses, with a real VGA device instead of
`-vga none`, waits for the shell prompt, optionally types a command, and pulls
the frame out through QEMU's `screendump`.

    ./kernel/scripts/shot.py                       # /tmp/codeos-shot.png
    ./kernel/scripts/shot.py --at 25 --cmd 'appvm list'
    ./kernel/scripts/shot.py --serial /tmp/shot-serial.log --out /tmp/a.png

The PPM QEMU writes is converted to PNG here rather than shelling out to
ImageMagick, which is not installed on this host.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ISO = os.path.join(REPO, "kernel", "codeos-1.0.iso")
SOCK = "/tmp/codeos-shot-qmp.sock"
LOG = "/tmp/codeos-shot-serial.log"
PPM = "/tmp/codeos-shot.ppm"
QMP = "/tmp/codeos-shot-qmp.out"


# ── PPM → PNG ──────────────────────────────────────────────────────────────
def ppm_to_png(src, dst):
    """Convert a binary P6 PPM to PNG using only the standard library."""
    import struct
    import zlib

    with open(src, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise SystemExit(f"{src}: not a binary PPM (starts {data[:2]!r})")

    # Header: P6 <w> <h> <maxval>, whitespace/comment separated.
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":                     # comment to EOL
            while pos < len(data) and data[pos] != 0x0A:
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1                                              # single whitespace
    w, h, maxval = fields
    if maxval != 255:
        raise SystemExit(f"{src}: maxval {maxval} (only 255 supported)")

    rgb = data[pos:pos + w * h * 3]
    if len(rgb) != w * h * 3:
        raise SystemExit(f"{src}: truncated ({len(rgb)} of {w * h * 3} bytes)")

    raw = bytearray()
    for y in range(h):
        raw.append(0)                                     # filter type 0
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
           + chunk(b"IEND", b""))
    with open(dst, "wb") as f:
        f.write(png)
    return w, h


# ── QMP ────────────────────────────────────────────────────────────────────
class Qmp:
    """The minimum QMP client needed for one screendump."""

    def __init__(self, path, deadline=20.0):
        end = time.time() + deadline
        while time.time() < end:
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.settimeout(20.0)
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.25)
        else:
            raise SystemExit(f"could not connect to QMP socket {path}")
        self.f = self.s.makefile("rwb")
        # The greeting is {"QMP": {...}} -- neither "return" nor "error" -- so it
        # must be consumed directly. Feeding it to _read() loops straight past it
        # and blocks forever waiting for a reply to a command we never sent.
        greet = self.f.readline()
        if not greet or b'"QMP"' not in greet:
            raise SystemExit(f"no QMP greeting (got {greet!r:.60})")
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:                                        # skip async events
            line = self.f.readline()
            if not line:
                raise SystemExit("QMP closed the connection")
            msg = json.loads(line)
            if "return" in msg or "error" in msg:
                return msg

    def cmd(self, name, **args):
        self.f.write((json.dumps({"execute": name, "arguments": args}
                                 if args else {"execute": name}) + "\n").encode())
        self.f.flush()
        return self._read()

    def screendump(self, path):
        r = self.cmd("screendump", filename=path)
        if "error" in r:
            raise SystemExit(f"screendump failed: {r['error']}")

    def key(self, qcode, hold_ms=0):
        """Send one key by QEMU qcode name ('esc', 'ret', 'a', ...)."""
        r = self.cmd("send-key", keys=[{"type": "qcode", "data": qcode}],
                     **{"hold-time": hold_ms} if hold_ms else {})
        if "error" in r:
            raise SystemExit(f"send-key failed: {r['error']}")

    def move_rel(self, dx, dy):
        """Move the pointer by a relative delta.

        The only pointer device the kernel enumerates is a PS/2 mouse, which
        reports *relative* motion -- `usb-tablet` is attached on the USB bus
        but never comes up as a HID device (`usb controller(s) found`, no HID
        lines, `wacom none`), so the usb-tablet device added to the command
        line is decorative and absolute-axis events go nowhere. Verified by
        clicking with them and watching the launcher's state never change.

        Large deltas are the way to aim: the pointer clamps at the screen edge,
        so one big move pins it to a corner and a second move puts it at an
        absolute position, without needing to know where it started.
        """
        ev = []
        if dx:
            ev.append({"type": "rel", "data": {"axis": "x", "value": int(dx)}})
        if dy:
            ev.append({"type": "rel", "data": {"axis": "y", "value": int(dy)}})
        if not ev:
            return
        r = self.cmd("input-send-event", events=ev)
        if "error" in r:
            raise SystemExit(f"relative move failed: {r['error']}")

    def goto(self, x, y):
        """Corner the pointer, then move to an absolute guest position."""
        self.move_rel(-4096, -4096)
        time.sleep(0.2)
        self.move_rel(int(x), int(y))
        time.sleep(0.2)

    def click(self, x, y):
        """Synthesise a left click at guest position (x, y).

        Aims by relative motion (see move_rel) rather than by absolute axis:
        the kernel only ever brings up a PS/2 mouse, so absolute events are
        silently dropped and a click with them does nothing at all.
        """
        self.goto(x, y)
        ev = [
            {"type": "btn", "data": {"down": True, "button": "left"}},
            {"type": "btn", "data": {"down": False, "button": "left"}},
        ]
        r = self.cmd("input-send-event", events=ev)
        if "error" in r:
            raise SystemExit(f"input-send-event failed: {r['error']}")


def has_dots(path):
    """True if the PPM contains any traffic-light pixel.

    Deliberately counts the palette's three vivid colours *and* the grey an
    unfocused window uses, and accepts them anywhere in the frame: the point
    is only "did a window band get composited at all", which is a different
    and much more robust question than "are the lights on the left".
    """
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return False
    for c in (b"\xff\x5f\x57", b"\xff\xbd\x2e", b"\x28\xc8\x40", b"\x5a\x5a\x5e"):
        if c in data:
            return True
    return False


def qemu_argv(iso=None, mem="4G", serial=None, sock=None):
    """The boot line both this script and chrome_check.py need."""
    return [
        "qemu-system-x86_64", "-machine", "q35", "-m", mem, "-smp", "2",
        "-vga", "std",                      # a real framebuffer: HyperDE draws here
        "-display", "none",                 # ... but nobody has to watch it
        "-usb", "-device", "usb-tablet",    # absolute pointer, so --click is absolute
        "-boot", "order=d", "-cdrom", iso or ISO,
        "-serial", f"file:{serial or LOG}",
        "-qmp", f"unix:{sock or SOCK},server,nowait",
        "-nic", "user",
    ]


def launch(qemu):
    """Start QEMU and return (proc, Qmp) with capabilities negotiated."""
    if not os.path.exists(qemu[qemu.index("-cdrom") + 1]):
        raise SystemExit(f"{qemu[qemu.index('-cdrom') + 1]} not built -- "
                         f"run: make -C kernel codeos-1.0.iso")
    for p in (SOCK, PPM):
        if os.path.exists(p):
            os.unlink(p)
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        # -qmp unix:...,server,nowait bind()s and listen()s during option
        # parsing, so connect() succeeds against the backlog seconds before the
        # monitor thread serves it -- and a client that has already connected
        # is never accepted. Wait for QEMU to be past that, then let
        # capabilities negotiation be the real readiness gate.
        time.sleep(2.0)
        return proc, Qmp(SOCK)
    except BaseException:
        proc.terminate()
        raise


def shutdown(proc):
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--at", type=float, default=22.0,
                    help="seconds to wait before the capture")
    ap.add_argument("--click", action="append", default=[], metavar="X,Y",
                    help="click at a guest coordinate before the capture; "
                         "repeatable, applied in order")
    ap.add_argument("--settle", type=float, default=2.0,
                    help="seconds to wait after each click")
    ap.add_argument("--expect-dots", action="store_true",
                    help="keep re-capturing until a frame actually contains "
                         "window chrome; the desktop is double-buffered, so a "
                         "single timed dump can land on the buffer that was "
                         "never presented and look like an empty desktop")
    ap.add_argument("--expect-timeout", type=float, default=25.0)
    ap.add_argument("--out", default="/tmp/codeos-shot.png")
    ap.add_argument("--serial", default=LOG)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot-entry", type=int, default=0, metavar="N",
                    help="pick limine boot entry N (1-based) instead of "
                         "letting default_entry auto-boot. 0 = current "
                         "behaviour. Entry 4 is 'CodeOS (No Demos)', which "
                         "suppresses the three self-test windows.")
    ap.add_argument("--boot-entry-at", type=float, default=2.5,
                    help="seconds after launch to send the menu keys; the "
                         "limine menu is up for `timeout:` seconds (5 in "
                         "limine.conf) so this must land inside that window")
    args = ap.parse_args()

    if not os.path.exists(ISO):
        raise SystemExit(f"{ISO} not built -- run: make -C kernel codeos-1.0.iso")

    for p in (args.serial,):
        if os.path.exists(p):
            os.unlink(p)

    proc, q = launch(qemu_argv(mem=args.mem, serial=args.serial))
    try:
        if args.boot_entry >= 2:
            # Navigate the limine menu rather than patching default_entry in
            # limine.conf and rebuilding the ISO -- this keeps the artifact
            # under test identical to the one users get, which is the whole
            # point of a reproducible check.
            #
            # This is menu-timed, not deterministic: the keys have to land
            # while the menu is up (`timeout:` in limine.conf, 5s). If they
            # land early they are dropped by the BIOS and the default entry
            # boots instead, silently. So callers must confirm from the serial
            # log that the entry they asked for is the one that ran --
            # chrome_check.py does exactly that.
            time.sleep(args.boot_entry_at)
            for _ in range(args.boot_entry - 1):
                q.key("down")
                time.sleep(0.2)
            time.sleep(0.4)
            q.key("ret")

        time.sleep(args.at)
        for spec in args.click:
            x, _, y = spec.partition(",")
            q.click(x, y)
            time.sleep(args.settle)

        if args.expect_dots:
            # Poll for a frame that actually shows window chrome. Two runs of
            # an identical boot produced byte-identical serial logs and
            # screenshots that differed entirely: one had the window bands,
            # the other was bare background. The desktop double-buffers, so
            # whether a given screendump catches the presented buffer or the
            # one behind it is a coin flip -- retrying turns a coin flip into
            # a deadline.
            deadline = time.time() + args.expect_timeout
            found = False
            tries = 0
            while time.time() < deadline:
                tries += 1
                q.screendump(PPM)
                time.sleep(0.4)
                if has_dots(PPM):
                    found = True
                    break
                time.sleep(0.6)
            print(f"chrome visible after {tries} capture(s)"
                  f"{'' if found else ' -- NEVER APPEARED'}")
            if not found:
                ppm_to_png(PPM, args.out)
                raise SystemExit(f"{args.out} has no window chrome; the "
                                 f"layout cannot be judged from it")
        else:
            q.screendump(PPM)
            time.sleep(1.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    w, h = ppm_to_png(PPM, args.out)
    print(f"{args.out}  {w}x{h}")
    print(f"serial: {args.serial}")

    # Report what the guest said, so a blank screen is distinguishable from a
    # desktop that never came up.
    if os.path.exists(args.serial):
        with open(args.serial, "r", errors="replace") as f:
            text = f.read()
        marks = [m for m in ("HYPERDE", "OWPANIC", "PAGE FAULT", "PANIC")
                 if m in text]
        print(f"serial {len(text)} bytes; markers: {marks or 'none'}")


if __name__ == "__main__":
    sys.exit(main())
