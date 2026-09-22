#!/bin/bash
# CodeOS build — compiles kernel and outputs clean build to ~/codeos
# Improvements: error handling, color output, logging, version display
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD="$SRC/kernel"
OUT="$HOME/codeos"
LOGFILE="${SRC}/build.log"

# Source color helpers
if [ -f "${SRC}/scripts/colors.sh" ]; then
    . "${SRC}/scripts/colors.sh"
else
    error()   { echo "ERROR: $1"; }
    success() { echo "OK: $1"; }
    info()    { echo "INFO: $1"; }
    step()    { echo "==> $1"; }
fi

# Source version info
if [ -f "${SRC}/version" ]; then
    . "${SRC}/version"
    info "CodeOS ${CODEOS_VERSION:-dev} (build ${CODEOS_BUILD:-0})"
fi

# Validate build environment
step "Checking build environment..."
if ! command -v make &>/dev/null; then
    error "make not found — install build tools"
    exit 1
fi
if ! command -v x86_64-elf-gcc &>/dev/null && ! command -v gcc &>/dev/null; then
    error "No cross-compiler found (x86_64-elf-gcc or gcc)"
    exit 1
fi
if ! command -v xorriso &>/dev/null; then
    warn "xorriso not found — ISO creation may fail"
fi

# Build kernel
step "Building kernel..."
MAKE_JOBS=$(nproc 2>/dev/null || echo 4)
if ! make -C "$BUILD" codeos-1-kernel.iso -j"$MAKE_JOBS" 2>&1 | tee "$LOGFILE"; then
    error "Kernel build failed — see $LOGFILE"
    exit 1
fi

# Copy artifacts
step "Copying build artifacts..."
mkdir -p "$OUT"
cp "$BUILD/codeos-1-kernel.bin"  "$OUT/codeos.bin"
cp "$BUILD/codeos-1-kernel.iso" "$OUT/codeos.iso"
cp "$BUILD/bootloader/limine.conf"      "$OUT/"
cp "$BUILD/bootloader/limine-bios.sys"  "$OUT/"
cp "$BUILD/bootloader/limine-deploy"    "$OUT/"

# Done
success "Build complete: $OUT"
ls -lh "$OUT/"
