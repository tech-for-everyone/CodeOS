#!/usr/bin/env bash
# setup.sh — prepare everything needed to build and run CodeOS.
# Targets Arch-based systems (pacman + yay).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v pacman >/dev/null 2>&1; then
    echo "This setup script targets Arch-based systems (pacman) only." >&2
    echo "On Debian/Ubuntu install: build-essential qemu-system-x86 xorriso python3" >&2
    exit 1
fi

echo "Installing dependencies..."
sudo pacman -Syu --needed --noconfirm base-devel git cmake nasm make clang qemu-full
sudo pacman -Syu --needed --noconfirm libelf
sudo pacman -Syu --needed --noconfirm libelf-devel
sudo pacman -Syu --needed --noconfirm qemu-arch-extra
sudo pacman -Syu --needed --noconfirm qemu-guest-agent
sudo pacman -Syu --needed --noconfirm qemu-virtio
sudo pacman -Syu --needed --noconfirm qemu-guest-wifi
sudo pacman -Syu --needed --noconfirm grub      # grub-mkrescue, for `make iso`
sudo pacman -Syu --needed --noconfirm python    # generator scripts in kernel/
sudo pacman -Syu --needed --noconfirm ninja pkgconf glib2 pixman  # ncvm/ QEMU fork build

# yay (AUR helper), built only if missing
if ! command -v yay >/dev/null 2>&1; then
    echo "Building yay (AUR helper)..."
    [ -d yay/.git ] || git clone https://aur.archlinux.org/yay.git
    cd yay
    makepkg -si --noconfirm
    cd ..
fi

yay -Syu --needed --noconfirm rustup
yay -Syu --needed --noconfirm rust-analyzer
yay -Syu --needed --noconfirm llvm
yay -Syu --needed --noconfirm lld
yay -Syu --needed --noconfirm x86_64-elf-g++ x86_64-elf-gcc   # cross toolchain

rustup default stable
rustup update

# The kernel build pins a specific Rust toolchain (kernel/Makefile:
# RUST_TOOLCHAIN) and compiles the Rust components for x86_64-unknown-none.
RUST_PINNED=1.92.0-x86_64-unknown-linux-gnu
echo "Installing pinned Rust toolchain ($RUST_PINNED)..."
rustup toolchain install "$RUST_PINNED"
rustup target add x86_64-unknown-none --toolchain "$RUST_PINNED"

echo "Dependencies installed successfully."

# CodeOS source
if [ -d CodeOS/.git ]; then
    echo "Updating existing CodeOS checkout..."
    git -C CodeOS pull --ff-only
    cd CodeOS
else
    echo "Cloning CodeOS repository..."
    git clone https://github.com/tech-for-everyone/CodeOS
    cd CodeOS
fi

echo "Compiling CodeOS and dependencies..."
make -j"$(nproc)"

echo "CodeOS compiled successfully."
echo "Boot it with:"
echo "  ./run.sh                                   # builds ISO + runs QEMU"
echo "  qemu-system-x86_64 -cdrom kernel/codeos-1-kernel.iso"

# ── CodeOS packages (individual pkgs from the CodeOS-Comunity org) ─────────
# Every repo in github.com/CodeOS-Comunity is an installable CodeOS package:
# Fetch (the native package manager) treats the whole org as its online
# registry (api.github.com/orgs/CodeOS-Comunity/repos), so each repo added
# there instantly becomes a `fetch -S <name>` package inside CodeOS.
# Here we clone the current org repos into a host workspace next to the
# checkout, and build + sync-registry the Fetch tool natively.
PKGS_DIR="${PKGS_DIR:-$SCRIPT_DIR/CodeOS-pkgs}"
echo "Cloning CodeOS community packages into $PKGS_DIR/ ..."
mkdir -p "$PKGS_DIR"
for repo in OpenWeb CSL NetBeam Fetch Ziggy HyperDE; do
    if [ -d "$PKGS_DIR/$repo/.git" ]; then
        git -C "$PKGS_DIR/$repo" pull --ff-only -q
    else
        git clone -q --depth 1 "https://github.com/CodeOS-Comunity/$repo" "$PKGS_DIR/$repo"
    fi
    echo "  ✓ $repo"
done

echo "Building Fetch package manager (host tool)..."
cargo build -q --release --manifest-path "$PKGS_DIR/Fetch/Cargo.toml"
FETCH_BIN="$PKGS_DIR/Fetch/target/release/fetch"
echo "Syncing package registry from the CodeOS-Comunity org..."
"$FETCH_BIN" -Sy || true
echo "Available packages:"
"$FETCH_BIN" -Sl || true
echo "Packages ready in $PKGS_DIR/ — add new repos to the CodeOS-Comunity"
echo "org and they appear as packages automatically."