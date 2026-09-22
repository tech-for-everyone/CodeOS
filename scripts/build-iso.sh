#!/bin/bash
# Build the CodeOS bootable ISO
set -e

cd "$(dirname "$0")/../kernel"
make codeos-1-kernel.iso
cp codeos-1-kernel.iso ../codeos-1-kernel.iso
echo "ISO built: codeos-1-kernel.iso"
