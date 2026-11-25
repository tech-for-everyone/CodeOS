#!/bin/bash
# CodeOS QEMU launch script.
#
# Asks which architecture to build and boot (x86_64 or arm64) unless one is
# given with -a or CODEOS_ARCH.  x86_64 builds the Limine ISO and boots the
# full desktop; arm64 builds the CodeOS ARM64 QEMU `virt` kernel and boots it
# with ramfb + virtio input devices.  `-z` selects the Zircon mobile demo.
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
ISO="${SRC}/kernel/codeos-1.0.iso"
X86_KERNEL="${SRC}/kernel/codeos-1-kernel.bin"
ARM_KERNEL="${SRC}/kernel/codeos-1-kernel-arm64.bin"
ZIRCON_KERNEL="${SRC}/kernel/codeos-1-kernel-arm64-zircon.bin"
DISK="${SRC}/disk.img"

JOBS="${JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

# Source color helpers
if [ -f "${SRC}/scripts/colors.sh" ]; then
    . "${SRC}/scripts/colors.sh"
else
    error()   { echo "ERROR: $1"; }
    success() { echo "OK: $1"; }
    info()    { echo "INFO: $1"; }
    step()    { echo "==> $1"; }
fi

# Default settings (MEM is filled in once the architecture is known)
MEM=""
CPUS="4"
SKIP_BUILD=0
NO_DISPLAY=0
ARCH_OPT=""
ARCH=""
ARM64_UI="${ARM64_UI:-codeos}"

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Boots CodeOS in QEMU. If no architecture is given, an interactive"
    echo "prompt asks whether to build x86_64 or arm64."
    echo ""
    echo "Options:"
    echo "  -a ARCH     x86_64|arm64 (aarch64 accepted; skips the prompt)"
    echo "  -m MEM      Memory (default: 12G for x86_64, 512M for arm64)"
    echo "  -c CPUS     CPU cores (default: 4)"
    echo "  -s          Skip the rebuild, boot the existing image"
    echo "  -n          No display (serial only)"
    echo "  -z          arm64 only: build the Zircon mobile demo instead of CodeOS"
    echo "  -h          Show this help"
    echo ""
    echo "Environment:"
    echo "  CODEOS_ARCH    Same as -a, used when stdin is not a terminal"
    echo "  ARM64_UI       codeos (default) or zircon for the arm64 image"
    echo "  JOBS           Parallel build jobs (default: detected CPU count)"
}

while getopts "a:m:c:snzh" opt; do
    case $opt in
        a) ARCH_OPT="$OPTARG" ;;
        m) MEM="$OPTARG" ;;
        c) CPUS="$OPTARG" ;;
        s) SKIP_BUILD=1 ;;
        n) NO_DISPLAY=1 ;;
        z) ARM64_UI="zircon" ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

# ── Architecture selection ──────────────────────────────────────────────
normalize_arch() {
    case "$1" in
        x86_64|x86-64|amd64|x64|x86) echo "x86_64" ;;
        arm64|aarch64|arm)           echo "arm64" ;;
        *)                           echo "" ;;
    esac
}

ask_arch() {
    echo ""
    echo "Which CodeOS architecture should be built and booted?"
    echo "  1) x86_64  - full kernel, Limine ISO, desktop + userspace"
    echo "  2) arm64   - CodeOS ARM64 kernel, serial + ramfb shell (use -z for Zircon demo)"
    echo ""
    local reply
    while true; do
        printf "Choice [1/2]: "
        if ! IFS= read -r reply; then
            echo
            info "No input available; defaulting to x86_64"
            ARCH="x86_64"
            return
        fi
        case "$reply" in
            1|"")     ARCH="x86_64"; break ;;
            2)        ARCH="arm64"; break ;;
            x86_64|aarch64|arm64)
                      ARCH="$(normalize_arch "$reply")"; break ;;
            *)        echo "Please enter 1 or 2 (or x86_64 / arm64)." ;;
        esac
    done
    echo ""
}

if [ -n "$ARCH_OPT" ]; then
    ARCH="$(normalize_arch "$ARCH_OPT")"
elif [ -n "${CODEOS_ARCH:-}" ]; then
    ARCH="$(normalize_arch "$CODEOS_ARCH")"
elif [ -t 0 ]; then
    ask_arch
else
    ARCH="x86_64"
    info "Non-interactive stdin; defaulting to x86_64 (use -a or CODEOS_ARCH)"
fi

if [ -z "$ARCH" ]; then
    error "Unknown architecture '${ARCH_OPT:-${CODEOS_ARCH:-}}' — use x86_64 or arm64"
    exit 1
fi
if [ "$ARCH" != "arm64" ] && [ "$ARM64_UI" = "zircon" ]; then
    info "-z only affects the arm64 image; ignoring it for x86_64"
fi

case "$ARM64_UI" in
    codeos|zircon) ;;
    *) error "Unknown ARM64_UI '$ARM64_UI' — use codeos or zircon"; exit 1 ;;
esac
if [ "$ARM64_UI" = "zircon" ]; then
    ARM_KERNEL="$ZIRCON_KERNEL"
fi

if [ -z "$MEM" ]; then
    if [ "$ARCH" = "arm64" ]; then MEM="512M"; else MEM="12G"; fi
fi

step "Target architecture: $ARCH (memory $MEM, $CPUS CPUs)"
if [ "$ARCH" = "arm64" ]; then
    info "ARM64 image flavour: $ARM64_UI"
fi

# ── Build ───────────────────────────────────────────────────────────────
if [ "$ARCH" = "arm64" ]; then
    if [ "$SKIP_BUILD" -eq 0 ]; then
        step "Building ARM64 kernel (ARCH=arm64, ARM64_UI=$ARM64_UI)..."
        make -C "${SRC}/kernel" ARCH=arm64 ARM64_UI="$ARM64_UI" all -j"$JOBS"
    else
        if [ ! -f "$ARM_KERNEL" ]; then
            error "ARM64 kernel not found: $ARM_KERNEL (run without -s to build it)"
            exit 1
        fi
        info "Using existing ARM64 kernel: $ARM_KERNEL"
    fi
else
    if [ "$SKIP_BUILD" -eq 0 ]; then
        step "Building fresh Limine ISO (x86_64)..."
        make -j"$JOBS" kernel codeos-1.0.iso
    else
        if [ ! -f "$ISO" ]; then
            error "ISO not found: $ISO (run without -s to build it)"
            exit 1
        fi
        info "Using existing ISO: $ISO"
    fi

    # The ncvm backend is an x86_64 in-guest VM feature and is exercised at
    # boot (ncvm_probe runs `ncvm --selftest`), so gate on its host checks.
    # The ARM64 image has no ncvm, so there is nothing to gate there.
    step "Running ncvm host checks..."
    make ncvm-check
fi

# ── QEMU ────────────────────────────────────────────────────────────────
if [ "$ARCH" = "arm64" ]; then
    if ! command -v qemu-system-aarch64 &>/dev/null; then
        error "qemu-system-aarch64 not found in PATH"
        exit 1
    fi
    QEMU="qemu-system-aarch64"
    QEMU_ARGS=(
        -machine virt
        -cpu max
        -m "$MEM"
        -smp "$CPUS"
        -serial stdio
        -accel tcg,thread=multi
        # QEMU's virtio-mmio devices default to the legacy (version 1) layout,
        # but arch/arm64/input.c speaks the modern (version 2) register ABI.
        # Without this global the devices exist yet the probe reports 0.
        -global virtio-mmio.force-legacy=off
        # ramfb is the framebuffer the ARM64 display driver publishes through
        # fw_cfg; the two virtio devices are what arch/arm64/input.c probes.
        -device ramfb
        -device virtio-tablet-device
        -device virtio-keyboard-device
        -kernel "$ARM_KERNEL"
    )
    info "ARM64 uses TCG emulation against the QEMU virt machine"

    if [ "$NO_DISPLAY" -eq 1 ]; then
        QEMU_ARGS+=(-display none)
    fi
else
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

    # The ISO carries a copy of disk.img, but the kernel cannot mount a file
    # inside the ISO — attach the real image explicitly.
    if [ -f "$DISK" ]; then
        QEMU_ARGS+=(-hda "$DISK")
    fi

    QEMU_ARGS+=(-boot order=d -cdrom "$ISO")
fi

info "Running: $QEMU ${QEMU_ARGS[*]}"
exec "$QEMU" "${QEMU_ARGS[@]}"
