#!/bin/bash
# CodeOS QEMU launch script — boots the full Limine ISO
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
ISO="${SRC}/kernel/codeos-1-kernel.iso"
DISK="${SRC}/disk.img"

# Source color helpers
if [ -f "${SRC}/scripts/colors.sh" ]; then
    . "${SRC}/scripts/colors.sh"
else
    error()   { echo "ERROR: $1"; }
    success() { echo "OK: $1"; }
    info()    { echo "INFO: $1"; }
    step()    { echo "==> $1"; }
fi

# Default settings
MEM="12G"
CPUS="4"
SKIP_BUILD=0

# Parse arguments
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Boots the full Limine ISO in QEMU. The ISO is rebuilt fresh"
    echo "before booting so the boot test always exercises current source."
    echo ""
    echo "Options:"
    echo "  -m MEM      Memory (default: 12G)"
    echo "  -c CPUS     CPU cores (default: 4)"
    echo "  -s          Skip ISO rebuild, boot the existing ISO"
    echo "  -n          No display (serial only)"
    echo "  -h          Show this help"
}

NO_DISPLAY=0

while getopts "m:c:snh" opt; do
    case $opt in
        m) MEM="$OPTARG" ;;
        c) CPUS="$OPTARG" ;;
        s) SKIP_BUILD=1 ;;
        n) NO_DISPLAY=1 ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

# Build a fresh Limine ISO so the boot test always exercises current code.
# The kernel Makefile's `all` target does NOT produce the ISO, so build the
# ISO target explicitly; it depends on the kernel binary and its objects.
if [ "$SKIP_BUILD" -eq 0 ]; then
    step "Building fresh Limine ISO..."
    make -j kernel codeos-1-kernel.iso
else
    if [ ! -f "$ISO" ]; then
        error "ISO not found: $ISO (run without -s to build it)"
        exit 1
    fi
    info "Using existing ISO: $ISO"
fi

# Build QEMU command
if ! command -v qemu-system-x86_64 &>/dev/null; then
    error "qemu-system-x86_64 not found in PATH"
    exit 1
fi
QEMU="qemu-system-x86_64"
QEMU_ARGS=(
    -machine q35
    -vga std
    -global VGA.edid=on
    -m "$MEM"
    -smp "$CPUS"
    -serial stdio
)

# USB devices
QEMU_ARGS+=(
    -device usb-ehci,id=ehci
    -device usb-tablet
    -device usb-kbd
)

# Network
QEMU_ARGS+=(
    -netdev user,id=net0,hostfwd=tcp::7070-:80,hostfwd=tcp::2222-:22
    -device e1000,netdev=net0
)

# KVM acceleration
if [ -e /dev/kvm ]; then
    QEMU_ARGS+=(-accel kvm)
    info "KVM acceleration enabled"
else
    QEMU_ARGS+=(-accel tcg,thread=multi)
    info "KVM not available, using TCG emulation"
fi

# Display mode: headless with serial on stdio for clean boot-test capture
if [ "$NO_DISPLAY" -eq 1 ]; then
    QEMU_ARGS+=(-display none)
fi

# Disk
if [ -f "$DISK" ]; then
    QEMU_ARGS+=(-hda "$DISK")
fi

# Boot from ISO
step "Booting Limine ISO..."
QEMU_ARGS+=(-boot order=d -cdrom "$ISO")

info "Running: $QEMU ${QEMU_ARGS[*]}"
exec "$QEMU" "${QEMU_ARGS[@]}"
