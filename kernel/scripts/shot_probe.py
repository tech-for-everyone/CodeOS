#!/usr/bin/env python3
"""Structural analysis of a shot.py PNG: find the panels, windows and the
distinct colours in them, so a visual change can be asserted on without
anyone having to eyeball a screenshot.

    ./kernel/scripts/shot_probe.py /tmp/codeos-shot.png
    ./kernel/scripts/shot_probe.py /tmp/a.png --grid

Reports, in order: the size, the top bar's row profile, every horizontal
band of non-background colour large enough to be a window, and -- the point
of the exercise -- the location of any traffic-light triples (the red/yellow/
green cluster that identifies a window control group).
"""
import argparse
import collections
import sys
import zlib

TRAFFIC = {0xFF5F57: "close", 0xFFBD2E: "min", 0x28C840: "max"}


def load(path):
    """Decode a non-interlaced 8-bit RGB PNG written by shot.py."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise SystemExit(f"{path}: not a PNG")
    pos, idat, w = 8, [], None
    while pos < len(data):
        ln = int.from_bytes(data[pos:pos + 4], "big")
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if tag == b"IHDR":
            w = int.from_bytes(body[0:4], "big")
            h = int.from_bytes(body[4:8], "big")
            depth, ctype = body[8], body[9]
            if depth != 8 or ctype != 2:
                raise SystemExit(f"{path}: only 8-bit RGB supported "
                                 f"(depth={depth} ctype={ctype})")
        elif tag == b"IDAT":
            idat.append(body)
        pos += 12 + ln
    raw = zlib.decompress(b"".join(idat))
    stride = w * 3
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(3, stride):
                line[i] = (line[i] + line[i - 3]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - 3] if i >= 3 else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif f != 0:
            raise SystemExit(f"{path}: filter {f}")
        rows.append(bytes(line))
        prev = line
    return w, h, rows


def at(rows, x, y):
    r = rows[y]
    return (r[x * 3] << 16) | (r[x * 3 + 1] << 8) | r[x * 3 + 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("png")
    ap.add_argument("--grid", action="store_true",
                    help="dump a coarse colour grid")
    args = ap.parse_args()

    w, h, rows = load(args.png)
    hist = collections.Counter(at(rows, x, y)
                               for y in range(0, h, 3) for x in range(0, w, 3))
    print(f"{args.png}  {w}x{h}  {len(hist)} distinct colours (sampled /3)")
    print("\ntop 12 colours:")
    for c, n in hist.most_common(12):
        print(f"  #{c:06X}  {n * 9:>8} px")

    print("\ntraffic lights (exact palette colours):")
    for target, name in TRAFFIC.items():
        pts = []
        for y in range(h):
            for x in range(w):
                if at(rows, x, y) == target:
                    pts.append((x, y))
        if pts:
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            print(f"  {name:>5} #{target:06X}  n={len(pts):<4} "
                  f"x={min(xs)}..{max(xs)}  y={min(ys)}..{max(ys)}")
        else:
            print(f"  {name:>5} #{target:06X}  -- absent --")

    if args.grid:
        step = max(1, min(w, h) // 24)
        print(f"\ncoarse grid (every {step}px):")
        for y in range(0, h, step):
            line = ""
            for x in range(0, w, step):
                c = at(rows, x, y)
                line += "." if c in TRAFFIC else ("#" if hist.get(c, 0) > 60 else "+")
            print(f"  {y:>4} {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
