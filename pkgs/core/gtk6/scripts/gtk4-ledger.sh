#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compile every imported GTK4 C file with gtk6's freestanding flags and record
# what stops each one.
#
# This is the measuring stick for the cairo/pango/gdk-pixbuf surgery in
# PLAN.md.  The OK count is the part of the widget layer that is already
# Qt-clean; the "top blockers" table says which dependency to remove next, and
# which file to look at.  It never links, so a missing cairo_* implementation
# shows up as a compile error naming the call site -- which is exactly the
# file-by-file work item.
#
# The compiler and flags come from the package Makefile (GTK4_CC, GTK4_CFLAGS),
# so there is exactly one definition of the build contract.
#
# Usage: scripts/gtk4-ledger.sh [OUTPUT]

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

CC=${GTK4_CC:-$HERE/toolchain/cc}
CFLAGS=${GTK4_CFLAGS:-}
OUT=${1:-build/gtk4-ledger.txt}

mkdir -p "$(dirname "$OUT")"
: > "$OUT"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

find gdk gsk gtk -name '*.c' -not -path '*/tests/*' | sort > "$tmp/files"

total=0
ok=0
while IFS= read -r f; do
    total=$((total + 1))
    if $CC $CFLAGS -fsyntax-only "$f" > "$tmp/err" 2>&1; then
        printf 'OK   %s\n' "$f" >> "$OUT"
        ok=$((ok + 1))
    else
        reason=$(grep -m1 -oE 'fatal error: .*|error: .*' "$tmp/err" | head -1)
        printf 'FAIL %s :: %s\n' "$f" "${reason:-(unknown)}" >> "$OUT"
    fi
done < "$tmp/files"

printf 'gtk4-ledger: %d/%d imported C files compile\n' "$ok" "$total"
printf 'top blockers:\n'
grep '^FAIL' "$OUT" | sed 's/.*:: //' | sed 's/:[0-9]*:[0-9]*:.*//' \
    | sort | uniq -c | sort -rn | head -20
