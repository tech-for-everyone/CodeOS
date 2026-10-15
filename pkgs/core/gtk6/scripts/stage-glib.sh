#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Stage the glib port into gtk6's sysroot.
#
# scripts/build-glib.sh builds the libraries; this script puts them, and the
# headers that go with them, where the rest of the package expects to find
# them.  It replaces the manual copy the plan used to describe -- that copy had
# drifted, and was missing glib/deprecated/*.h, which glib.h includes
# unconditionally.
#
#   sysroot/usr/include/{glib,gobject,gio,gmodule}   source + generated headers
#   sysroot/usr/lib/lib{glib,gobject,gio,gmodule,gthread}-2.0.a
#   sysroot/usr/lib/pkgconfig/*.pc                   prefix rewritten to the sysroot
#
# Usage: scripts/stage-glib.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

SRC="subprojects/glib"
BUILD="build/glib"
INCDIR="sysroot/usr/include"
LIBDIR="sysroot/usr/lib"

if [[ ! -f "$BUILD/glib/libglib-2.0.a" ]]; then
    echo "stage-glib: $BUILD/glib/libglib-2.0.a missing; run scripts/build-glib.sh first" >&2
    exit 1
fi

echo "==> headers -> $INCDIR"
for d in glib gobject gio gmodule; do
    rm -rf "$INCDIR/$d"
    mkdir -p "$INCDIR/$d"
done

# Source headers, keeping the directory layout (deprecated/, libcharset/, ...).
( cd "$SRC" && find glib gobject gio gmodule -name '*.h' \
      -not -path '*/tests/*' -not -path '*/glib-mirroring-tab/*' -print0 ) \
    | ( cd "$SRC" && xargs -0 cp --parents -f -t "$HERE/$INCDIR" )

# Generated headers win over any same-named source header.
for d in glib gobject gio gmodule; do
    if compgen -G "$BUILD/$d/*.h" > /dev/null; then
        cp -f "$BUILD"/$d/*.h "$INCDIR/$d/"
    fi
done

echo "==> libraries -> $LIBDIR"
mkdir -p "$LIBDIR"
for a in glib/libglib-2.0.a gobject/libgobject-2.0.a gio/libgio-2.0.a \
         gmodule/libgmodule-2.0.a gthread/libgthread-2.0.a; do
    cp -f "$BUILD/$a" "$LIBDIR/"
done

echo "==> pkg-config -> $LIBDIR/pkgconfig"
mkdir -p "$LIBDIR/pkgconfig"
for pc in glib-2.0 gobject-2.0 gio-2.0 gio-unix-2.0 gmodule-2.0 \
          gmodule-no-export-2.0 gthread-2.0; do
    [[ -f "$BUILD/meson-private/$pc.pc" ]] || continue
    sed -e "s:^prefix=.*:prefix=$HERE/sysroot/usr:" \
        "$BUILD/meson-private/$pc.pc" > "$LIBDIR/pkgconfig/$pc.pc"
done

echo "==> staged: $(find "$INCDIR" -name '*.h' | wc -l) headers, $(ls "$LIBDIR"/*.a | wc -l) archives"
