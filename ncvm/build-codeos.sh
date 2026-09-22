#!/usr/bin/env bash
# ncvm — CodeOS-minimal QEMU fork build
#
# Builds two minimal system emulators from QEMU v10.2.4:
#   bin/ncvm-x86_64    - q35 machine only (the CodeOS desktop target)
#   bin/ncvm-aarch64   - virt machine only (the CodeOS/Zircon ARM64 target)
#   bin/ncvm           - CodeOS VM runner wrapper (launches the VMs)
#
# Everything the binaries need (BIOS, EDK2 firmware, VGA ROMs, ...) is
# installed next to them under share/qemu so they run straight from bin/.
#
# Usage:
#   bash build-codeos.sh            # build both, refresh bin/ + share/qemu
#   bash build-codeos.sh x86_64     # build just the x86_64 target
#   bash build-codeos.sh aarch64    # build just the aarch64 target
#   bash build-codeos.sh install    # also copy to /usr/local/bin (+ data, wrapper)
#
# Env:
#   NCVM_QEMU_SRC   existing QEMU checkout to reuse (default: ./src/qemu).
#                   If unset and ./src/qemu is absent, clones QEMU v10.2.4
#                   from gitlab.com (depth-1 tag clone).
set -euo pipefail
cd "$(dirname "$0")"

QEMU_SRC="${NCVM_QEMU_SRC:-./src/qemu}"
QEMU_TAG="${NCVM_QEMU_TAG:-v10.2.4}"
JOBS="$(nproc)"
TARGET="${1:-all}"

# If this checkout itself is the QEMU source (ncvm overlay committed inside
# a QEMU tree), use it directly instead of a nested src/qemu clone.
if [ -f ./configure ] && [ -d ./.git ] && ! [ -d ./src/qemu ]; then
    QEMU_SRC="."
fi

have() { [ "$TARGET" = "$1" ] || [ "$TARGET" = "all" ]; return; }

# ── 1. Source: clone once, patch idempotently, install device configs ─────
bootstrap() {
    if [ ! -d "$QEMU_SRC/.git" ]; then
        echo "==> Cloning QEMU ${QEMU_TAG} into ${QEMU_SRC}/ ..."
        git clone --depth 1 --branch "$QEMU_TAG" \
            https://gitlab.com/qemu-project/qemu.git "$QEMU_SRC"
    fi
    # When run from inside the already-patched QEMU tree itself, the patch
    # and config steps are no-ops (they are already part of this checkout).
    if [ "$QEMU_SRC" != "." ]; then
        echo "==> Applying ncvm patches to ${QEMU_SRC}/ ..."
        (cd "$QEMU_SRC" && for p in "$OLDPWD"/patches/*.patch; do
            if git apply --check --reverse "$p" 2>/dev/null; then
                continue                      # already applied
            fi
            if git apply --check "$p" 2>/dev/null; then
                git apply "$p"
            else
                echo "    ! patch $p cannot be applied cleanly (skipping)" >&2
            fi
        done)
        echo "==> Installing device configs (codeos.mak) ..."
        mkdir -p "$QEMU_SRC/configs/devices/x86_64-softmmu" \
                 "$QEMU_SRC/configs/devices/aarch64-softmmu"
        cp configs/devices/x86_64-softmmu/codeos.mak \
           "$QEMU_SRC/configs/devices/x86_64-softmmu/"
        cp configs/devices/aarch64-softmmu/codeos.mak \
           "$QEMU_SRC/configs/devices/aarch64-softmmu/"
    fi
}

build_target() { # $1 = arch (x86_64|aarch64), $2 = target-list, $3 = device set
    local arch="$1" tlist="$2" devset="$3"
    if have "$arch"; then
        echo "==> Building ncvm-${arch} (${devset} only)"
        if [ ! -f "build-${arch}/build.ninja" ]; then
            (cd "build-${arch}" && "../$QEMU_SRC/configure" \
                --target-list="$tlist" \
                --without-default-devices \
                --with-devices-"$arch"="$devset" \
                --disable-werror \
                --prefix="$PWD/../install-${arch}")
        fi
        (cd "build-${arch}" && ninja -j"$JOBS" "qemu-system-$arch")
        (cd "build-${arch}" && ninja install)   # firmware/data -> install-<arch>/share/qemu
        mkdir -p bin
        cp "build-${arch}/qemu-system-${arch}" "bin/ncvm-${arch}"
        echo "==> ncvm-${arch} -> bin/ncvm-${arch}"
    fi
}

# the `ncvm` runner wrapper (CodeOS VM launcher) beside the binaries
install_runner() {
    if [ -f ./ncvm ]; then
        mkdir -p bin
        cp ./ncvm bin/ncvm
        chmod +x bin/ncvm
        echo "==> ncvm runner -> bin/ncvm"
    fi
}

if [ "$TARGET" = "install" ]; then
    # install eggs-mode: build both first, then put them + data in place
    TARGET=all
    bootstrap
    build_target x86_64   x86_64-softmmu   codeos
    build_target aarch64  aarch64-softmmu  codeos
    echo "==> Installing to /usr/local ..."
    sudo mkdir -p /usr/local/share/qemu
    sudo cp -r install-x86_64/share/qemu/. /usr/local/share/qemu/
    sudo cp bin/ncvm-x86_64 /usr/local/bin/
    sudo cp bin/ncvm-aarch64 /usr/local/bin/
    install_runner
    sudo cp bin/ncvm /usr/local/bin/
    echo "Installed: /usr/local/bin/ncvm-x86_64 /usr/local/bin/ncvm-aarch64 /usr/local/bin/ncvm"
    exit 0
fi

bootstrap
build_target x86_64   x86_64-softmmu   codeos
build_target aarch64  aarch64-softmmu  codeos
install_runner

# ── 2. Data dir so the copied binaries find BIOS/EDK2 from bin/ ────────────
# QEMU resolves its data dir relative to the executable (bin/../share/qemu);
# the x86 install carries the full firmware set (incl. EDK2 for aarch64).
if have x86_64 && [ ! -e share/qemu ]; then
    echo "==> Linking share/qemu -> install-x86_64/share/qemu"
    mkdir -p share
    ln -s "$PWD/install-x86_64/share/qemu" share/qemu
fi

echo "Done: $PWD/bin/ncvm-x86_64 $PWD/bin/ncvm-aarch64 $PWD/bin/ncvm"
echo "Try:  ./bin/ncvm           # boot CodeOS (x86_64, q35, ISO auto-detect)"