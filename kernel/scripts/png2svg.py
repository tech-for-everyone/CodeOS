#!/usr/bin/env python3
"""Convert PNG icons to SVG by embedding as base64 image data."""
import base64, os, re, struct, sys, zlib

def read_png_info(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return None, None
    pos = 8
    while pos + 8 <= len(data):
        length = struct.unpack_from('>I', data, pos)[0]
        chunk = data[pos+4:pos+8]
        if chunk == b'IHDR':
            w, h = struct.unpack_from('>II', data, pos+8)
            return w, h
        pos += 12 + length
    return None, None

def convert(png_path, svg_path):
    with open(png_path, 'rb') as f:
        png_data = f.read()
    w, h = read_png_info(png_data)
    if not w:
        print(f"  SKIP {png_path}: not a valid PNG")
        return False
    b64 = base64.b64encode(png_data).decode('ascii')
    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
     width="{w}" height="{h}" viewBox="0 0 {w} {h}">
  <image width="{w}" height="{h}" xlink:href="data:image/png;base64,{b64}"/>
</svg>
'''
    with open(svg_path, 'w') as f:
        f.write(svg)
    print(f"  {os.path.basename(png_path)} -> {os.path.basename(svg_path)}  ({w}x{h}, {len(svg)} bytes)")
    return True

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    png_dir = os.path.join(script_dir, '..', 'png')
    svg_dir = os.path.join(script_dir, '..', 'svg')

    if len(sys.argv) > 1:
        png_dir = sys.argv[1]
    if len(sys.argv) > 2:
        svg_dir = sys.argv[2]

    if not os.path.isdir(png_dir):
        print(f"PNG directory not found: {png_dir}")
        print("Usage: python3 png2svg.py [png_dir] [svg_dir]")
        sys.exit(1)

    os.makedirs(svg_dir, exist_ok=True)

    pngs = sorted(f for f in os.listdir(png_dir) if f.lower().endswith('.png'))
    if not pngs:
        print(f"No PNG files found in {png_dir}")
        sys.exit(1)

    print(f"Converting {len(pngs)} PNG(s) from {png_dir} -> {svg_dir}")
    ok = 0
    for name in pngs:
        if convert(os.path.join(png_dir, name), os.path.join(svg_dir, name.replace('.png', '.svg'))):
            ok += 1
    print(f"Done: {ok}/{len(pngs)} converted")

if __name__ == '__main__':
    main()
