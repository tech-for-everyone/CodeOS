#!/bin/bash
# Build the CodeOS kernel
set -e

cd "$(dirname "$0")/../kernel"
make clean && make all
echo "Kernel built: codeos-1-kernel.bin"
