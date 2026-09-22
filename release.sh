#!/bin/bash
# CodeOS release builder — copies artifacts to Downloads for GitHub upload
# Improvements: error handling, colors, version management, checksums
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="${SRC}/kernel"
OUT_DIR="$HOME/Downloads"
LOGFILE="${SRC}/release.log"

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
VERSION="dev"
if [ -f "${SRC}/version" ]; then
    . "${SRC}/version"
    VERSION="${CODEOS_VERSION:-dev}"
fi

ARCHIVE="CodeOS-${VERSION}"
step "Preparing release: ${ARCHIVE}"

# Verify build artifacts exist
if [ ! -f "${KERNEL_DIR}/codeos-1-kernel.iso" ]; then
    error "ISO not found — run ./build.sh first"
    exit 1
fi
if [ ! -f "${KERNEL_DIR}/codeos-1-kernel.bin" ]; then
    error "Kernel binary not found — run ./build.sh first"
    exit 1
fi

# Create release directory
mkdir -p "${OUT_DIR}/${ARCHIVE}"

# Copy artifacts with architecture suffix
cp "${KERNEL_DIR}/codeos-1-kernel.iso" "${OUT_DIR}/${ARCHIVE}/CodeOS-${VERSION}-x86_64.iso"
cp "${KERNEL_DIR}/codeos-1-kernel.bin"  "${OUT_DIR}/${ARCHIVE}/CodeOS-${VERSION}-x86_64.bin"

# Generate checksums
step "Generating checksums..."
cd "${OUT_DIR}/${ARCHIVE}"
sha256sum *.iso *.bin > SHA256SUMS 2>/dev/null || true
info "Checksums saved to SHA256SUMS"

# Create tarball
cd "${OUT_DIR}" && tar czf "${ARCHIVE}.tar.gz" "${ARCHIVE}"
rm -rf "${OUT_DIR}/${ARCHIVE}"

# Done
success "Release ready: ${OUT_DIR}/${ARCHIVE}.tar.gz"
ls -lh "${OUT_DIR}/${ARCHIVE}.tar.gz"
