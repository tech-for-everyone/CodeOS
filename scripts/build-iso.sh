#!/bin/bash
# Build the CodeOS bootable ISO
set -e

cd "$(dirname "$0")/../kernel"
make codeos-1.0.iso
cp codeos-1.0.iso ../codeos-1.0.iso
echo "ISO built: codeos-1.0.iso"
