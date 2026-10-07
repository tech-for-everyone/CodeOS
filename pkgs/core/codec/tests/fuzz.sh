#!/bin/sh
# codec fuzz run.
#
# Takes every real fixture, then produces mutated copies (truncations, single
# bit flips, corrupted magic) and feeds all of them to the decoders under
# -fsanitize=address,undefined.
#
# Nothing here asserts a particular decode result. A decoder is entitled to
# refuse essentially all mutated input. What is asserted is memory safety: no
# out-of-bounds read or write, no signed overflow, no bad shift, no divide by
# zero. Decoders are the classic place for those, because every field in an
# image header is attacker-controlled and is used as a loop bound or a size.
#
# The mutation count is fixed and the seed is fixed, so a clean run is a
# repeatable claim rather than a lucky sample.
#
#   sh pkgs/core/codec/tests/fuzz.sh
#
# Exits non-zero if any sanitizer diagnostic fires, or if zero inputs were fed
# (which would pass while testing nothing).

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src="$here/../src"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail=0

echo "codec fuzz"

if [ ! -x /usr/bin/gcc ] && ! command -v gcc >/dev/null 2>&1; then
    echo "  ..   no gcc; skipping (the fuzz run needs a sanitizer build)"
    exit 0
fi

python3 "$here/gen_fixtures.py" "$tmp/fx" >/dev/null
echo "  ..   fixtures generated"

python3 "$here/gen_fuzz.py" "$tmp/fx" "$tmp/fx/fuzz" || {
    echo "  FAIL could not generate fuzz inputs"
    exit 1
}

# shellcheck disable=SC2086
gcc -std=c99 -O1 -g -Wall -Wextra \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DCODEC_HOST_TEST=1 -I"$src" \
    -o "$tmp/fuzz" \
    "$src/codec_registry.c" "$src/codec_inflate.c" "$src/codec_png.c" \
    "$src/codec_bmp.c" "$src/codec_wav.c" "$here/codec_fuzz.c"

# ASAN_OPTIONS=abort_on_error makes a diagnostic a non-zero exit, and
# halt_on_error stops at the first one so the report is about a real fault
# rather than a cascade.
if ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 \
   UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
   "$tmp/fuzz" "$tmp/fx" "$tmp/fx/fuzz" >"$tmp/fuzz.out" 2>"$tmp/asan.log"; then
    echo "  ..   $(cat "$tmp/fuzz.out")"
    if [ -s "$tmp/asan.log" ]; then
        echo "  FAIL sanitizer reported something:"
        sed 's/^/       /' "$tmp/asan.log" | head -40
        fail=1
    else
        echo "  ok   no sanitizer diagnostics"
    fi
else
    echo "  ..   $(cat "$tmp/fuzz.out")"
    echo "  FAIL fuzz run crashed or a sanitizer fired:"
    sed 's/^/       /' "$tmp/asan.log" | head -40
    fail=1
fi

# The real fixtures must still decode cleanly under the fuzz driver. If a
# mutation campaign broke normal operation, the run above would look clean for
# the wrong reason.
# The real fixtures must still decode cleanly under the fuzz driver. If a
# mutation campaign broke normal operation, the run above would look clean for
# the wrong reason. The check is on the count, so it fails if the decoders
# started rejecting everything.
real=$(ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 \
       "$tmp/fuzz" "$tmp/fx" 2>>"$tmp/asan.log" || true)
echo "  ..   $real"
case "$real" in
    *" 0 decoded as image"*)
        echo "  FAIL fuzz driver decoded none of the real fixtures"
        fail=1
        ;;
    *)
        if ! printf '%s' "$real" | grep -q "decoded as image"; then
            echo "  FAIL fuzz driver produced no summary"
            fail=1
        else
            echo "  ok   real fixtures still decode under the fuzz driver"
        fi
        ;;
esac

if [ "$fail" = 0 ]; then
    echo "codec fuzz: clean"
else
    echo "codec fuzz: FAILURES"
fi
exit "$fail"