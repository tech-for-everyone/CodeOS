#!/bin/sh
# codec host test.
#
# Compiles the real decoders against the host libc (CODEC_HOST_TEST switches
# codec_alloc.h over) and checks them against fixtures whose expected pixels were
# computed independently by tests/gen_fixtures.py.
#
# The negative controls are the point of this script. Three separate checks in
# here can pass by construction rather than by correctness -- a red/blue swap, an
# alpha byte in the wrong place, and a wrong Paeth predictor all produce images
# that look plausible rather than broken -- so each one is rebuilt with the
# defect deliberately reintroduced and the script asserts that build FAILS.
# If a control build passes, the corresponding real check is not a check.
#
#   sh pkgs/core/codec/tests/run.sh
#
# Exits non-zero on any failure, control or real.

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src="$here/../src"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail=0
pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }

SRCS="$src/codec_registry.c $src/codec_inflate.c $src/codec_png.c $src/codec_bmp.c $src/codec_wav.c"

echo "codec host test"

# ── fixtures ────────────────────────────────────────────────────────────────
# expected.h is generated next to the fixture files, so the fixture directory has
# to be on the include path. Hence a build function rather than a bare $CFLAGS
# variable: adding it by hand to each of the four builds below is how one of them
# ends up quietly compiled without it.
python3 "$here/gen_fixtures.py" "$tmp/fx" >/dev/null
echo "  ..   fixtures generated in $tmp/fx"

# build <output> [extra cc flags...]
build() {
    out=$1
    shift
    # shellcheck disable=SC2086
    gcc -std=c99 -O1 -Wall -Wextra -DCODEC_HOST_TEST=1 \
        -I"$src" -I"$here" -I"$tmp/fx" "$@" \
        -o "$out" $SRCS "$here/codec_host_test.c"
}

# ── the real build ──────────────────────────────────────────────────────────
build "$tmp/t" 2>"$tmp/build.log" || {
    echo "  FAIL build failed:"
    sed 's/^/       /' "$tmp/build.log"
    exit 1
}
if [ -s "$tmp/build.log" ]; then
    bad "build produced warnings:"
    sed 's/^/       /' "$tmp/build.log"
else
    pass "builds clean with -Wall -Wextra"
fi

if "$tmp/t" "$tmp/fx"; then
    pass "all real checks pass"
else
    bad "real checks failed"
fi

# ── negative control 1: BMP red/blue swapped ────────────────────────────────
build "$tmp/t_bgr" -DCODEC_NEG_SWAP_BGR 2>/dev/null
out=$("$tmp/t_bgr" "$tmp/fx" 2>&1) || true
if echo "$out" | grep -q "bgr24.bmp: all 4 pixels exact"; then
    bad "NEGATIVE CONTROL: BGR-swapped build still passed bgr24 -- control is dead"
else
    pass "NEGATIVE CONTROL: BGR-swapped build fails bgr24 as it must"
fi

# ── negative control 2: alpha in the bottom byte (0xRRGGBBAA) ───────────────
build "$tmp/t_rgba" -DCODEC_NEG_RGBA 2>/dev/null
out=$("$tmp/t_rgba" "$tmp/fx" 2>&1) || true
if echo "$out" | grep -q "rgba8.png: all 4 pixels exact"; then
    bad "NEGATIVE CONTROL: RGBA-packed build still passed rgba8 -- control is dead"
else
    pass "NEGATIVE CONTROL: RGBA-packed build fails rgba8 as it must"
fi

# ── negative control 3: wrong Paeth predictor ───────────────────────────────
build "$tmp/t_paeth" -DCODEC_NEG_WRONG_PAETH 2>/dev/null
out=$("$tmp/t_paeth" "$tmp/fx" 2>&1) || true
if echo "$out" | grep -q "rgb8_filters.png: all 32 pixels exact"; then
    bad "NEGATIVE CONTROL: wrong-Paeth build still passed the filter fixture"
else
    pass "NEGATIVE CONTROL: wrong-Paeth build fails the filter fixture"
fi

if [ "$fail" = 0 ]; then
    echo "codec: all checks passed"
else
    echo "codec: FAILURES"
fi
exit "$fail"