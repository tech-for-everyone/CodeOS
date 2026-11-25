#!/bin/sh
# tar host test — builds the real port sources against the shared CodeOS fs
# shim (pkgs/ports/tests/codeos_fs_shim.h) and checks the ustar bytes with
# Python's tarfile, plus extraction and hostile-input handling.
#
#   sh pkgs/util/tar/tests/run.sh
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

gcc -std=c11 -O1 -Wall -Wextra -Werror -o "$tmp/tar" -include "$shim" "$src/tar.c"

echo "tar port host test"

# 1. Create a nested archive. Python must see the exact ustar members and
#    payloads, which proves the header octal fields, padding and trailer are
#    real ustar rather than something only our reader understands.
mkdir -p "$tmp/in/sub"
printf 'hello tar\n' > "$tmp/in/a.txt"
python3 -c "open('$tmp/in/sub/b.bin','wb').write(bytes(range(256))*20)"
(cd "$tmp" && ./tar -cf out.tar in)
python3 - "$tmp/out.tar" <<'PY'
import sys, tarfile
with tarfile.open(sys.argv[1]) as f:
    names = f.getnames()
    assert names == ['in', 'in/sub', 'in/sub/b.bin', 'in/a.txt'], names
    assert f.extractfile('in/a.txt').read() == b'hello tar\n'
    assert f.extractfile('in/sub/b.bin').read() == bytes(range(256)) * 20
PY
[ "$fail" = 0 ] && pass "create: Python reads all members and payloads" \
                 || bad "create: ustar output rejected: $(cat "$tmp/tar.log" 2>/dev/null)"

# 2. -tf lists the same names.
out=$(cd "$tmp" && ./tar -tf out.tar)
[ "$out" = "in/
in/sub/
in/sub/b.bin
in/a.txt" ] && pass "-tf lists every member" || bad "-tf output wrong: $out"

# 3. -xf restores the bytes exactly, including nested directories.
mkdir "$tmp/out"
(cd "$tmp/out" && ../tar -xf ../out.tar)
python3 - "$tmp" <<'PY' && pass "-xf restores files byte for byte" || bad "-xf extraction differs"
import os, sys
d = sys.argv[1]
for rel in ('in/a.txt', 'in/sub/b.bin'):
    a = open(os.path.join(d, rel), 'rb').read()
    b = open(os.path.join(d, 'out', rel), 'rb').read()
    assert a == b, rel
PY

# 4. Extract an archive produced by Python, so the reader is checked against
#    an independent writer (including an explicit TarInfo and prefix-free name).
python3 - "$tmp/py.tar" <<'PY'
import io, sys, tarfile
with tarfile.open(sys.argv[1], 'w') as f:
    info = tarfile.TarInfo('docs/readme.txt')
    data = b'from python\n'
    info.size = len(data)
    f.addfile(info, io.BytesIO(data))
PY
mkdir "$tmp/pyout"
(cd "$tmp/pyout" && ../tar -xf ../py.tar)
python3 - "$tmp/pyout/docs/readme.txt" <<'PY' && pass "-xf reads a Python-generated archive" || bad "Python archive failed"
import sys
assert open(sys.argv[1], 'rb').read() == b'from python\n'
PY

# 5. A bad checksum must be reported, not treated as a member.
python3 - "$tmp/out.tar" "$tmp/bad.tar" <<'PY'
import sys
b = bytearray(open(sys.argv[1], 'rb').read())
b[0] ^= 0xFF
open(sys.argv[2], 'wb').write(bytes(b))
PY
set +e
(cd "$tmp" && ./tar -tf bad.tar) > "$tmp/bad.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "corrupt header exits non-zero" || bad "corrupt header accepted"
grep -q "corrupt header" "$tmp/bad.log" \
    && pass "corrupt header is named" || bad "wrong diagnosis: $(cat "$tmp/bad.log")"

# 6. A traversal member must be refused before touching the filesystem.
python3 - "$tmp/evil.tar" <<'PY'
import io, sys, tarfile
with tarfile.open(sys.argv[1], 'w') as f:
    info = tarfile.TarInfo('../escaped.txt')
    data = b'do not write me\n'
    info.size = len(data)
    f.addfile(info, io.BytesIO(data))
PY
set +e
(cd "$tmp" && ./tar -xf evil.tar) > "$tmp/evil.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "traversal member exits non-zero" || bad "traversal member accepted"
[ ! -e "$tmp/escaped.txt" ] && pass "traversal wrote no file outside the target" \
                            || bad "traversal escaped the extraction directory"

# 7. A member beyond the kernel's 64 KiB file cap must be refused.
python3 - "$tmp/toobig.tar" <<'PY'
import io, sys, tarfile
with tarfile.open(sys.argv[1], 'w', format=tarfile.USTAR_FORMAT) as f:
    info = tarfile.TarInfo('big.bin')
    data = b'x' * 70000
    info.size = len(data)
    f.addfile(info, io.BytesIO(data))
PY
set +e
(cd "$tmp" && ./tar -xf toobig.tar) > "$tmp/toobig.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "over-cap archive exits non-zero" || bad "over-cap archive accepted"
grep -q "archive too large" "$tmp/toobig.log" \
    && pass "over-cap archive names the limit" || bad "wrong diagnosis: $(cat "$tmp/toobig.log")"

if [ "$fail" = 0 ]; then
    echo "tar: all checks passed"
else
    echo "tar: FAILURES"
fi
exit "$fail"
