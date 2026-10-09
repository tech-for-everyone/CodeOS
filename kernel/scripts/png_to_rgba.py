#!/usr/bin/env python3
"""Convert PNG to raw RGBA pixel data using Python's built-in zlib."""
import sys, struct, zlib

def png_to_rgba(png_data, size=None):
    w = h = bit_depth = color_type = 0
    palette = []
    trns = {}
    idat_data = bytearray()
    interlace = 0

    sig = png_data[:8]
    if sig != b'\x89PNG\r\n\x1a\n':
        raise ValueError("Not a valid PNG")

    pos = 8
    while pos + 8 <= len(png_data):
        length = struct.unpack_from('>I', png_data, pos)[0]
        chunk_type = png_data[pos+4:pos+8]
        chunk_data = png_data[pos+8:pos+8+length]
        pos += 12 + length

        if chunk_type == b'IHDR':
            w, h, bit_depth, color_type = struct.unpack_from('>IIBB', chunk_data)
            # compression=chunk_data[8], filter=chunk_data[9], interlace=chunk_data[10]
            interlace = chunk_data[10]
        elif chunk_type == b'PLTE':
            palette = [(chunk_data[i], chunk_data[i+1], chunk_data[i+2]) for i in range(0, len(chunk_data), 3)]
        elif chunk_type == b'tRNS':
            if color_type == 0:
                trns['gray'] = struct.unpack_from('>H', chunk_data)[0]
            elif color_type == 2:
                trns['rgb'] = (struct.unpack_from('>H', chunk_data)[0], struct.unpack_from('>H', chunk_data, 2)[0], struct.unpack_from('>H', chunk_data, 4)[0])
            elif color_type == 3:
                for i in range(min(len(chunk_data), len(palette))):
                    trns[i] = chunk_data[i]
        elif chunk_type == b'IDAT':
            idat_data.extend(chunk_data)
        elif chunk_type == b'IEND':
            break

    raw = zlib.decompress(bytes(idat_data))

    def samples_per_pixel():
        if color_type == 0: return 1
        if color_type == 2: return 3
        if color_type == 3: return 1
        if color_type == 4: return 2
        if color_type == 6: return 4
        return 0

    def bits_per_sample():
        return bit_depth

    spp = samples_per_pixel()
    bps = bits_per_sample()
    bpp = spp * bps  # bits per pixel
    stride = (w * bpp + 7) // 8

    def paeth(a, b, c):
        p = a + b - c
        pa = abs(p - a)
        pb = abs(p - b)
        pc = abs(p - c)
        if pa <= pb and pa <= pc: return a
        if pb <= pc: return b
        return c

    def recon_a(r, c, bpp_bytes):
        return r[c - bpp_bytes] if c >= bpp_bytes else 0

    def recon_b(r, c):
        return r[c] if len(r) > 0 else 0

    def recon_c(r, c, bpp_bytes):
        return r[c - bpp_bytes] if c >= bpp_bytes and len(r) > 0 else 0

    bpp_bytes = (bpp + 7) // 8
    prev = bytearray()
    pixels = bytearray()

    if interlace == 0:
        # Non-interlaced
        scanline_len = stride + 1  # filter byte + data
        for y in range(h):
            offset = y * scanline_len
            filter_type = raw[offset]
            scanline = raw[offset + 1:offset + scanline_len]
            recon = bytearray(scanline)
            if filter_type == 1:  # Sub
                for c in range(bpp_bytes, len(recon)):
                    recon[c] = (recon[c] + recon_a(recon, c, bpp_bytes)) & 0xFF
            elif filter_type == 2:  # Up
                for c in range(len(recon)):
                    recon[c] = (recon[c] + recon_b(prev, c)) & 0xFF
            elif filter_type == 3:  # Average
                for c in range(len(recon)):
                    recon[c] = (recon[c] + (recon_a(recon, c, bpp_bytes) + recon_b(prev, c)) // 2) & 0xFF
            elif filter_type == 4:  # Paeth
                for c in range(len(recon)):
                    recon[c] = (recon[c] + paeth(recon_a(recon, c, bpp_bytes), recon_b(prev, c), recon_c(prev, c, bpp_bytes))) & 0xFF
            prev = recon
            pixels.extend(recon)
    else:
        # Adam7 interlacing
        pass  # skip interlace for now, fall through

    # Convert to RGBA pixels
    rgba = bytearray()
    bps = bit_depth

    if color_type == 6:  # RGBA
        for y in range(h):
            row_start = y * stride
            for x in range(w):
                pix_start = row_start + x * 4
                rgba.extend(pixels[pix_start:pix_start+4])
    elif color_type == 2:  # RGB
        for y in range(h):
            row_start = y * stride
            for x in range(w):
                pix_start = row_start + x * 3
                rgba.extend(pixels[pix_start:pix_start+3])
                rgba.append(0xFF)
    elif color_type == 0:  # Grayscale
        for y in range(h):
            row_start = y * stride
            for x in range(w):
                if bps == 8:
                    v = pixels[row_start + x]
                elif bps == 16:
                    v = pixels[row_start + x * 2]
                else:
                    v = 0
                rgba.extend([v, v, v, 0xFF])
    elif color_type == 4:  # Grayscale + Alpha
        for y in range(h):
            row_start = y * stride
            for x in range(w):
                if bps == 8:
                    g = pixels[row_start + x * 2]
                    a = pixels[row_start + x * 2 + 1]
                else:
                    g = a = 0
                rgba.extend([g, g, g, a])
    elif color_type == 3:  # Indexed
        for y in range(h):
            row_start = y * stride
            for x in range(w):
                idx = pixels[row_start + x]
                if idx < len(palette):
                    r, g, b = palette[idx]
                else:
                    r = g = b = 0
                a = trns.get(idx, 0xFF)
                rgba.extend([r, g, b, a])

    # Resize if requested
    if size and size != w:
        from math import floor
        new_w = new_h = size
        resized = bytearray()
        for y in range(new_h):
            src_y = min(floor(y * h / new_h), h - 1)
            for x in range(new_w):
                src_x = min(floor(x * w / new_w), w - 1)
                idx = (src_y * w + src_x) * 4
                resized.extend(rgba[idx:idx+4])
        return bytes(resized)

    return bytes(rgba)

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 png_to_rgba.py input.png output.rgba [size]")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else None

    with open(input_file, 'rb') as f:
        png = f.read()

    rgba = png_to_rgba(png, size)
    with open(output_file, 'wb') as f:
        f.write(rgba)

    print(f"Converted {input_file} -> {output_file} ({len(rgba)} bytes)")

if __name__ == '__main__':
    main()
