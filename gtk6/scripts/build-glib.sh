#!/usr/bin/env bash
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
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

SYSROOT="$HERE/../qt6/sysroot/usr"
CC=x86_64-elf-gcc
CFLAGS=(-ffreestanding -fno-stack-protector -fno-PIC -mcmodel=large -O2
        -isystem /usr/include
        -isystem /usr/lib/gcc/x86_64-pc-linux-gnu/16/include)

mkdir -p deps/lib deps/src

# ── 1. Link-time shims for CodeOS libc gaps ──────────────────────────────
echo "==> building CodeOS shims"
$CC "${CFLAGS[@]}" -c deps/src/iconv_stub.c -o deps/src/iconv_stub.o
x86_64-elf-ar rcs deps/lib/libiconv.a deps/src/iconv_stub.o

rm -f deps/lib/libmathshim.a
for s in math_shim net_stub alloc_stub fs_stub; do
    $CC "${CFLAGS[@]}" -c "deps/src/$s.c" -o "deps/src/$s.o"
    x86_64-elf-ar rcs deps/lib/libmathshim.a "deps/src/$s.o"
done

# ── 2. Configure glib for the CodeOS target ──────────────────────────────
echo "==> configuring glib"
meson setup build/glib subprojects/glib \
    --cross-file codeos-glib.cross \
    --default-library=static \
    --wipe \
    -Dintrospection=disabled -Ddocumentation=false -Dtests=false \
    -Dinstalled_tests=false -Dman-pages=disabled -Ddtrace=disabled \
    -Dsystemtap=disabled -Dselinux=disabled -Dlibmount=disabled -Dxattr=false \
    -Dsysprof=disabled -Dlibelf=disabled -Dglib_debug=disabled -Dnls=disabled \
    -Dmultiarch=false -Dbsymbolic_functions=false

# ── 3. Build the libraries ───────────────────────────────────────────────
echo "==> building glib libraries"
ninja -C build/glib \
    glib/libglib-2.0.a \
    gobject/libgobject-2.0.a \
    gio/libgio-2.0.a \
    gmodule/libgmodule-2.0.a \
    gthread/libgthread-2.0.a

echo "==> done"
ls -lh build/glib/glib/libglib-2.0.a \
       build/glib/gobject/libgobject-2.0.a \
       build/glib/gio/libgio-2.0.a
