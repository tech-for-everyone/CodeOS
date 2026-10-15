#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the CodeOS port of glib (target: freestanding x86_64, CodeOS libc).
#
# Produces static archives under build/glib/ that the GTK-on-Qt6 toolkit links:
#   glib/libglib-2.0.a  gobject/libgobject-2.0.a  gio/libgio-2.0.a
#   gmodule/libgmodule-2.0.a  gthread/libgthread-2.0.a
#
# Only the libraries are built. glib's installed CLI tools are deliberately
# skipped: they are host programs and link host pcre2/libffi, which is
# meaningless for the freestanding target. Code generation for later phases
# uses the host's glib tools (glib-compile-resources/schemas) plus the Python
# scripts meson generates in build/glib/gobject/.
#
# glib no longer needs private link-time shims: the small set of libc entry
# points it needs (iconv, frexp/ldexp, aligned allocation, statfs, resolver
# and sockets) now live in CodeOS itself, in qt6/posixstubs/src/codeos_compat.c
# (part of libcodeos_posixstubs).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

# This package lives at pkgs/core/gtk6, so the shared Qt6 sysroot is three
# levels up at the repo root.
QT_SYSROOT="$(cd "$HERE/../../../qt6/sysroot/usr" && pwd)"

mkdir -p build

# ── 1. Materialise the cross-file (absolute paths, regenerated each build) ──
CROSS="$HERE/build/codeos-glib.cross"
sed -e "s|@HERE@|$HERE|g" -e "s|@QT_SYSROOT@|$QT_SYSROOT|g" \
    "$HERE/codeos-glib.cross.in" > "$CROSS"

# ── 2. Configure glib for the CodeOS target ──────────────────────────────
echo "==> configuring glib"
meson setup build/glib subprojects/glib \
    --cross-file "$CROSS" \
    --default-library=static \
    --wrap-mode=forcefallback \
    --wipe \
    -Dintrospection=disabled -Ddocumentation=false -Dtests=false \
    -Dinstalled_tests=false -Dman-pages=disabled -Ddtrace=disabled \
    -Dsystemtap=disabled -Dselinux=disabled -Dlibmount=disabled -Dxattr=false \
    -Dsysprof=disabled -Dlibelf=disabled -Dglib_debug=disabled -Dnls=disabled \
    -Dmultiarch=false -Dbsymbolic_functions=false

# ── 2. Build the libraries ───────────────────────────────────────────────
# The three libraries glib/gobject/gio reference but do not contain --
# pcre2 (glib regex), libffi (GObject closures) and zlib (gio) -- are forced
# to their bundled subprojects by --wrap-mode=forcefallback, so they are built
# for the CodeOS target rather than picked up from the host.
echo "==> building glib libraries"
ninja -C build/glib \
    glib/libglib-2.0.a \
    gobject/libgobject-2.0.a \
    gio/libgio-2.0.a \
    gmodule/libgmodule-2.0.a \
    gthread/libgthread-2.0.a \
    subprojects/pcre2-10.46/libpcre2-8.a \
    subprojects/libffi-3.5.2/src/libffi.a \
    subprojects/zlib-1.3.1/libz.a

echo "==> done"
ls -lh build/glib/glib/libglib-2.0.a \
       build/glib/gobject/libgobject-2.0.a \
       build/glib/gio/libgio-2.0.a \
       build/glib/subprojects/pcre2-10.46/libpcre2-8.a \
       build/glib/subprojects/libffi-3.5.2/src/libffi.a \
       build/glib/subprojects/zlib-1.3.1/libz.a

# ── 3. Stage headers + libraries into gtk6/sysroot ──────────────────────
"$HERE/scripts/stage-glib.sh"
