#!/bin/sh
# zip host test — builds the real port sources with the shared CodeOS fs shim
# and checks the archive with Python's zipfile (an independent implementation).
#
#   sh pkgs/util/zip/tests/run.sh

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
shim="$here/../../../ports/tests/codeos_fs_shim.h"
src="$here/../src"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }
fail=0

gcc -std=c11 -O1 -Wall -Wextra -Werror -o "$tmp/zip" -include "$shim" "$src/zip.c"

echo "zip port host test"

# 1. Create an archive and let Python validate the headers, CRC and payloads.
mkdir -p "$tmp/in/sub"
printf 'hello zip\n' > "$tmp/in/a.txt"
python3 -c "open('$tmp/in/sub/b.bin','wb').write(bytes(range(256))*10)"
(cd "$tmp/in" && ../zip "$tmp/out.zip" a.txt sub/b.bin)
python3 - "$tmp/out.zip" <<'PY' && pass "create: Python validates names, CRC and payloads" || bad "create: invalid ZIP"
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.namelist() == ['a.txt', 'sub/b.bin'], z.namelist()
    assert z.read('a.txt') == b'hello zip\n'
    assert z.read('sub/b.bin') == bytes(range(256)) * 10
    assert z.testzip() is None
PY

# 2. -l reads the central directory back out of our own archive.
out=$(cd "$tmp" && ./zip -l out.zip)
echo "$out" | grep -q "a.txt  10 bytes" && pass "-l lists members and sizes" \
    || bad "-l output wrong: $out"

# 3. A Python reader can extract every member to disk.
mkdir "$tmp/pyout"
python3 - "$tmp/out.zip" "$tmp/pyout" <<'PY' && pass "Python extracts every member" || bad "Python could not extract archive"
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    z.extractall(sys.argv[2])
assert open(sys.argv[2] + '/a.txt', 'rb').read() == b'hello zip\n'
PY

# 4. Zero-length files are legal ZIP entries.
: > "$tmp/empty"
(cd "$tmp" && ./zip empty.zip empty)
python3 - "$tmp/empty.zip" <<'PY' && pass "empty member is accepted" || bad "empty member failed"
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.namelist() == ['empty'] and z.read('empty') == b''
PY

# 5. A file past the kernel's 64 KiB cap must be refused, not truncated.
python3 -c "open('$tmp/big','wb').write(b'x'*70000)"
set +e
(cd "$tmp" && ./zip big.zip big) > "$tmp/big.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "over-cap file exits non-zero" || bad "over-cap file accepted"
grep -q "larger than 64 KiB" "$tmp/big.log" \
    && pass "over-cap file names the limit" || bad "wrong diagnosis: $(cat "$tmp/big.log")"

if [ "$fail" = 0 ]; then echo "zip: all checks passed"; else echo "zip: FAILURES"; fi
exit "$fail"
