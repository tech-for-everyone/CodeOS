#!/usr/bin/env python3
"""Generate codec fixtures with independently-computed expected pixels.

The point of computing the expectations here, in a completely separate
implementation, is that they cannot drift with the C decoder. If expected.h were
produced by running the decoder, every test would pass by construction and the
suite would be worthless. Here, Python decides what the pixels should be and the
C decoder has to agree.

Everything is written into the output directory given as argv[1]:

  *.png *.bmp *.wav   fixture files
  expected.h          expected 0xAARRGGBB pixels / int16 samples
"""

import os
import struct
import sys
import zlib

# ── PNG writer ───────────────────────────────────────────────────────────

def chunk(tag, data):
    return (struct.pack('>I', len(data)) + tag + data +
            struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def filter_row(ft, cur, prev, bpp):
    out = bytearray(len(cur))
    for i, x in enumerate(cur):
        a = cur[i - bpp] if i >= bpp else 0
        b = prev[i] if prev is not None else 0
        c = prev[i - bpp] if (prev is not None and i >= bpp) else 0
        if ft == 0:
            v = x
        elif ft == 1:
            v = (x - a) & 0xFF
        elif ft == 2:
            v = (x - b) & 0xFF
        elif ft == 3:
            v = (x - ((a + b) // 2)) & 0xFF
        elif ft == 4:
            v = (x - paeth(a, b, c)) & 0xFF
        else:
            raise ValueError(ft)
        out[i] = v
    return bytes(out)


def png(width, height, depth, colortype, rows, palette=None, trns=None,
        interlace=0, filters=None, level=6, strategy=zlib.Z_DEFAULT_STRATEGY):
    """rows: list of raw (unfiltered) sample bytes, one per scanline."""
    bpp_bits = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colortype] * depth
    bpp = max(1, (bpp_bits + 7) // 8)

    body = bytearray()
    prev = None
    for y, raw in enumerate(rows):
        ft = 0 if filters is None else filters[y]
        body.append(ft)
        body += filter_row(ft, raw, prev, bpp)
        prev = raw

    co = zlib.compressobj(level, zlib.DEFLATED, 15, 9, strategy)
    idat = co.compress(bytes(body)) + co.flush()

    ihdr = struct.pack('>IIBBBBB', width, height, depth, colortype, 0, 0, interlace)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr)
    if palette:
        out += chunk(b'PLTE', bytes(palette))
    if trns is not None:
        out += chunk(b'tRNS', bytes(trns))
    out += chunk(b'IDAT', idat) + chunk(b'IEND', b'')
    return out


def argb(a, r, g, b):
    """Pack in the project's order: alpha in the TOP byte."""
    return (a << 24) | (r << 16) | (g << 8) | b


# ── BMP writer ───────────────────────────────────────────────────────────

def bmp(width, height, rows_bgr, bpp=24, top_down=False, palette=None):
    """rows_bgr: top-to-bottom list of bytes, each pixel in B,G,R[,A] order."""
    if top_down:
        height = -height
    row_bytes = width * (bpp // 8)
    pad = (-row_bytes) % 4
    stride = row_bytes + pad

    pixels = bytearray()
    for row in rows_bgr:
        pixels += row + b'\x00' * pad
    if not top_down:
        pixels = b''.join(rows_bgr[i] + b'\x00' * pad
                          for i in range(len(rows_bgr) - 1, -1, -1))

    # palette arrives already packed as 4 bytes per entry (B,G,R,reserved), so
    # its length is the on-disk palette size. Multiplying by 4 again pushed
    # data_off past the header and every 8-bit fixture came out truncated.
    off = 14 + 40 + (len(palette) if palette else 0)
    info = struct.pack('<IiiHHIIiiII', 40, width, height, 1, bpp, 0,
                       len(pixels), 2835, 2835,
                       len(palette) // 4 if palette else 0, 0)
    head = b'BM' + struct.pack('<IHHI', off + len(pixels), 0, 0, off)
    pal = b''
    if palette:
        for i in range(0, len(palette), 4):
            pal += bytes([palette[i + 2], palette[i + 1], palette[i + 0], palette[i + 3]])
    return head + info + pal + bytes(pixels)


# ── WAV writer ───────────────────────────────────────────────────────────

def wav(samples, rate, channels=1, bits=16, fmt=1, extra_chunks=()):
    """samples: flat list of per-channel ints (or floats when fmt == 3)."""
    if fmt == 3:
        body = b''.join(struct.pack('<f', float(s)) for s in samples)
    elif bits == 8:
        body = bytes((s + 128) & 0xFF for s in samples)
    elif bits == 16:
        body = b''.join(struct.pack('<h', s) for s in samples)
    elif bits == 24:
        body = b''.join(struct.pack('<i', s)[:3] for s in samples)
    elif bits == 32:
        body = b''.join(struct.pack('<i', s) for s in samples)
    else:
        raise ValueError(bits)

    block = channels * (bits // 8)
    fmtchunk = struct.pack('<HHIIHH', fmt, channels, rate,
                           rate * block, block, bits)

    payload = b'fmt ' + struct.pack('<I', len(fmtchunk)) + fmtchunk
    for tag, body_ in extra_chunks:
        chunk_bytes = tag + struct.pack('<I', len(body_)) + body_
        if len(body_) % 2:
            chunk_bytes += b'\x00'          # RIFF word alignment
        payload += chunk_bytes
    payload += b'data' + struct.pack('<I', len(body)) + body
    return b'RIFF' + struct.pack('<I', len(payload) + 4) + b'WAVE' + payload


# ── Fixture set ──────────────────────────────────────────────────────────

def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    exp = {}          # name -> python list of ints (or nested list)

    def write(name, data):
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(data)
        return data

    # 1. PNG truecolour 8-bit, 4x2. Deliberately includes pure red/green/blue
    #    and a ramp so a channel swap cannot pass unnoticed.
    rows = [bytes([255, 0, 0,   0, 255, 0,   0, 0, 255,   255, 255, 255]),
            bytes([0, 0, 0,     18, 52, 86,   128, 128, 128, 1, 2, 3])]
    write('rgb8.png', png(4, 2, 8, 2, rows))
    exp['rgb8'] = [argb(255, 255, 0, 0), argb(255, 0, 255, 0),
                   argb(255, 0, 0, 255), argb(255, 255, 255, 255),
                   argb(255, 0, 0, 0), argb(255, 18, 52, 86),
                   argb(255, 128, 128, 128), argb(255, 1, 2, 3)]

    # 2. Eight scanlines so all five filter types get exercised, and so Up/Average/
    #    Paeth have a real `prev` row rather than the row-0 None case only. A
    #    two-row image cannot do this: it can carry at most two filter types.
    filt_rows = [bytes([(x * 60 + y * 25) % 256 for x in range(4) for _ in range(3)])
                 for y in range(8)]
    write('rgb8_filters.png', png(4, 8, 8, 2, filt_rows,
                                   filters=[0, 1, 2, 3, 4, 1, 4, 3]))
    fflat = []
    for y in range(8):
        for x in range(4):
            v = (x * 60 + y * 25) % 256
            fflat.append(argb(255, v, v, v))
    exp['rgb8_filters'] = fflat

    # 3. PNG stored (uncompressed) blocks, to cover DEFLATE block type 0.
    write('rgb8_stored.png', png(4, 2, 8, 2, rows, level=0))
    exp['rgb8_stored'] = exp['rgb8']

    # 4. Larger, so DEFLATE emits dynamic Huffman (block type 2) with a real
    #    code-length table and back-references that overlap.
    big_rows = [bytes([(x * 7 + y * 13) % 256 for x in range(64) for _ in range(3)])
                for y in range(16)]
    write('rgb8_dynamic.png', png(64, 16, 8, 2, big_rows))
    flat = []
    for y in range(16):
        for x in range(64):
            flat.append(argb(255, (x * 7 + y * 13) % 256, (x * 7 + y * 13) % 256,
                             (x * 7 + y * 13) % 256))
    exp['rgb8_dynamic'] = flat

    # 5. RGBA, checking that alpha lands in the TOP byte of the packed word.
    rgba_rows = [bytes([10, 20, 30, 255, 40, 50, 60, 128]),
                 bytes([70, 80, 90, 0,   0, 0, 0, 255])]
    write('rgba8.png', png(2, 2, 8, 6, rgba_rows))
    exp['rgba8'] = [argb(255, 10, 20, 30), argb(128, 40, 50, 60),
                    argb(0, 70, 80, 90), argb(255, 0, 0, 0)]

    # 6. 8-bit grey: tests the grey path and, via depth 1, the bit unpacking.
    grey_rows = [bytes([0, 85, 170, 255]), bytes([255, 170, 85, 0])]
    write('grey8.png', png(4, 1, 8, 0, grey_rows))
    exp['grey8'] = [argb(255, v, v, v) for v in grey_rows[0]]

    grey1_rows = [bytes([0b10100000])]
    write('grey1.png', png(4, 1, 1, 0, grey1_rows))
    exp['grey1'] = [argb(255, 255, 255, 255), argb(255, 0, 0, 0),
                    argb(255, 255, 255, 255), argb(255, 0, 0, 0)]

    # 7. Palette at 8-bit and at 4-bit (two pixels per byte), plus tRNS, so both
    #    the index path and unpack_bits() are covered.
    #
    #    Four colours, not three: the image uses indices 0..3, and a PLTE holding
    #    only three would make index 3 an out-of-range reference. The decoder
    #    has to reject that (see index_range in codec_png.c), so an in-range
    #    palette is what the happy-path fixture should use.
    pal = [255, 0, 0,  0, 255, 0,  0, 0, 255,  255, 255, 255]
    # Alpha per palette index, so the entry positions matter. Index 0 fully
    # transparent, index 3 half transparent, the rest opaque.
    trns = [0, 255, 255, 128, 255, 255, 255, 255, 255, 255, 255, 255]
    write('pal8.png', png(4, 1, 8, 3, [bytes([0, 1, 2, 3])],
                          palette=pal, trns=trns))
    exp['pal8'] = [argb(0, 255, 0, 0), argb(255, 0, 255, 0),
                   argb(255, 0, 0, 255), argb(128, 255, 255, 255)]

    write('pal4.png', png(4, 1, 4, 3, [bytes([0x01, 0x23])],
                          palette=pal, trns=trns))
    exp['pal4'] = exp['pal8']

    # 7b. A palette shorter than the indices the image uses. PLTE says three
    #     entries, the image references index 3. Must be refused as corrupt, not
    #     read past the end of the palette.
    write('pal_short.png', png(4, 1, 8, 3, [bytes([0, 1, 2, 3])],
                               palette=[255, 0, 0,  0, 255, 0,  0, 0, 255],
                               trns=None))
    exp['pal_short'] = None

    # 8. 16-bit samples. Every byte pair differs in BOTH halves, so keeping the
    #    wrong one is visible rather than coincidental: 0x1234 must reduce to
    #    0x12, not 0x34.
    #    Two pixels at 3 channels x 2 bytes = 12 bytes per scanline.
    write('rgb16.png', png(2, 1, 16, 2, [bytes([0x12, 0x34,   0x56, 0x78,
                                                 0x9A, 0xBC,   0xDE, 0xF0,
                                                 0x11, 0x22,   0x33, 0x44])]))
    exp['rgb16'] = [argb(255, 0x12, 0x56, 0x9A), argb(255, 0xDE, 0x11, 0x33)]

    # 8b. 16-bit grey and 16-bit RGBA, so the same high-byte rule is pinned for
    #     the other two colour types that can be 16 bits deep.
    write('grey16.png', png(2, 1, 16, 0, [bytes([0x12, 0x34,   0xAB, 0xCD])]))
    exp['grey16'] = [argb(255, 0x12, 0x12, 0x12), argb(255, 0xAB, 0xAB, 0xAB)]

    write('rgba16.png', png(1, 1, 16, 6, [bytes([0x12, 0x34,   0x56, 0x78,
                                                   0x9A, 0xBC,   0xDE, 0xF0])]))
    exp['rgba16'] = [argb(0xDE, 0x12, 0x56, 0x9A)]

    # 9. Adam7 interlaced: must be refused, not half-rendered.
    write('interlaced.png', png(4, 2, 8, 2, rows, interlace=1))
    exp['interlaced'] = None

    # 10. Oversized dimensions, which must be refused before allocating.
    write('huge.png', png(20000, 20000, 8, 2, [b'', b'']))
    exp['huge'] = None

    # 11. BMP 24-bit. Pixel 0 is pure red in RGB terms, stored as BGR, so this
    #     is the direct test of the channel-order trap.
    bmp_rows = [bytes([0, 0, 255, 0, 255, 0]),      # red, green
                bytes([255, 0, 0, 255, 255, 255])]   # blue, white
    write('bgr24.bmp', bmp(2, 2, bmp_rows, bpp=24))
    exp['bgr24'] = [argb(255, 255, 0, 0), argb(255, 0, 255, 0),
                    argb(255, 0, 0, 255), argb(255, 255, 255, 255)]

    # 12. BMP 32-bit with alpha, and top-down row order. At least one pixel has
    #     non-zero alpha, so the channel is real and the pixel whose alpha is 0
    #     must come out genuinely transparent. This is the case that a
    #     per-pixel "if alpha==0 assume the channel is unused" rule gets wrong.
    bmp32_rows = [bytes([0, 0, 255, 255, 0, 255, 0, 128]),
                  bytes([255, 0, 0, 0, 255, 255, 255, 255])]
    write('bgra32_topdown.bmp', bmp(2, 2, bmp32_rows, bpp=32, top_down=True))
    exp['bgra32_topdown'] = [argb(255, 255, 0, 0), argb(128, 0, 255, 0),
                             argb(0, 0, 0, 255), argb(255, 255, 255, 255)]

    # 12b. 32-bit where every alpha byte is zero: the writer never intended an
    #      alpha channel, so the image must be treated as opaque rather than as
    #      fully transparent. Indistinguishable from 12 at pixel scope, which is
    #      why the decoder disambiguates at image scope.
    write('bgra32_noalpha.bmp',
          bmp(2, 2, [bytes([0, 0, 255, 0,   0, 255, 0, 0]),
                     bytes([255, 0, 0, 0,   255, 255, 255, 0])],
              bpp=32, top_down=True))
    exp['bgra32_noalpha'] = [argb(255, 255, 0, 0), argb(255, 0, 255, 0),
                             argb(255, 0, 0, 255), argb(255, 255, 255, 255)]

    # 13. BMP 8-bit paletted.
    pal8 = []
    for r, g, b in [(255, 0, 0), (0, 255, 0), (0, 0, 255), (9, 9, 9)]:
        pal8 += [b, g, r, 0]
    write('pal8.bmp', bmp(4, 1, [bytes([0, 1, 2, 3])], bpp=8, palette=pal8))
    exp['pal8_bmp'] = [argb(255, 255, 0, 0), argb(255, 0, 255, 0),
                       argb(255, 0, 0, 255), argb(255, 9, 9, 9)]

    # 14. WAV 16-bit mono, exact samples.
    s16 = [0, 32767, -32768, 1000, -1000, 12345]
    write('m16.wav', wav(s16, 8000, 1, 16))
    exp['m16'] = s16

    # 15. WAV 16-bit stereo, anti-phase. Must average to exactly 0.
    write('s16.wav', wav([1000, -1000, 2000, -2000, 300, -300], 44100, 2, 16))
    exp['s16'] = [0, 0, 0]

    # 16. WAV 8-bit unsigned: 0 -> -32768, 128 -> 0, 255 -> +32512.
    write('m8.wav', wav([0, 128, 255], 8000, 1, 8))
    exp['m8'] = [-32768, 0, 32512]

    # 17. WAV 24-bit: 0x123456 -> high 16 bits, kept by an arithmetic shift.
    #     The negative case is the interesting one: -0x123456 >> 16 (well,
    #     >> 8 to reach 16 bits) floors to -0x1235, not -0x1234, because the
    #     shift is arithmetic on a negative value. Computing the expectation
    #     from the positive magnitude and negating it would give -0x1234 and be
    #     wrong by one -- which is exactly the off-by-one a truncation-based
    #     implementation gets.
    write('m24.wav', wav([0x123456, -0x123456], 8000, 1, 24))
    exp['m24'] = [0x1234, -0x1235]

    # 18. WAV float32: 0.5, 1.0, -1.0, 0.0.
    write('f32.wav', wav([0.5, 1.0, -1.0, 0.0], 8000, 1, 32, fmt=3))
    exp['f32'] = [16384, 32767, -32767, 0]

    # 19. WAV with an odd-sized chunk before data, exercising RIFF word
    #     alignment. A decoder that ignores the pad byte reads the wrong offset.
    write('padded.wav', wav(s16, 8000, 1, 16,
                            extra_chunks=[(b'LIST', b'INFOxxxx')]))
    exp['padded'] = s16

    # 20. WAV declaring a compressed codec, which must be refused by name.
    write('mp3.wav', wav(s16, 8000, 1, 16, fmt=0x0055))
    exp['mp3'] = None

    # 21. Truncated PNG: valid header, body cut off.
    good = png(4, 2, 8, 2, rows)
    write('trunc.png', good[:len(good) // 2])
    exp['trunc'] = None

    # 22. Corrupted PNG: one byte flipped inside IDAT, so the CRC must catch it
    #     before DEFLATE is asked to interpret garbage.
    bad = bytearray(good)
    idx = bad.find(b'IDAT') + 8
    bad[idx + 4] ^= 0xFF
    write('corrupt.png', bytes(bad))
    exp['corrupt'] = None

    # 23. Truncated BMP.
    write('trunc.bmp', bmp(2, 2, bmp_rows, bpp=24)[:30])
    exp['trunc_bmp'] = None

    # 24. RLE-compressed BMP. Generated rather than hand-assembled: a byte-count
    #     slip in a literal hex blob produces a fixture that tests nothing while
    #     looking deliberate. The palette comes first because that is where the
    #     format puts it.
    rle_pal = b''.join(bytes([b_, g_, r_, 0]) for r_, g_, b_ in
                       [(255, 0, 0), (0, 255, 0), (0, 0, 255), (9, 9, 9)])
    rle_pixels = bytes([0x00, 0x03,          # absolute mode: 3 copies of index 0
                        0x01, 0x04,          # absolute mode: 4 copies of index 1
                        0x00, 0x02])         # end of line
    rle_body = rle_pal + rle_pixels
    rle_hdr = struct.pack('<IiiHHIIiiII', 40, 4, 1, 1, 8, 1,
                           len(rle_pixels), 2835, 2835, 4, 0)
    rle_off = 14 + 40 + len(rle_pal)
    write('rle8.bmp',
          b'BM' + struct.pack('<IHHI', rle_off + len(rle_pixels), 0, 0, rle_off)
          + rle_hdr + rle_body)
    exp['rle8'] = None

    # ── expected.h ──
    lines = ['/* GENERATED by gen_fixtures.py -- do not edit. */',
             '#ifndef FIXTURES_H', '#define FIXTURES_H', '#include <stdint.h>', '']

    def emit(name, values):
        if values is None:
            return
        lines.append('#define %s_W %d' % (name.upper(), len(values)))
        lines.append('static const uint32_t %s[] = {' % name)
        for i in range(0, len(values), 6):
            # Mask to 32 bits and print as an unsigned literal. Audio
            # expectations are negative (e.g. -32768), and '%08x' on a negative
            # Python int emits "0x-8000", which is not a C integer literal at
            # all -- it silently swallowed the preceding 0x and made the whole
            # array fail to parse.
            chunk = ['0x%08xu' % (v & 0xFFFFFFFF) for v in values[i:i + 6]]
            lines.append('    ' + ', '.join(chunk) + ',')
        lines.append('};')

    for key in ('rgb8', 'rgb8_filters', 'rgb8_stored', 'rgb8_dynamic', 'rgba8',
                'grey8', 'grey1', 'pal8', 'pal4', 'rgb16', 'grey16', 'rgba16',
                'bgr24', 'bgra32_topdown', 'bgra32_noalpha', 'pal8_bmp'):
        emit(key, exp[key])
    for key in ('m16', 's16', 'm8', 'm24', 'f32', 'padded'):
        emit(key, exp[key])

    lines += ['', '#endif', '']
    with open(os.path.join(outdir, 'expected.h'), 'w') as f:
        f.write('\n'.join(lines))

    print('fixtures written to', outdir)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'fixtures')