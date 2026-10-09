#!/usr/bin/env python3
"""CodeOS 2.5D Icon Generator
Renders flat SVG icons into 2.5D RGBA icons with perspective, shadows, and gradients.

Usage:
  python3 scripts/gen_25d_icons.py
"""

import os
import sys
import math
import re
import xml.etree.ElementTree as ET

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

DEFAULT_INPUT = os.path.join(PROJECT_ROOT, "pkgs", "core", "icons", "src")
DEFAULT_OUTPUT = os.path.join(PROJECT_ROOT, "pkgs", "core", "icons", "src")

SIZES = {"44": 44, "64": 64, "128": 128}

PERSPECTIVE_TILT = 0.93
SHADOW_OFFSET_Y_FRAC = 0.03
SHADOW_BLUR_FRAC = 0.04
SHADOW_ALPHA = 90
HIGHLIGHT_ALPHA = 35
GRADIENT_BOTTOM_TINT = 0.82
CORNER_RADIUS_FRAC = 0.14


def parse_color(s):
    s = s.strip().lstrip("#")
    if len(s) == 3:
        return (int(s[0]*2,16), int(s[1]*2,16), int(s[2]*2,16))
    if len(s) == 6:
        return (int(s[0:2],16), int(s[1:3],16), int(s[2:4],16))
    return (255, 255, 255)


def tokenize_path(d):
    """Properly tokenize SVG path data."""
    tokens = re.findall(r'[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d)
    return tokens


def parse_path(d):
    """Parse SVG path 'd' attribute into list of (command, [args]) tuples."""
    tokens = tokenize_path(d)
    commands = []
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if tok.isalpha():
            cmd = tok
            args = []
            i += 1
            while i < len(tokens) and not tokens[i].isalpha():
                args.append(float(tokens[i]))
                i += 1
            commands.append((cmd, args))
        else:
            i += 1
    return commands


def rasterize_svg(svg_path, size):
    """Rasterize an SVG to an RGBA pixel buffer using scanline fill."""
    try:
        tree = ET.parse(svg_path)
        root = tree.getroot()
    except Exception as e:
        print(f"  WARNING: Failed to parse {svg_path}: {e}")
        return None

    ns = {}
    for k, v in root.attrib.items():
        if 'viewBox' in k:
            break

    vb = root.get('viewBox', '0 0 64 64').split()
    vb_x, vb_y, vb_w, vb_h = float(vb[0]), float(vb[1]), float(vb[2]), float(vb[3])
    sx = size / vb_w
    sy = size / vb_h

    pixels = [[0]*size for _ in range(size)]  # single channel (alpha mask)

    def tx(x): return (x - vb_x) * sx
    def ty(y): return (y - vb_y) * sy

    def fill_scanline(pts, val=255):
        """Fill a polygon using scanline."""
        if len(pts) < 3:
            return
        min_y = max(0, int(min(p[1] for p in pts)))
        max_y = min(size - 1, int(max(p[1] for p in pts)))
        for y in range(min_y, max_y + 1):
            xs = []
            n = len(pts)
            for i in range(n):
                x1, y1 = pts[i]
                x2, y2 = pts[(i+1) % n]
                if (y1 <= y < y2) or (y2 <= y < y1):
                    t = (y - y1) / (y2 - y1) if y2 != y1 else 0
                    xs.append(x1 + t * (x2 - x1))
            xs.sort()
            for j in range(0, len(xs) - 1, 2):
                x0 = max(0, int(xs[j]))
                x1 = min(size - 1, int(xs[j + 1]))
                for x in range(x0, x1 + 1):
                    pixels[y][x] = val

    def process_path(path_d, color, alpha):
        """Parse and rasterize a single path element."""
        commands = parse_path(path_d)
        pts = []
        cur = [0.0, 0.0]
        start = [0.0, 0.0]

        for cmd, args in commands:
            c = cmd.lower()
            is_rel = cmd.islower()

            if c == 'm':
                if pts and len(pts) >= 3:
                    fill_scanline(pts, alpha)
                    pts = []
                j = 0
                first = True
                while j + 1 < len(args):
                    dx, dy = args[j], args[j+1]
                    if is_rel:
                        cur[0] += dx; cur[1] += dy
                    else:
                        cur[0] = dx; cur[1] = dy
                    if first:
                        start = cur[:]
                        first = False
                    pts.append([tx(cur[0]), ty(cur[1])])
                    j += 2
            elif c == 'l':
                j = 0
                while j + 1 < len(args):
                    if is_rel:
                        cur[0] += args[j]; cur[1] += args[j+1]
                    else:
                        cur[0] = args[j]; cur[1] = args[j+1]
                    pts.append([tx(cur[0]), ty(cur[1])])
                    j += 2
            elif c == 'h':
                for v in args:
                    if is_rel:
                        cur[0] += v
                    else:
                        cur[0] = v
                    pts.append([tx(cur[0]), ty(cur[1])])
            elif c == 'v':
                for v in args:
                    if is_rel:
                        cur[1] += v
                    else:
                        cur[1] = v
                    pts.append([tx(cur[0]), ty(cur[1])])
            elif c == 'c':
                # Cubic bezier - approximate with line segments
                j = 0
                while j + 5 < len(args):
                    cx1, cy1 = args[j], args[j+1]
                    cx2, cy2 = args[j+2], args[j+3]
                    ex, ey = args[j+4], args[j+5]
                    if is_rel:
                        cx1 += cur[0]; cy1 += cur[1]
                        cx2 += cur[0]; cy2 += cur[1]
                        ex += cur[0]; ey += cur[1]
                    # Flatten bezier
                    steps = 8
                    for t_i in range(1, steps + 1):
                        t = t_i / steps
                        mt = 1 - t
                        bx = mt*mt*mt*cur[0] + 3*mt*mt*t*cx1 + 3*mt*t*t*cx2 + t*t*t*ex
                        by = mt*mt*mt*cur[1] + 3*mt*mt*t*cy1 + 3*mt*t*t*cy2 + t*t*t*ey
                        pts.append([tx(bx), ty(by)])
                    cur[0] = ex; cur[1] = ey
                    j += 6
            elif c == 'z':
                if pts and len(pts) >= 3:
                    fill_scanline(pts, alpha)
                    pts = []
                cur = start[:]

        if pts and len(pts) >= 3:
            fill_scanline(pts, alpha)

    def process_elem(elem):
        tag = elem.tag
        if '}' in tag:
            tag = tag.split('}')[1]

        # Get fill color
        fill_str = elem.get('fill', '')
        if not fill_str:
            style = elem.get('style', '')
            m = re.search(r'fill:\s*([^;]+)', style)
            if m:
                fill_str = m.group(1)
        if not fill_str or fill_str == 'none':
            return
        color = parse_color(fill_str)

        opacity = 1.0
        if 'opacity' in elem.attrib:
            opacity = float(elem.attrib['opacity'])
        alpha = int(opacity * 255)

        if tag == 'rect':
            x = tx(float(elem.get('x', '0')))
            y = ty(float(elem.get('y', '0')))
            w = float(elem.get('width', '0')) * sx
            h = float(elem.get('height', '0')) * sy
            pts = [[x, y], [x+w, y], [x+w, y+h], [x, y+h]]
            fill_scanline(pts, alpha)

        elif tag == 'circle':
            cx = tx(float(elem.get('cx', '0')))
            cy = ty(float(elem.get('cy', '0')))
            r = float(elem.get('r', '0')) * sx
            # Fill as polygon
            n = 32
            pts = [[cx + r * math.cos(2*math.pi*i/n), cy + r * math.sin(2*math.pi*i/n)] for i in range(n)]
            fill_scanline(pts, alpha)

        elif tag == 'ellipse':
            cx = tx(float(elem.get('cx', '0')))
            cy = ty(float(elem.get('cy', '0')))
            rx = float(elem.get('rx', '0')) * sx
            ry = float(elem.get('ry', '0')) * sy
            n = 32
            pts = [[cx + rx * math.cos(2*math.pi*i/n), cy + ry * math.sin(2*math.pi*i/n)] for i in range(n)]
            fill_scanline(pts, alpha)

        elif tag == 'path':
            d = elem.get('d', '')
            if d:
                process_path(d, color, alpha)

        # Recurse into children
        for child in elem:
            process_elem(child)

    # Process all elements
    process_elem(root)

    # Convert alpha mask to RGBA pixels
    result = [[(0,0,0,0) for _ in range(size)] for _ in range(size)]
    for y in range(size):
        for x in range(size):
            if pixels[y][x] > 0:
                result[y][x] = (255, 255, 255, pixels[y][x])

    return result


def apply_25d(pixels, size):
    """Apply 2.5D effect: perspective, shadow, gradient, highlight."""
    result = [[(0,0,0,0) for _ in range(size)] for _ in range(size)]

    shadow_y = max(1, int(SHADOW_OFFSET_Y_FRAC * size))
    shadow_b = max(1, int(SHADOW_BLUR_FRAC * size))
    cx = size / 2.0
    corner_r = max(1, int(CORNER_RADIUS_FRAC * size))

    # Step 1: Shadow
    for y in range(size):
        for x in range(size):
            if pixels[y][x][3] == 0:
                continue
            sy = y + shadow_y
            if sy >= size:
                continue
            # Box blur for shadow alpha
            total = 0
            count = 0
            for dy in range(-shadow_b, shadow_b+1):
                ty = sy + dy
                if ty < 0 or ty >= size:
                    continue
                for dx in range(-shadow_b, shadow_b+1):
                    tx2 = x + dx
                    if 0 <= tx2 < size and pixels[ty][tx2][3] > 0:
                        total += 1
                        count += 1
            if count > 0:
                a = min(255, int(SHADOW_ALPHA * count / max(1, shadow_b * shadow_b)))
                ea = result[sy][x][3]
                result[sy][x] = (0, 0, 0, max(ea, a))

    # Step 2: Apply perspective + gradient to icon
    for y in range(size):
        for x in range(size):
            r, g, b, a = pixels[y][x]
            if a == 0:
                continue

            # Perspective
            ny = y / size
            tilt = PERSPECTIVE_TILT + (1.0 - PERSPECTIVE_TILT) * ny
            rx = (x - cx) * tilt + cx
            ix = int(rx)
            if ix < 0 or ix >= size:
                continue

            # Gradient
            brightness = 1.0 - (1.0 - GRADIENT_BOTTOM_TINT) * ny
            r = int(r * brightness)
            g = int(g * brightness)
            b = int(b * brightness)

            # Top highlight
            if ny < 0.12:
                h = int(HIGHLIGHT_ALPHA * (1.0 - ny / 0.12))
                r = min(255, r + h)
                g = min(255, g + h)
                b = min(255, b + h)

            # Round corners
            in_corner = False
            for (ccx, ccy) in [(corner_r, corner_r), (size-1-corner_r, corner_r),
                               (corner_r, size-1-corner_r), (size-1-corner_r, size-1-corner_r)]:
                dx = x - ccx
                dy = y - ccy
                dist = math.sqrt(dx*dx + dy*dy)
                if (abs(dx) > corner_r and abs(dy) > corner_r) and dist > corner_r + 2:
                    in_corner = True
                    break
            if in_corner:
                a = max(0, a - 200)  # fade corners

            # Composite over shadow
            if a > 0:
                sa = a / 255.0
                da = result[y][ix][3] / 255.0
                oa = sa + da * (1 - sa)
                if oa > 0:
                    or_ = int((r * sa + result[y][ix][0] * da * (1-sa)) / oa)
                    og = int((g * sa + result[y][ix][1] * da * (1-sa)) / oa)
                    ob = int((b * sa + result[y][ix][2] * da * (1-sa)) / oa)
                    result[y][ix] = (or_, og, ob, min(255, int(oa * 255)))

    return result


def write_rgba(path, pixels, size):
    data = bytearray()
    for y in range(size):
        for x in range(size):
            r, g, b, a = pixels[y][x]
            data.extend([r, g, b, a])
    with open(path, 'wb') as f:
        f.write(data)


def main():
    input_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_INPUT
    output_dir = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUTPUT

    os.makedirs(output_dir, exist_ok=True)

    svgs = sorted([f for f in os.listdir(input_dir) if f.endswith('.svg')])

    if not svgs:
        alt = os.path.join(PROJECT_ROOT, "svg")
        if os.path.isdir(alt):
            svgs = [os.path.join(alt, f) for f in os.listdir(alt) if f.endswith('.svg')]

    if not svgs:
        print("ERROR: No SVG files found")
        sys.exit(1)

    print(f"Generating 2.5D icons from {len(svgs)} SVGs...")

    for svg in svgs:
        svg_path = svg if os.path.isabs(svg) else os.path.join(input_dir, svg)
        if not os.path.isfile(svg_path):
            continue
        basename = os.path.splitext(os.path.basename(svg_path))[0]

        for suffix, size in SIZES.items():
            print(f"  {basename} @ {size}x{size}...", end="", flush=True)
            pixels = rasterize_svg(svg_path, size)
            if pixels is None:
                print(" FAILED")
                continue
            pixels = apply_25d(pixels, size)
            if suffix == "128":
                out = os.path.join(output_dir, f"{basename}.rgba")
            else:
                out = os.path.join(output_dir, f"{basename}_{suffix}.rgba")
            write_rgba(out, pixels, size)
            print(f" OK ({os.path.basename(out)})")

    print("Done!")


if __name__ == "__main__":
    main()
