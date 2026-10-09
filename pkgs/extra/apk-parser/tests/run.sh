#!/bin/sh
# apk-parser host test.
#
# The real apk-parser.c is a freestanding CodeOS binary, so it can only be
# exercised inside a booted guest -- which is a ~170 s round trip per case and
# makes it easy to ship a change that was only ever built, never run. This
# compiles the *real* source against a POSIX shim (tests/apk_shim.h, same
# pattern as pkgs/core/ncvm/tests/codeos_shim.h) so the parsing logic can be
# tested directly, including on inputs larger than the guest could ever
# present.
#
#   sh pkgs/extra/apk-parser/tests/run.sh
#
# Exits non-zero on the first failed expectation.

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../../.." && pwd)
src="$root/pkgs/extra/apk-parser/src/apk-parser.c"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

apk="$root/pkgs/dist/netbeam-1.0.0.apk"
fail=0
pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }

# ── build the real parser, unrestricted ────────────────────────────────────
gcc -std=c99 -O1 -Wall -Wextra -o "$tmp/apk-parser" \
    -include "$here/apk_shim.h" "$src"

# ── build it again with the kernel's FS_CONTENT_MAX clamp emulated ─────────
gcc -std=c99 -O1 -Wall -Wextra -DAPK_SHIM_FILE_CAP=65536 \
    -o "$tmp/apk-parser-cap" -include "$here/apk_shim.h" "$src"

# ── build the negative control ────────────────────────────────────────────
gcc -std=c99 -O1 -Wall -Wextra -o "$tmp/legacy" \
    "$here/legacy_apk_parser_8k.c"

# A >64 KiB archive, to exercise the truncation path. Only exists on the host:
# the guest could never hand this to the parser.
#
# ZIP_STORED, deliberately. An earlier version used ZIP_DEFLATED over runs of
# a single repeated byte, which compresses to a few hundred bytes -- the
# archive never exceeded the cap, so the truncation path was never entered
# and the checks below "passed" for the wrong reason (rc=0, no diagnosis).
python3 - "$tmp/big.zip" <<'PY'
import sys, zipfile, os
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_STORED) as z:
    z.writestr('AndroidManifest.xml', os.urandom(256))
    z.writestr('classes.dex', os.urandom(200000))
    z.writestr('lib/arm64-v8a/libbig.so', os.urandom(40000))
print('big.zip bytes:', os.path.getsize(sys.argv[1]))
PY

echo "apk-parser host test"

# 1. The real APK in the tree must parse completely.
out=$("$tmp/apk-parser" "$apk" 2>&1) || true
echo "$out" | grep -q "AndroidManifest.xml" \
    && pass "lists AndroidManifest.xml from the real APK" \
    || bad "did not list AndroidManifest.xml"
echo "$out" | grep -q "classes.dex" \
    && pass "lists classes.dex" || bad "did not list classes.dex"
echo "$out" | grep -q "lib/x86_64/netbeam" \
    && pass "lists the native lib" || bad "did not list the native lib"
echo "$out" | grep -q "Entries listed: 8 of 8" \
    && pass "all 8 entries listed" || bad "entry count wrong: $(echo "$out" | grep 'Entries listed')"
echo "$out" | grep -q "Native .so libraries: 0" \
    && pass "native .so count is 0 -- netbeam's lib has no .so suffix" \
    || bad "unexpected .so count: $(echo "$out" | grep 'Native .so')"
echo "$out" | grep -q "not a valid ZIP" \
    && bad "still emits the old bogus message" \
    || pass "does not claim a valid APK is invalid"

# 2. NEGATIVE CONTROL: the old algorithm must fail on that same APK. If this
#    ever passes, every check above has stopped being sensitive.
if "$tmp/legacy" "$apk" >/dev/null 2>&1; then
    bad "NEGATIVE CONTROL: legacy 8 KiB parser succeeded -- control is dead"
else
    pass "NEGATIVE CONTROL: legacy 8 KiB parser fails on this APK"
fi

# 3. With the 64 KiB cap emulated, an under-cap APK must still parse: the cap
#    is the kernel's, not the parser's, and must not cost us the good case.
out=$("$tmp/apk-parser-cap" "$apk" 2>&1) || true
echo "$out" | grep -q "Entries listed: 8 of 8" \
    && pass "24 KB APK parses even under the 64 KiB cap" \
    || bad "under-cap APK failed under the cap: $(echo "$out" | tail -1)"

# 4. An archive past the cap. EOCD for a >64 KiB archive sits past the cap
#    too, so the honest outcome is "no EOCD found" PLUS an explicit statement
#    that the file was truncated on open -- not the old "not a valid ZIP".
#    (The cd_offset-past-end check in check 4b is the separate case where the
#    record *is* reachable but describes more than is there.)
set +e
out=$("$tmp/apk-parser-cap" "$tmp/big.zip" 2>&1); rc=$?
set -e
[ "$rc" = 1 ] \
    && pass "over-cap archive exits 1" \
    || bad "over-cap archive exit=$rc (want 1)"
echo "$out" | grep -q "no ZIP End Of Central Directory" \
    && pass "over-cap: reports the record is unreachable" \
    || bad "over-cap: wrong diagnosis: $(echo "$out" | tail -3)"
echo "$out" | grep -q "65536" \
    && pass "over-cap: names the 65536 cap" || bad "over-cap: cap not named"
echo "$out" | grep -q "truncated" \
    && pass "over-cap: says truncated on open, not 'invalid'" \
    || bad "over-cap: does not say truncated"

# 4b. Reachable EOCD describing a central directory that runs past the end.
#     Crafted by inflating cd_size in an otherwise valid small archive, which
#     is the only way to reach that branch.
python3 - "$tmp/shortcd.zip" <<'PY'
import sys, zipfile
p = sys.argv[1]
with zipfile.ZipFile(p, 'w') as z:
    z.writestr('classes.dex', b'A' * 5000)
b = bytearray(open(p, 'rb').read())
i = b.rfind(b'PK\x05\x06')          # EOCD signature
b[i+12:i+16] = (0x00FFFFFF).to_bytes(4, 'little')   # absurd cd_size
open(p, 'wb').write(bytes(b))
PY
set +e
out=$("$tmp/apk-parser" "$tmp/shortcd.zip" 2>&1); rc=$?
set -e
[ "$rc" = 2 ] \
    && pass "CD running past EOF exits 2" \
    || bad "CD-past-EOF exit=$rc (want 2)"
echo "$out" | grep -q "past the" \
    && pass "CD-past-EOF names the overrun" \
    || bad "CD-past-EOF: no diagnosis: $(echo "$out" | tail -3)"
echo "$out" | grep -q "not a valid ZIP" \
    && bad "CD-past-EOF still blames the file" \
    || pass "CD-past-EOF does not blame the file"

# 5. The same big archive parses fine without the cap, proving checks 4/4b were
#    detecting the clamp and the corruption, not some unrelated defect.
out=$("$tmp/apk-parser" "$tmp/big.zip" 2>&1) || true
echo "$out" | grep -q "Entries listed: 3 of 3" \
    && pass "over-cap archive parses when not clamped" \
    || bad "unclamped big archive failed: $(echo "$out" | tail -2)"
echo "$out" | grep -q "Native .so libraries: 1" \
    && pass "a real .so inside lib/ is counted" \
    || bad "libbig.so not counted: $(echo "$out" | grep 'Native .so')"

# 6. Garbage in, clean failure out -- no crash, no hang.
head -c 4096 /dev/urandom > "$tmp/junk.bin"
set +e
out=$("$tmp/apk-parser" "$tmp/junk.bin" 2>&1); rc=$?
set -e
[ "$rc" = 1 ] \
    && pass "random bytes rejected with rc=1" \
    || bad "random bytes rc=$rc (want 1)"
echo "$out" | grep -q "no ZIP End Of Central Directory" \
    && pass "names the actual problem" || bad "vague error: $out"

# 7. A ZIP with a long archive comment, so EOCD is not in the last 22 bytes.
#    This is the case a naive "peek at the tail" fix would still get wrong.
python3 - "$tmp/comment.zip" <<'PY'
import sys, zipfile, os
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_STORED) as z:
    z.comment = b'x' * 5000          # pushes EOCD 5000 bytes back
    z.writestr('classes.dex', os.urandom(30000))
PY
out=$("$tmp/apk-parser" "$tmp/comment.zip" 2>&1) || true
echo "$out" | grep -q "Entries listed: 1 of 1" \
    && pass "archive with a 5000-byte comment still found" \
    || bad "commented archive failed: $(echo "$out" | tail -2)"

# 8. Zero-length file.
: > "$tmp/empty.bin"
set +e
out=$("$tmp/apk-parser" "$tmp/empty.bin" 2>&1); rc=$?
set -e
[ "$rc" = 1 ] && pass "empty file rejected with rc=1" || bad "empty file rc=$rc"

if [ "$fail" = 0 ]; then
    echo "apk-parser: all checks passed"
else
    echo "apk-parser: FAILURES"
fi
exit "$fail"