#!/bin/sh
# Print a linker command that can emit x86_64 ELF objects.
#
# The build used to fall back to the host `ld` whenever `x86_64-elf-ld` was
# missing.  That works on an x86_64 host, but on aarch64 ( phones, Apple
# Silicon, Termux, CI runners ) it fails with "target elf64-x86-64 not found"
# even though a perfectly good x86_64 linker is already installed: LLVM's lld,
# shipped with clang or with rustup as `gcc-ld/ld.lld`.
#
# Preference order:
#   1. x86_64-elf-ld      - GNU cross linker (needed for the prebuilt Qt TLS)
#   2. ld.lld             - from PATH
#   3. rustup's ld.lld    - same binary, not on PATH on minimal installs
#   4. host ld            - only when it advertises elf_x86_64 support
#
# Emitting a command (not just a path) matters for the last case: a multilib
# host ld on a non-x86 machine needs `-m elf_x86_64`, while a native x86_64
# host ld must be left alone.  Callers use `$(firstword $(LD))` when probing
# the command with `command -v`.

set -eu

if [ "${1:-x86_64}" != "x86_64" ]; then
    echo "find_x86_64_ld.sh: only the x86_64 target is supported" >&2
    exit 1
fi

for candidate in x86_64-elf-ld ld.lld; do
    if command -v "$candidate" >/dev/null 2>&1; then
        printf '%s\n' "$candidate"
        exit 0
    fi
done

# rustup installs lld under its sysroot; `gcc-ld/ld.lld` has the right argv[0]
# (plain `rust-lld` is the generic driver and refuses to link).  The newest
# toolchain wins so the result tracks `rustup default`.
for candidate in $(find "${HOME:-/root}/.rustup/toolchains" \
                        -path '*/gcc-ld/ld.lld' -type f 2>/dev/null | sort -r); do
    if "$candidate" --version >/dev/null 2>&1; then
        printf '%s\n' "$candidate"
        exit 0
    fi
done

if command -v ld >/dev/null 2>&1 && ld -V 2>&1 | grep -q elf_x86_64; then
    case $(uname -m) in
        x86_64|amd64) printf '%s\n' ld ;;
        *)            printf '%s\n' 'ld -m elf_x86_64' ;;
    esac
    exit 0
fi

echo "error: no linker can emit x86_64 ELF (install ld.lld or x86_64-elf-binutils)" >&2
exit 1
