#!/bin/sh
# Build a CodeOS ISO at a caller-chosen path, WITHOUT going through `make iso`.
#
# Why this exists: `codeos-1.0.iso` depends on `$(LIVE_DISK_IMG)` = `disk.img`,
# and `disk.img` is itself a generated artifact whose rule depends on
# `$(USER_ELF)`. So `make iso` re-runs `mke2fs -d` + `parted` over `disk.img`
# every time a userspace binary is newer -- i.e. after any ordinary `make all`.
# A boot test that shells out to `make iso` therefore silently replaces the
# image the OS was tested against, with a freshly built one. That is exactly
# the kind of "it is green and it is now testing something else" failure the
# AGENTS.md negative-control discipline exists to catch.
#
# This reproduces the Makefile's recipe verbatim, reading `disk.img` and
# writing only the ISO:
#
#     scratch_iso.sh [out.iso]        (default /tmp/codeos-scratch.iso)
#
# `kernel/disk.img` must be byte-identical afterwards; that is checked here
# rather than assumed, because the whole point is that it must not move.

set -eu

KERNEL_DIR=${KERNEL_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
OUT=${1:-/tmp/codeos-scratch.iso}
ISODIR=${ISODIR:-/tmp/codeos-scratch-isoroot}

cd "$KERNEL_DIR"

if [ ! -f codeos-1-kernel.bin ]; then
    echo "error: codeos-1-kernel.bin not built; run: make -C kernel all" >&2
    exit 1
fi

before=$(md5sum disk.img | cut -d' ' -f1)

rm -rf "$ISODIR"
cp -r bootloader/iso_root "$ISODIR"
cp codeos-1-kernel.bin          "$ISODIR/boot/"
cp bootloader/limine.conf       "$ISODIR/boot/"
cp bootloader/splash.png        "$ISODIR/boot/"
cp bootloader/limine-bios-cd.bin  "$ISODIR/"
cp bootloader/limine-uefi-cd.bin "$ISODIR/"
cp disk.img                    "$ISODIR/disk.img"

xorriso -as mkisofs -b limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    --efi-boot limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$ISODIR" -o "$OUT" >/dev/null 2>&1
./bootloader/limine-deploy bios-install "$OUT" >/dev/null 2>&1 \
    || { echo "error: limine-deploy failed" >&2; exit 1; }

after=$(md5sum disk.img | cut -d' ' -f1)
if [ "$before" != "$after" ]; then
    echo "error: kernel/disk.img changed ($before -> $after)" >&2
    exit 1
fi

echo "scratch ISO: $OUT (kernel/disk.img unchanged, md5 ${before})"
