#!/bin/bash
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
ISO="${SRC}/kernel/codeos-1-kernel.iso"
MEM="4G"

if [ ! -f "$ISO" ]; then
    echo "ERROR: ISO not found at $ISO"
    exit 1
fi

echo "Starting QEMU with gdbstub on port 1234..."
echo "ISO: $ISO"

echo "Running: qemu-system-x86_64 -machine q35 -m $MEM -smp 5 -serial stdio -display none -no-reboot -netdev user,id=net0 -device e1000,netdev=net0 -boot order=d -cdrom $ISO -s -S"

echo "=== Waiting for connection (5 seconds) ==="
sleep 5

exec qemu-system-x86_64 -machine q35 -m $MEM -smp 5 -serial stdio -display none -no-reboot -netdev user,id=net0 -device e1000,netdev=net0 -boot order=d -cdrom "$ISO" -s -S
