#!/usr/bin/env python3
"""Where do window lights actually appear, frame by frame?

`chrome_check.py` answers "does a frame pass", which is the right question for a
regression gate but the wrong question for a diagnosis: when it fails, all you
learn is that *some* expected light was missing, and the desktop's
double-buffering means a failing capture may simply have been one of the
half-composited ones.

So this walks a run and reports, for every capture, the distinct light centre
positions it can find and how many lit pixels it saw. That distinguishes the
three cases that look identical from a single failing frame:

  * the lights are at the COSMIC position -> the change did not take effect
  * the lights are at the macOS position  -> the change works, the frame is bad
  * no lights at all                    -> the frame is not composited

The macOS geometry for a window at (x, y) is close_x = x + WIN_SH + 14, then
+20 and +40, on the band row y + WIN_SH + (WIN_TB - 12) / 2 + 6.  The COSMIC
geometry (what this replaced) was the same band, controls at x + w - 24 going
backwards.  Both are printed so a reader can tell them apart by eye.

usage: chrome_scan.py [--key meta_l+ret] [--at 40] [--frames 60] [--interval 1.0]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WIN_SH = 6
WIN_TB = 30
PITCH = 20
LEFT_INSET = 14
BAR_H = 28

CLOSE, MIN, MAX, GREY = 0xFF5F57, 0xFFBD2E, 0x28C840, 0x5A5A5E
DOT_RGB = {CLOSE: "close", MIN: "min", MAX: "max", GREY: "grey"}


def read_ppm(path):
    """(w, h, bytes) from a QEMU screendump P6 file.

    Decoded straight to bytes rather than through shot_probe's PNG path: this
    scans every capture of a run, and a pure-Python PNG inflate of 1280x800 per
    frame costs seconds each.
    """
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        return None
    # Header: P6 <w> <h> <maxval>, whitespace separated, '#' comments allowed.
    pos, fields = 2, []
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1                                   # single whitespace after maxval
    w, h, _ = fields
    return w, h, data[pos:pos + w * h * 3]


def lights(w, h, px, y0, y1):
    """Distinct light centre positions found in rows [y0, y1].

    Returns [(centre_x, centre_y, name)] -- a light is a run of >= 6 same-coloured
    pixels in a row, and its centre is the middle of the widest such run per
    contiguous group, so a 13px circle reads as one light rather than thirteen.
    """
    found = []
    for y in range(max(0, y0), min(h, y1)):
        row = y * w * 3
        runs = []
        start = None
        for x in range(w):
            o = row + x * 3
            c = (px[o] << 16) | (px[o + 1] << 8) | px[o + 2]
            if c in DOT_RGB:
                if start is None or x != start[1] + 1 or c != start[2]:
                    if start is not None:
                        runs.append(start)
                    start = [x, x, c]
                else:
                    start[1] = x
            elif start is not None:
                runs.append(start)
                start = None
        if start is not None:
            runs.append(start)
        # group adjacent same-colour runs into one light (the circle widens
        # towards its middle row and narrows again at the top and bottom)
        i = 0
        while i < len(runs):
            j, gx0, gx1, col = i, runs[i][0], runs[i][1], runs[i][2]
            while j + 1 < len(runs) and runs[j + 1][2] == col and \
                    runs[j + 1][0] - runs[j - 1][1] <= 3:
                j += 1
                gx0 = min(gx0, runs[j][0])
                gx1 = max(gx1, runs[j][1])
            if gx1 - gx0 + 1 >= 6:
                found.append(((gx0 + gx1) // 2, y, DOT_RGB[col]))
            i = j + 1
    # one entry per (x, name) -- keep the row where it was widest
    best = {}
    for cx, y, name in found:
        k = (cx, name)
        best[k] = best.get(k, 0) + 1
    return sorted((cx, name) for (cx, name), _ in best.items())


def group_triples(spots):
    """Collapse light positions into (centre_x, names) triples left to right."""
    if not spots:
        return []
    by_x = sorted(spots)
    out, i = [], 0
    while i < len(by_x):
        j = i
        while j + 1 < len(by_x) and by_x[j + 1][0] - by_x[j][0] <= PITCH + 8:
            j += 1
        if j - i >= 2:
            out.append((by_x[i][0], [n for _, n in by_x[i:j + 1]]))
        i = j + 1
    return out


def parse_rects(serial):
    """(family, x, y, w, h, title) for every window the compositor reported."""
    import re
    prs = re.compile(r"PRS pos xid=(\d+) '([^']*)' @(-?\d+),(-?\d+) (\d+)x(\d+)")
    qt = re.compile(r"tile SYNC ws=\d+ id=(\d+) n=\d+ "
                    r"'@(-?\d+),(-?\d+) (\d+)x(\d+)' title='([^']*)'")
    latest = {}
    with open(serial, errors="replace") as f:
        for line in f:
            m = prs.search(line)
            if m:
                g = m.groups()
                latest["x11:" + g[0]] = ("x11", int(g[2]), int(g[3]),
                                         int(g[4]), int(g[5]), g[1])
            m = qt.search(line)
            if m:
                g = m.groups()
                latest["qt:" + g[0]] = ("qt", int(g[1]), int(g[2]),
                                        int(g[3]), int(g[4]), g[5])
    return list(latest.values())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", action="append", default=[],
                    help="key combo in QEMU qcodes, '+' separated; repeatable")
    ap.add_argument("--at", type=float, default=40.0)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--out", default="/tmp/codeos-scan")
    args = ap.parse_args()
    serial = args.out + "-serial.log"

    import shot

    if os.path.exists(serial):
        os.unlink(serial)
    proc, q = shot.launch(shot.qemu_argv(mem="4G", serial=serial))
    try:
        time.sleep(args.at)
        for combo in args.key:
            q.cmd("send-key", keys=[{"type": "qcode", "data": c}
                                    for c in combo.split("+")])
            time.sleep(3.0)
        print(f"scanning {args.frames} frames, one every {args.interval:.1f}s")
        stats = {"composed": 0, "bare": 0}
        for i in range(args.frames):
            q.screendump(shot.PPM)
            time.sleep(0.35)
            img = read_ppm(shot.PPM)
            if img is None:
                print(f"  {i + 1:>3}: undecodable frame")
                continue
            w, h, px = img
            # Only the rows a title band can occupy, to keep this cheap enough
            # to run every second of a minute-long run.
            spots = lights(w, h, px, BAR_H, min(h, 200))
            if not spots:
                stats["bare"] += 1
                print(f"  {i + 1:>3}: no lights in rows {BAR_H}..200 "
                      f"-> not composited")
                continue
            stats["composed"] += 1
            for cx, names in group_triples(spots):
                print(f"  {i + 1:>3}: triple at x={cx:<5} {'/'.join(names)}")
            shot.ppm_to_png(shot.PPM, f"{args.out}-{i + 1:03d}.png")
    finally:
        shot.shutdown(proc)

    print(f"\n{stats['composed']} of {args.frames} frames had lights; "
          f"{stats['bare']} were bare")
    print("window rects the compositor reported:")
    for fam, x, y, ww, hh, title in parse_rects(serial):
        macos = x + WIN_SH + LEFT_INSET
        cosmic = x + ww - WIN_SH - 18
        print(f"  {fam:>3} {title!r:>14} @({x},{y}) {ww}x{hh}  band row "
              f"{y + WIN_SH + (WIN_TB - 12) // 2 + WIN_SH}  lights wanted at "
              f"x={macos},{macos + 20},{macos + 40} (old: x={cosmic - 40},"
              f"{cosmic - 20},{cosmic})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
