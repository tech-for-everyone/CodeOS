#!/usr/bin/env bash
#
# CodeOS installer — install the OS to a bootable disk (partitioned HDD image
# or a real block device).  Unlike the live ISO (read-only CD boot), this writes
# a persistent install booted straight from the BIOS boot order.
#
# The kernel is self-contained (rootfs is embedded at build time via
# initramfs_files.c), so the boot partition only needs:
#   /boot/codeos-1-kernel.bin   (the multiboot2 kernel)
#   /boot/limine.conf           (Limine boot menu)
#   /limine-bios.sys            (Limine BIOS stage 2/3)
# The MBR bootstrap is written by `limine-deploy bios-install`.
#
# The boot partition is FAT32 (Limine's most reliably readable BIOS driver).
# Population uses mtools, so the image path needs no root or mount.
#
# Usage:
#   ./scripts/install-codesos.sh make-image [SIZE_MB] [output.img]
#       Build a bootable disk *image* (no root needed).  Boot in QEMU via
#       `make run-installed`.
#
#   ./scripts/install-codesos.sh install <device> [--force]
#       Install onto a real block device (e.g. /dev/sdb).  Needs sudo.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KERNEL_DIR="$SRC_ROOT/kernel"
KERNEL_BIN="$KERNEL_DIR/codeos-1-kernel.bin"
BL_DIR="$KERNEL_DIR/bootloader"
LIMINE_DEPLOY="$BL_DIR/limine-deploy"
LIMINE_BIOS_SYS="$BL_DIR/limine-bios.sys"

PART_START_SECTOR=2048          # 1 MiB alignment
PART_OFFSET=$((PART_START_SECTOR * 512))
MB=$((1024 * 1024))

if [ -t 1 ]; then
  C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YEL=$'\033[33m'; C_BLU=$'\033[34m'; C_RST=$'\033[0m'
else
  C_RED=''; C_GRN=''; C_YEL=''; C_BLU=''; C_RST=''
fi
err()  { printf '%sERROR:%s %s\n' "$C_RED" "$C_RST" "$*" >&2; }
info() { printf '%s==>%03s %s\n' "$C_BLU" "$C_RST" "$*"; }
ok()   { printf '%sOK:%s %s\n' "$C_GRN" "$C_RST" "$*"; }
die()  { err "$*"; exit 1; }

usage() {
  sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

require_tools() {
  for t in parted mformat mmd mcopy dd "$LIMINE_DEPLOY"; do
    command -v "$t" >/dev/null 2>&1 || die "required tool not found: $t"
  done
  [ -x "$LIMINE_DEPLOY" ] || die "limine-deploy not executable: $LIMINE_DEPLOY"
  [ -f "$LIMINE_BIOS_SYS" ] || die "limine-bios.sys not found: $LIMINE_BIOS_SYS"
}

ensure_kernel() {
  if [ ! -x "$KERNEL_BIN" ]; then
    info "Kernel binary missing — building kernel (large build)..."
    make -C "$KERNEL_DIR" all
  fi
  [ -x "$KERNEL_BIN" ] || die "kernel binary not found at $KERNEL_BIN"
}

# Write the limine.conf used on the boot partition.
write_conf() {
  local conf="$1"
  cat > "$conf" <<'EOF'
timeout: 5
serial: yes
verbose: yes
no_early_emsgs: yes

/CodeOS
    protocol: limine
    kernel_path: boot():/boot/codeos-1-kernel.bin
EOF
}

# Populate a FAT32 volume identified by an mtools drive spec `$drv`
# (a flat volume image, or a partition device node). Caller mformat'd it.
# Optional `$su` prefixes commands with sudo (for real devices).
fat_populate() {
  local drv="$1" conf="$2"; local su="${3:-}"
  info "   /boot ..."
  mmd    $su -i "$drv" ::/boot
  mcopy  $su -i "$drv" "$KERNEL_BIN" ::/boot/codeos-1-kernel.bin
  mcopy  $su -i "$drv" "$conf"     ::/boot/limine.conf
  mcopy  $su -i "$drv" "$LIMINE_BIOS_SYS" ::/limine-bios.sys
  info "Verify partition root:"
  mdir $su -i "$drv" :: 2>/dev/null | sed 's/^/    /'
  mdir $su -i "$drv" ::/boot 2>/dev/null | sed 's/^/    /'
}

# ──────────────────────────────────────────────────────────────
#  Image path (rootless): standalone FAT volume, then copy into partition slot.
# ──────────────────────────────────────────────────────────────
fat_seed_image() {
  local img="$1"
  local img_bytes p1_bytes p1img conf
  img_bytes=$(stat -c %s "$img")
  p1_bytes=$(( img_bytes - PART_OFFSET ))
  p1img="$(mktemp)"
  truncate -s "$p1_bytes" "$p1img"   # standalone FAT volume, sized to the partition
  conf="$(mktemp)"

  info "Formatting partition 1 as FAT32 (standalone volume, mtools)..."
  mformat -i "$p1img" -F -v CODEOS ::

  info "Copying kernel + limine config + stages into the partition..."
  write_conf "$conf"
  fat_populate "$p1img" "$conf"

  info "Writing the FAT partition into $img at 1MiB offset..."
  dd if="$p1img" of="$img" bs=512 seek=$PART_START_SECTOR conv=notrunc status=none
  rm -f "$p1img" "$conf"
}

# ──────────────────────────────────────────────────────────────
#  Device path (needs sudo): format the partition node directly.
# ──────────────────────────────────────────────────────────────
fat_seed_device() {
  local partdev="$1" conf
  conf="$(mktemp)"
  info "Formatting $partdev as FAT32 (sudo, mtools)..."
  sudo mformat -i "$partdev" -F -v CODEOS ::
  info "Copying kernel + limine config + stages (sudo)..."
  write_conf "$conf"
  fat_populate "$partdev" "$conf" sudo
  rm -f "$conf"
}

# ──────────────────────────────────────────────────────────────
#  make-image
# ──────────────────────────────────────────────────────────────
cmd_make_image() {
  local size_mb="${1:-128}"
  local out_img="${2:-$SRC_ROOT/codeos-installed.img}"
  local kbytes kbytes_min

  ensure_kernel
  require_tools
  kbytes=$(( ( $(stat -c %s "$KERNEL_BIN") + 1024 ) / 1024 ))
  kbytes_min=$(( size_mb * 1024 * 9 / 10 ))
  info "kernel size: $((kbytes/1024)) MB; image size: ${size_mb} MB"
  [ "$kbytes" -lt "$kbytes_min" ] || die "image too small: ${size_mb}MB for a $((${kbytes}/1024))MB kernel"

  info "Creating blank image $out_img (${size_mb}MB)..."
  truncate -s 0 "$out_img"
  truncate -s $((size_mb * MB)) "$out_img"

  info "Partitioning (msdos label, 1MiB-aligned FAT32 partition 1)..."
  parted -s "$out_img" mklabel msdos
  parted -s "$out_img" mkpart primary fat32 ${PART_START_SECTOR}s 100%
  parted -s "$out_img" set 1 boot on

  fat_seed_image "$out_img"

  info "Installing Limine BIOS boot (MBR + stages)..."
  "$LIMINE_DEPLOY" bios-install "$out_img"

  ok "Installed image ready: $out_img"
  info "Boot it with:  make run-installed   (or qemu-system-x86_64 -drive file=$out_img,format=raw,if=ide -boot order=c)"
}

# ──────────────────────────────────────────────────────────────
#  install <device>
# ──────────────────────────────────────────────────────────────
partition_for_device() {
  local dev="$1"
  case "$dev" in
    /dev/nvme*|/dev/mmcblk*) echo "${dev}p1" ;;
    *) echo "${dev}1" ;;
  esac
}

cmd_install() {
  local dev="$1"; shift
  local force=0
  while [ $# -gt 0 ]; do
    case "$1" in --force) force=1 ;; *) die "unknown option: $1" ;; esac
    shift
  done

  [ -b "$dev" ] || die "$dev is not a block device"
  local part; part="$(partition_for_device "$dev")"

  local model="unknown"
  if command -v lsblk >/dev/null 2>&1; then
    model="$(lsblk -ndo rm "$dev" 2>/dev/null || echo 1)"
  fi
  if [ "$force" -ne 1 ]; then
    case "$dev" in
      /dev/sda|/dev/nvme0n1|/dev/mmcblk0)
        die "$dev looks like a system disk — pass --force to override" ;;
    esac
    if [ "$model" != "1" ]; then
      die "$dev is not marked removable — pass --force to override (DANGEROUS)"
    fi
    printf '%sWARNING:%s About to destroy ALL data on %s\n' "$C_YEL" "$C_RST" "$dev"
    read -r -p "Type the device path again to confirm: " confirm
    [ "$confirm" = "$dev" ] || die "confirmation mismatch — aborting"
  fi

  ensure_kernel
  require_tools

  info "Partitioning $dev (msdos label, FAT32 boot partition)..."
  sudo parted -s "$dev" mklabel msdos
  sudo parted -s "$dev" mkpart primary fat32 1MiB 100%
  sudo parted -s "$dev" set 1 boot on

  fat_seed_device "$part"

  info "Installing Limine BIOS boot to $dev (sudo)..."
  sudo "$LIMINE_DEPLOY" bios-install "$dev"

  ok "Installation complete. Reboot, select $dev in the BIOS boot order."
}

main() {
  [ $# -lt 1 ] && usage
  case "$1" in
    make-image) shift; cmd_make_image "${1:-128}" "${2:-$SRC_ROOT/codeos-installed.img}" ;;
    install)
      shift
      [ $# -ge 1 ] || die "install: missing <device>"
      cmd_install "$1" "${@:2}" ;;
    -h|--help|help) usage ;;
    *) usage ;;
  esac
}

main "$@"
