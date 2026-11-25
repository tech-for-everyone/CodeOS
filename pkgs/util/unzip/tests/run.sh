#!/bin/sh
# unzip host test — builds the real port sources with the shared CodeOS fs
# shim and checks them against archives written by Python's zipfile.
#
#   sh pkgs/util/unzip/tests/run.sh

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
shim="$here/../../../ports/tests/codeos_fs_shim.h"
src="$here/../src"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }
fail=0

gcc -std=c11 -O1 -Wall -Wextra -Werror -o "$tmp/unzip" -include "$shim" "$src/unzip.c"

echo "unzip port host test"

# 1. Extract an archive with nested directories written by Python (stored).
python3 - "$tmp/py.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_STORED) as z:
    z.writestr('docs/readme.txt', b'from python\n')
    z.writestr('docs/sub/data.bin', bytes(range(256)) * 20)
PY
mkdir "$tmp/out"
(cd "$tmp/out" && ../unzip "$tmp/py.zip")
python3 - "$tmp/out" <<'PY' && pass "-xf restores Python's stored members byte for byte" || bad "extraction differs"
import sys
d = sys.argv[1]
assert open(d + '/docs/readme.txt', 'rb').read() == b'from python\n'
assert open(d + '/docs/sub/data.bin', 'rb').read() == bytes(range(256)) * 20
PY

# 2. -l lists members; -p writes one member to stdout.
out=$(cd "$tmp" && ./unzip -l py.zip)
echo "$out" | grep -q "docs/readme.txt  12 bytes" && pass "-l lists members" \
    || bad "-l output wrong: $out"
(cd "$tmp" && ./unzip -p py.zip docs/sub/data.bin) > "$tmp/p.out"
python3 - "$tmp/p.out" <<'PY' && pass "-p writes a member to stdout" || bad "-p payload wrong"
import sys
assert open(sys.argv[1], 'rb').read() == bytes(range(256)) * 20
PY

# 3. An explicit member filter only handles the named entry.
mkdir "$tmp/filtered"
(cd "$tmp/filtered" && ../unzip "$tmp/py.zip" docs/readme.txt) >/dev/null
[ -f "$tmp/filtered/docs/readme.txt" ] && [ ! -e "$tmp/filtered/docs/sub/data.bin" ] \
    && pass "member filter extracts only the named entry" \
    || bad "member filter extracted too much"

# 4. An EOCD comment must not hide the central directory.
python3 - "$tmp/comment.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_STORED) as z:
    z.comment = b'x' * 5000
    z.writestr('note.txt', b'still findable\n')
PY
mkdir "$tmp/commentout"
(cd "$tmp/commentout" && ../unzip "$tmp/comment.zip") >/dev/null
[ "$(cat "$tmp/commentout/note.txt")" = "still findable" ] \
    && pass "archive with a 5000-byte comment is read" \
    || bad "commented archive failed"

# 5. Deflate members are identified, not silently written as garbage.
python3 - "$tmp/deflate.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('d.txt', b'A' * 1000)
PY
set +e
(cd "$tmp" && ./unzip deflate.zip) > "$tmp/deflate.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "deflate member exits non-zero" || bad "deflate member accepted"
grep -q "method 8" "$tmp/deflate.log" \
    && pass "deflate member names the unsupported method" \
    || bad "wrong diagnosis: $(cat "$tmp/deflate.log")"

# 6. A corrupt payload must fail its CRC instead of being written.
python3 - "$tmp/py.zip" "$tmp/bad.zip" <<'PY'
import sys
b = bytearray(open(sys.argv[1], 'rb').read())
b[len(b) // 2] ^= 0xFF
open(sys.argv[2], 'wb').write(bytes(b))
PY
mkdir "$tmp/badout"
set +e
(cd "$tmp/badout" && ../unzip "$tmp/bad.zip") > "$tmp/bad.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "corrupt member exits non-zero" || bad "corrupt member accepted"
grep -q "CRC" "$tmp/bad.log" && pass "corrupt member names the CRC failure" \
    || bad "wrong diagnosis: $(cat "$tmp/bad.log")"

# 7. Traversal members are refused before any file is created.
python3 - "$tmp/evil.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_STORED) as z:
    z.writestr('../escaped.txt', b'do not write me\n')
PY
mkdir "$tmp/evilout"
set +e
(cd "$tmp/evilout" && ../unzip "$tmp/evil.zip") > "$tmp/evil.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "traversal member exits non-zero" || bad "traversal member accepted"
[ ! -e "$tmp/escaped.txt" ] && pass "traversal wrote no file outside the target" \
                            || bad "traversal escaped the extraction directory"

if [ "$fail" = 0 ]; then echo "unzip: all checks passed"; else echo "unzip: FAILURES"; fi
exit "$fail"
