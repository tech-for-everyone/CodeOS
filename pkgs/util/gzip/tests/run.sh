#!/bin/sh
# gzip host test — builds the real port sources against the shared CodeOS fs
# shim (pkgs/ports/tests/codeos_fs_shim.h) and checks the bytes against
# Python's gzip implementation, not against the port's own output alone.
#
#   sh pkgs/util/gzip/tests/run.sh
#
# Exits non-zero on the first failed expectation.

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
shim="$here/../../../ports/tests/codeos_fs_shim.h"
src="$here/../src"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }
fail=0

gcc -std=c11 -O1 -Wall -Wextra -Werror -o "$tmp/gzip" \
    -include "$shim" "$src/gzip.c" "$src/inflate.c"
ln -s "$tmp/gzip" "$tmp/gunzip"

echo "gzip port host test"

# 1. Our compressor must produce a stream Python can read, and the output file
#    must be named <input>.gz.
printf 'hello gzip port\nsecond line\n' > "$tmp/note.txt"
(cd "$tmp" && ./gzip note.txt)
python3 - "$tmp/note.txt" "$tmp/note.txt.gz" <<'PY'
import gzip, sys
want = open(sys.argv[1], 'rb').read()
got = gzip.open(sys.argv[2], 'rb').read()
assert got == want, (got, want)
PY
[ -f "$tmp/note.txt.gz" ] && pass "compress: valid gzip stream named note.txt.gz" \
                          || bad "compress: missing .gz output"

# 2. Our decompressor must restore that file byte for byte.
(cd "$tmp" && ./gzip -d note.txt.gz)
python3 - "$tmp/note.txt" <<'PY' && pass "decompress: restores the original bytes" || bad "decompress: content differs"
import sys
assert open(sys.argv[1], 'rb').read() == b'hello gzip port\nsecond line\n'
PY

# 3. Python's compressor emits dynamic-Huffman blocks; the vendored decoder
#    must handle those, not just our own stored blocks.
python3 - "$tmp/py.gz" <<'PY'
import gzip, sys
data = b'A' * 1000 + bytes(range(256)) * 40
with gzip.open(sys.argv[1], 'wb') as f: f.write(data)
PY
(cd "$tmp" && ./gzip -d -c py.gz) > "$tmp/py.out"
python3 - "$tmp/py.out" <<'PY' && pass "inflate: decodes Python's dynamic-Huffman stream" || bad "inflate: failed on Python's stream"
import sys
want = b'A' * 1000 + bytes(range(256)) * 40
assert open(sys.argv[1], 'rb').read() == want
PY

# 4. A gzip header carrying FNAME (and FCOMMENT) must be skipped correctly.
python3 - "$tmp/flagged.gz" <<'PY'
import io, sys, gzip, struct, zlib
buf = io.BytesIO()
with gzip.GzipFile(filename='inner.txt', mode='wb', fileobj=buf) as f:
    f.write(b'flagged header payload\n')
raw = bytearray(buf.getvalue())
raw[3] |= 16                                   # add FCOMMENT
fname_end = raw.index(b'\0', 10) + 1
raw = raw[:fname_end] + b'note\0' + raw[fname_end:]
open(sys.argv[1], 'wb').write(bytes(raw))
PY
(cd "$tmp" && ./gunzip -c flagged.gz) > "$tmp/flagged.out"
python3 - "$tmp/flagged.out" <<'PY' && pass "argv[0]=gunzip implies -d; FNAME/FCOMMENT skipped" || bad "flagged header or gunzip alias failed"
import sys
assert open(sys.argv[1], 'rb').read() == b'flagged header payload\n'
PY

# 5. -l reports the uncompressed size without writing a file.
out=$(cd "$tmp" && ./gzip -l py.gz)
echo "$out" | grep -q "11240 bytes" && pass "-l reports uncompressed size" \
    || bad "-l output wrong: $out"

# 6. Corrupt payload must be rejected by the CRC, not silently accepted.
python3 - "$tmp/note.txt.gz" "$tmp/bad.gz" <<'PY'
import sys
b = bytearray(open(sys.argv[1], 'rb').read())
b[len(b) // 2] ^= 0xFF
open(sys.argv[2], 'wb').write(bytes(b))
PY
set +e
(cd "$tmp" && ./gzip -d bad.gz) > "$tmp/bad.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "corrupt stream exits non-zero" || bad "corrupt stream accepted"
grep -q "CRC or size mismatch" "$tmp/bad.log" \
    && pass "corrupt stream named as CRC/size mismatch" \
    || bad "wrong diagnosis: $(cat "$tmp/bad.log")"

# 7. A zero-length file is legal gzip.
: > "$tmp/empty"
(cd "$tmp" && ./gzip empty && ./gzip -d -c empty.gz) > "$tmp/empty.out"
[ ! -s "$tmp/empty.out" ] && pass "empty file round-trips" || bad "empty file failed"

# 8. Files larger than the kernel's 64 KiB cap must be refused explicitly
#    rather than silently truncated.
python3 -c "open('$tmp/big','wb').write(b'x' * 70000)"
set +e
(cd "$tmp" && ./gzip big) > "$tmp/big.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "over-cap file exits non-zero" || bad "over-cap file accepted"
grep -q "larger than 64 KiB" "$tmp/big.log" \
    && pass "over-cap file names the limit" || bad "wrong diagnosis: $(cat "$tmp/big.log")"

if [ "$fail" = 0 ]; then
    echo "gzip: all checks passed"
else
    echo "gzip: FAILURES"
fi
exit "$fail"
