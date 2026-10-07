#!/usr/bin/env python3
"""Generate mutated codec inputs for the fuzz run.

Reads every real fixture from <fixtures> and writes variants into <outdir>:

  trunc_*    truncated at a fixed set of fractions
  flip_*     one bit flipped in the first 200 bytes, where the headers live
  magic_*    a leading byte replaced, forcing the registry to misdispatch
  junk_*     random bytes, sometimes starting with a real magic

Only headers and compressed data are mutated; the dimension fields are the most
interesting, because a decoder that trusts them as an allocation size is exactly
the bug being looked for, so those bytes are hit hard by flip_ and magic_.

The seed is fixed so a clean run is reproducible.
"""

import os
import random
import sys

SEED = 20261005


def load(fixtures):
    seeds = []
    for name in sorted(os.listdir(fixtures)):
        if name == 'expected.h':
            continue
        path = os.path.join(fixtures, name)
        if os.path.isfile(path) and 0 < os.path.getsize(path) < 200000:
            with open(path, 'rb') as f:
                seeds.append((name, f.read()))
    return seeds


def main(fixtures, outdir):
    os.makedirs(outdir, exist_ok=True)
    rng = random.Random(SEED)
    seeds = load(fixtures)
    if not seeds:
        print('no fixtures found in', fixtures, file=sys.stderr)
        return 1

    written = 0

    def put(tag, name, data):
        nonlocal written
        path = os.path.join(outdir, '%s_%s' % (tag, name.replace('.', '_')))
        with open(path, 'wb') as f:
            f.write(data)
        written += 1

    for name, data in seeds:
        # Truncation. A decoder that reads length fields without checking them
        # against the bytes actually present shows up here.
        for frac in (0.05, 0.2, 0.35, 0.5, 0.66, 0.8, 0.9, 0.99, 0.999):
            k = int(len(data) * frac)
            if k > 0:
                put('trunc', name, data[:k])

        # Single-bit flips near the front, which covers IHDR / BITMAPINFOHEADER /
        # fmt chunk: dimensions, bit depth, colour type, compression, channels.
        if len(data) < 400:
            continue
        head = min(len(data), 200)
        for _ in range(60):
            d = bytearray(data)
            i = rng.randrange(0, head)
            d[i] ^= 1 << rng.randrange(0, 8)
            put('flip', '%s_%d' % (name, i), bytes(d))

        # Replace a leading byte outright. This both corrupts the magic (so the
        # registry may dispatch to the wrong decoder) and, when the magic
        # survives, corrupts a header field.
        for i in range(0, min(16, len(data))):
            d = bytearray(data)
            d[i] = rng.randrange(1, 256)
            put('magic', '%s_%d' % (name, i), bytes(d))

        # Byte-swap style damage to length fields: swap adjacent pairs in the
        # header, which turns small lengths into large ones.
        if len(data) >= 24:
            d = bytearray(data)
            for i in range(0, min(20, len(data) - 1), 2):
                d[i], d[i + 1] = d[i + 1], d[i]
            put('swap', name, bytes(d))

    # Random junk, half of it prefixed with a real magic so the registry matches
    # and a decoder receives pure garbage it believes is valid.
    magics = [b'\x89PNG\r\n\x1a\n', b'BM', b'\xff\xd8\xff', b'RIFF',
              b'GIF8', b'qoif', b'fLaC']
    for i in range(120):
        size = rng.randrange(0, 600)
        blob = bytes(rng.randrange(0, 256) for _ in range(size))
        if i % 2 == 0 and magics:
            blob = rng.choice(magics) + blob
        put('junk', str(i), blob)

    print('generated %d fuzz inputs in %s' % (written, outdir))
    return 0


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('usage: gen_fuzz.py <fixtures> <outdir>', file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))