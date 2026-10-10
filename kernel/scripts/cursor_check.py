#!/usr/bin/env python3
"""Verify the Bibata Modern Classic cursor theme the kernel actually loaded.

The kernel's Xcursor loader prints one line per style at boot:

    cursor: arrow left_ptr 24x24 hot=5,1 crc=a09ea7d6

and, once, a rendered probe of the arrow over a known background:

    cursor: probe style=arrow origin=635,399 size=24x24 bg=00224466
    cursor: prow 0 00224466 00224466 ...

This script does not trust either. It re-reads the same Xcursor files the loader
was pointed at, picks the same 24px chunk, and recomputes

  * the FNV-1a of the pixel bytes (proves the whole selected surface is right,
    not just width/height/hotspot), and
  * the alpha-blended readback over the known background (proves the renderer
    honours the hotspot and the blend formula, which a correct loader cannot).

Usage:
    kernel/scripts/cursor_check.py                 # boot the ISO itself
    kernel/scripts/cursor_check.py --serial LOG    # reuse a captured log
    kernel/scripts/cursor_check.py --negative      # no disk attached: MUST fail
"""

import argparse
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # kernel/
CURSORS = os.path.join(ROOT, "disk", "usr", "share", "icons",
                       "Bibata-Modern-Classic", "cursors")
ISO = os.path.join(ROOT, "codeos-1.0.iso")
DISK = os.path.join(ROOT, "disk.img")

XCUR_MAGIC = b"Xcur"
XCUR_IMAGE = 0xFFFD0002

# style name -> file, mirroring kernel/arch/cursor_theme.cpp
STYLES = {
    "arrow": "left_ptr",
    "hand": "hand2",
    "crosshair": "crosshair",
    "text": "xterm",
    "resize_h": "sb_h_double_arrow",
    "resize_v": "sb_v_double_arrow",
    "resize_nwse": "bd_double_arrow",
    "resize_nesw": "fd_double_arrow",
}

PROBE_X, PROBE_Y = 640, 400   # must match fb_cursor_probe() in fb.cpp


def pick_image(path, target=24):
    """Return (w, h, hotx, hoty, pixel_bytes) for the chunk nearest `target`."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != XCUR_MAGIC:
        raise ValueError(f"{path}: not an Xcursor file")
    hsize, _ver, ntoc = struct.unpack_from("<III", data, 4)
    best = None
    for i in range(ntoc):
        ctype, sub, pos = struct.unpack_from("<III", data, hsize + i * 12)
        if ctype != XCUR_IMAGE:
            continue
        key = (abs(sub - target), -sub)
        if best is None or key < best[0]:
            best = (key, pos)
    if best is None:
        raise ValueError(f"{path}: no image chunks")
    pos = best[1]
    _hs, ctype, _sub, _v, w, h, hotx, hoty, _delay = struct.unpack_from(
        "<IIIIIIIII", data, pos)
    if ctype != XCUR_IMAGE:
        raise ValueError(f"{path}: chosen chunk is not an image")
    pix = data[pos + 36:pos + 36 + w * h * 4]
    if len(pix) != w * h * 4:
        raise ValueError(f"{path}: truncated pixel data")
    return w, h, hotx, hoty, pix


def fnv1a_bytes(data):
    h = 2166136261
    for x in data:
        h ^= x
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def blend(src, dst):
    """The kernel's cursor_blend(): integer /255, alpha in the high byte."""
    a = (src >> 24) & 0xFF
    if a == 0:
        return dst & 0xFFFFFF
    if a == 255:
        return src & 0xFFFFFF
    ia = 255 - a
    r = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia) // 255
    g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * ia) // 255
    b = ((src & 0xFF) * a + (dst & 0xFF) * ia) // 255
    return (r << 16) | (g << 8) | b


def parse_probe(log):
    header = None
    rows = {}
    for line in log.splitlines():
        m = re.match(r"cursor: probe style=(\w+) origin=(\d+),(\d+) "
                     r"size=(\d+)x(\d+) bg=([0-9a-fA-F]+)", line)
        if m:
            header = (m.group(1), int(m.group(2)), int(m.group(3)),
                      int(m.group(4)), int(m.group(5)), int(m.group(6), 16))
            continue
        m = re.match(r"cursor: prow (\d+)((?:\s+[0-9a-fA-F]+)+)", line)
        if m and header is not None:
            rows[int(m.group(1))] = [int(t, 16) for t in m.group(2).split()]
    return header, rows


def validate(log):
    checks, fails = [], []

    def chk(ok, msg):
        checks.append(msg)
        if not ok:
            fails.append(msg)

    found = {}
    for m in re.finditer(r"cursor: (\w+) (\w+) (\d+)x(\d+) hot=(\d+),(\d+) "
                         r"crc=([0-9a-f]{8})", log):
        name, fn, w, h, hx, hy, crc = m.groups()
        found[name] = (fn, int(w), int(h), int(hx), int(hy), crc)

    chk("cursor: loaded 8/8" in log, "kernel reports 8/8 styles loaded")

    for style, filename in STYLES.items():
        rec = found.get(style)
        if rec is None:
            chk(False, f"loader line for {style} present")
            continue
        fn, w, h, hx, hy, crc = rec
        chk(fn == filename, f"{style} loads {filename}")
        path = os.path.join(CURSORS, filename)
        if not os.path.exists(path):
            chk(False, f"{path} exists")
            continue
        tw, th, tx, ty, pix = pick_image(path)
        chk((w, h, hx, hy) == (tw, th, tx, ty),
            f"{style} size/hotspot {w}x{h} {hx},{hy} matches file")
        chk(fnv1a_bytes(pix) == int(crc, 16),
            f"{style} pixel CRC {crc} matches file bytes")

    header, rows = parse_probe(log)
    if header is None:
        chk(False, "render probe present (needs a framebuffer boot)")
        return checks, fails

    style, ox, oy, pw, ph, bg = header
    chk(style == "arrow", "probe renders the arrow style")
    _tw, _th, hotx, hoty, pix = pick_image(os.path.join(CURSORS, STYLES["arrow"]))
    # The rendered surface must equal the chunk this check independently picks.
    # Compare before indexing `pix`: a kernel that chose a different size must
    # make the checker report a failure, not fault inside unpack_from (a control
    # that crashes proves nothing).
    size_ok = (pw, ph) == (_tw, _th) and len(pix) >= pw * ph * 4
    chk(size_ok, f"probe surface {pw}x{ph} matches the file's {_tw}x{_th}")
    chk((ox, oy) == (PROBE_X - hotx, PROBE_Y - hoty),
        f"probe origin {ox},{oy} honours hotspot ({hotx},{hoty})")

    if size_ok:
        for r in range(ph):
            vals = rows.get(r)
            if vals is None or len(vals) != pw:
                chk(False, f"probe row {r} has {0 if vals is None else len(vals)} px, want {pw}")
                continue
            want = [blend(struct.unpack_from("<I", pix, (r * pw + c) * 4)[0], bg)
                    for c in range(pw)]
            chk(vals == want, f"probe row {r} matches the blend over bg {bg:06x}")

    return checks, fails


def run_qemu(disk, serial_path, timeout):
    cmd = ["qemu-system-x86_64", "-machine", "pc", "-m", "1G", "-smp", "2",
           "-vga", "std", "-display", "none", "-monitor", "none",
           "-boot", "order=d", "-cdrom", ISO, "-no-reboot",
           "-serial", f"file:{serial_path}"]
    if disk:
        cmd += ["-drive", f"file={DISK},format=raw,if=ide"]
    try:
        subprocess.run(["timeout", str(timeout)] + cmd,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=False)
    except FileNotFoundError:
        raise SystemExit("qemu-system-x86_64 not found")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", help="captured serial log to check instead of booting")
    ap.add_argument("--negative", action="store_true",
                    help="boot with no disk attached; validation MUST fail")
    ap.add_argument("--timeout", type=int, default=45)
    args = ap.parse_args()

    if args.serial:
        log_path = args.serial
    else:
        log_path = "/tmp/cursor_check_serial.log"
        if os.path.exists(log_path):
            os.remove(log_path)
        run_qemu(disk=not args.negative, serial_path=log_path,
                 timeout=args.timeout)

    log = ""
    if os.path.exists(log_path):
        with open(log_path, "r", errors="replace") as fh:
            log = fh.read()

    try:
        checks, fails = validate(log)
    except Exception as exc:
        # A checker that raises must never look like a pass: report it as one
        # failure. This is also the safety net for a kernel that reports a
        # surface shape the checker does not expect.
        print(f"FAIL checker raised: {exc!r}")
        return 1
    for msg in checks:
        print(("PASS " if msg not in fails else "FAIL ") + msg)
    print(f"{len(checks)} checks, {len(fails)} failures")

    if args.negative:
        # The point of the negative control: with no theme volume the loader
        # must not claim success and the checks must actually fail. A control
        # that passes here would mean the check cannot detect a missing theme.
        if fails:
            print("negative control OK: validation failed as it must without a theme")
            return 0
        print("negative control BROKEN: validation passed with no disk attached")
        return 1

    if not log:
        print("no serial output captured")
        return 1
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
