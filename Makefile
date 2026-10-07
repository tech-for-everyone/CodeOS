# CodeOS Root Makefile
# Define target architecture
ARCH ?= x86_64
export ARCH

# Version info
-include version

# Color output
CYAN  := \033[0;36m
GREEN := \033[0;32m
BOLD  := \033[1m
DIM   := \033[2m
RESET := \033[0m

# NetBeam packaging (see netbeam-pkg below). The keystore is *not* checked in:
# generate one with
#   keytool -genkeypair -keystore netbeam.jks -storetype PKCS12 \
#           -storepass netbeam -keypass netbeam -alias netbeam \
#           -keyalg RSA -keysize 2048 -validity 10000 \
#           -dname "CN=NetBeam, O=CodeOS"
NETBEAM_DIST         ?= pkgs/dist
NETBEAM_KEYSTORE     ?=
NETBEAM_KEY_ALIAS    ?= netbeam
NETBEAM_KEY_PASSWORD ?=

.PHONY: all check jengine-check codec-check codec-fuzz-check kernel iso clean run run-iso help
.PHONY: ncvm ncvm-build ncvm-run
.PHONY: netbeam-check netbeam-pkg
.PHONY: qt6 qt6-build qt6-install qt6-clean
.PHONY: qt6-download qt6-configure qt6-build-full qt6-all
.PHONY: icons icons-25d icons-dedup
.PHONY: xora-pack xora-inspect xora-unpack xora-install xora-audit xora-all
.PHONY: sdl3 sdl3-build sdl3-install sdl3-clean sdl3-demo sdl3-desktop

all: kernel iso
	@printf "\n$(CYAN)$(BOLD)Build complete!$(RESET)\n"

check:
	@printf "$(CYAN)$(BOLD)==> Checking build environment...$(RESET)\n"
	$(MAKE) -C kernel check
	$(MAKE) jengine-check
	$(MAKE) ncvm-check
	$(MAKE) codec-check

jengine-check:
	@printf "$(CYAN)$(BOLD)==> Checking Jengine...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -Werror -Iinclude src/jengine.c tests/jengine_test.c \
		-o /tmp/codeos-jengine-test
	@/tmp/codeos-jengine-test
	@printf "$(GREEN)Jengine checks passed$(RESET)\n"

# pkgs/core/ncvm/src/ncvm.c reaches the kernel through inline `int $0x80`
# stubs, so the only way to test it on the host is to shadow unistd.h with
# tests/codeos_shim.h and compile the real source against POSIX. CMD_DIR is
# overridden to a scratch dir so the wire protocol can be exercised without
# touching /tmp/crosvm-cmds. See AGENTS.md for what the shim does and does
# not model.
ncvm-check:
	@printf "$(CYAN)$(BOLD)==> Checking ncvm backend...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -Werror \
		-Ipkgs/core/ncvm/tests \
		-DCMD_DIR='"/tmp/ncvm-hosttest-cmds"' \
		pkgs/core/ncvm/tests/ncvm_test.c \
		-o /tmp/codeos-ncvm-test
	@/tmp/codeos-ncvm-test
	@printf "$(GREEN)ncvm checks passed$(RESET)\n"

# pkgs/core/codec is compiled into the kernel with -nostdinc -mno-sse, so the
# only way to run its decoders on the host is to compile the real sources
# against libc via -DCODEC_HOST_TEST (the switch lives in codec_alloc.h).
# tests/run.sh also builds three deliberately-broken variants and asserts that
# each one fails: without those, "0 failures" would not distinguish a working
# decoder from one that asserts nothing. See AGENTS.md for the negative-control
# discipline.
codec-check:
	@printf "$(CYAN)$(BOLD)==> Checking codec decoders...$(RESET)\n"
	@sh pkgs/core/codec/tests/run.sh

# Separate from codec-check because it needs a sanitizer-capable compiler and
# takes noticeably longer. Worth running before touching a decoder: the
# cross-media dispatch bug it found (codec_decode_image reaching the WAV
# decoder) was invisible to every fixture-based check.
codec-fuzz-check:
	@printf "$(CYAN)$(BOLD)==> Fuzzing codec decoders...$(RESET)\n"
	@sh pkgs/core/codec/tests/fuzz.sh

# pkgs/extra/netbeam/src/netbeam.c is a CodeOS userspace ELF, so it issues
# `int $0x80` with CodeOS syscall numbers and cannot run on Linux. The test
# includes the real source (with main renamed) against a POSIX model of
# those syscalls, which is what lets nb_selftest() and a loopback transfer
# actually execute. -DNB_TEST_* redirects the inbox and both ports: /NetBeam
# is not writable on the host, and 54918 may already be in use.
#
# What a host pass does NOT establish is listed in netbeam_shim.h -- the
# real 1024-byte send cap, the absent MSG_DONTWAIT, and the kernel VFS are
# all invisible here. The OS-side confirmation is the `netbeam selftest`
# marker line on the serial log during a real boot.
netbeam-check:
	@printf "$(CYAN)$(BOLD)==> Checking NetBeam protocol...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE \
		-Ipkgs/extra/netbeam/tests \
		-DNB_TEST_INBOX='"/tmp/netbeam-hosttest/NetBeam/Incoming"' \
		-DNB_TEST_TRANSFER_PORT=45918 \
		-DNB_TEST_DISCOVER_PORT=45917 \
		pkgs/extra/netbeam/tests/netbeam_host_test.c \
		-o /tmp/codeos-netbeam-test
	@/tmp/codeos-netbeam-test
	@/tmp/codeos-netbeam-test loopback
	@printf "$(GREEN)netbeam checks passed$(RESET)\n"

# Build all three package formats from the one built ELF and verify each with
# a reader that shares none of the writer's code: `ar t` + `md5sum -c` for the
# deb, `xora inspect` for the xora, `jarsigner -verify` for the apk. Signing
# needs a keystore; without NETBEAM_KEYSTORE the APK is built unsigned and the
# signature check is reported as skipped rather than silently passing.
netbeam-pkg: kernel/userspace/netbeam
	@printf "$(CYAN)$(BOLD)==> Packaging NetBeam...$(RESET)\n"
	@if [ -z "$(NETBEAM_KEYSTORE)" ]; then \
		echo "netbeam-pkg: NETBEAM_KEYSTORE is unset; building an unsigned APK"; \
		python3 scripts/mkpkg_netbeam.py --out $(NETBEAM_DIST); \
	else \
		python3 scripts/mkpkg_netbeam.py --out $(NETBEAM_DIST) \
			--keystore $(NETBEAM_KEYSTORE) --key-alias $(NETBEAM_KEY_ALIAS) \
			--password $(NETBEAM_KEY_PASSWORD); \
	fi
	@printf "$(GREEN)netbeam packages built in $(NETBEAM_DIST)$(RESET)\n"

# ── Kernel ────────────────────────────────────────────────────────
kernel:
	@printf "$(CYAN)$(BOLD)==> Building kernel...$(RESET)\n"
	$(MAKE) -C kernel all
	@printf "$(GREEN)Kernel built successfully$(RESET)\n"

iso: codeos-1.0.iso

codeos-1.0.iso:
	@printf "$(CYAN)$(BOLD)==> Creating ISO...$(RESET)\n"
	$(MAKE) -C kernel codeos-1.0.iso
	cp kernel/codeos-1.0.iso codeos-1.0.iso
	@printf "$(GREEN)ISO ready: codeos-1.0.iso$(RESET)\n"

clean: clean-qt6
	$(MAKE) -C kernel clean
	rm -f codeos-1-kernel.bin codeos-1.0.iso
	@printf "$(GREEN)Clean complete$(RESET)\n"

run: kernel
	$(MAKE) -C kernel run

run-iso: iso
	$(MAKE) -C kernel run-iso

# ── ncvm ──────────────────────────────────────────────────────────────
# ncvm/ is CodeOS's own QEMU fork (host-side VM backend + runner). The
# delta project builds from ncvm/ (clones QEMU v10.2.4 into ncvm/src/qemu
# on first run), producing bin/ncvm-* plus the firmware data + runner.
ncvm ncvm-build:
	@printf "$(CYAN)$(BOLD)==> Building ncvm (CodeOS QEMU fork)...$(RESET)\n"
	bash ncvm/build-codeos.sh
	@printf "$(GREEN)ncvm built: ncvm/bin/ncvm-x86_64, ncvm/bin/ncvm-aarch64, ncvm/bin/ncvm$(RESET)\n"

ncvm-run: iso ncvm
	bash ncvm/bin/ncvm

# ── Qt6 Support Libraries ────────────────────────────────────────
# These are the POSIX stubs, platform plugin, font engine, etc.
# that Qt6 needs to cross-compile for CodeOS.

qt6: qt6-build

qt6-build:
	@printf "$(CYAN)$(BOLD)==> Building Qt6 support libraries...$(RESET)\n"
	mkdir -p qt6/support-build
	cd qt6/support-build && cmake .. -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=../cmake/codeos-toolchain.cmake
	cd qt6/support-build && $(MAKE) -j$$(nproc)
	@printf "$(GREEN)Qt6 support libraries ready$(RESET)\n"

qt6-install: qt6-build
	@printf "$(CYAN)$(BOLD)==> Installing Qt6 support to sysroot...$(RESET)\n"
	DESTDIR=$(PWD)/qt6/sysroot $(MAKE) -C qt6/support-build install
	@printf "$(GREEN)Qt6 support installed$(RESET)\n"

qt6-clean:
	rm -rf qt6/support-build
	@printf "$(GREEN)Qt6 build cleaned$(RESET)\n"

clean-qt6:
	rm -rf qt6/support-build

# ── Qt6 Cross-Compilation ────────────────────────────────────────
# Downloads Qt6 source and cross-compiles it for CodeOS.
# This requires x86_64-elf-gcc, cmake, python3, and ~2GB disk.

qt6-download:
	@printf "$(CYAN)$(BOLD)==> Downloading Qt6 source...$(RESET)\n"
	cd qt6 && ./build-qt6.sh download
	@printf "$(GREEN)Qt6 downloaded$(RESET)\n"

qt6-configure:
	@printf "$(CYAN)$(BOLD)==> Configuring Qt6 for CodeOS...$(RESET)\n"
	cd qt6 && ./build-qt6.sh mkspec
	cd qt6 && ./build-qt6.sh configure
	@printf "$(GREEN)Qt6 configured$(RESET)\n"

qt6-build-full:
	@printf "$(CYAN)$(BOLD)==> Building Qt6 from source (this takes a while)...$(RESET)\n"
	cd qt6 && ./build-qt6.sh all
	@printf "$(GREEN)Qt6 built$(RESET)\n"

qt6-all: qt6 qt6-download qt6-configure qt6-build-full
	@printf "$(GREEN)Qt6 full build complete$(RESET)\n"

# ── SDL3 Support ──────────────────────────────────────────

sdl3: sdl3-build sdl3-install

sdl3-build:
	@printf "$(CYAN)$(BOLD)==> Building SDL3 for CodeOS...$(RESET)\n"
	cd qt6 && ./build-sdl3.sh
	@printf "$(GREEN)SDL3 built$(RESET)\n"

sdl3-install: sdl3-build
	@printf "$(CYAN)$(BOLD)==> Installing SDL3 to sysroot...$(RESET)\n"
	@# SDL3 is installed by build-sdl3.sh via cmake install
	@printf "$(GREEN)SDL3 installed$(RESET)\n"

sdl3-demo: sdl3-install
	@printf "$(CYAN)$(BOLD)==> Building SDL3 demo...$(RESET)\n"
	mkdir -p qt6/sdl3-demo-build
	cd qt6/sdl3-demo-build && cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/codeos-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
	cd qt6/sdl3-demo-build && $(MAKE) -j$$(nproc)
	@printf "$(GREEN)SDL3 demo built$(RESET)\n"

sdl3-desktop: sdl3-install
	@printf "$(CYAN)$(BOLD)==> Building SDL3 desktop...$(RESET)\n"
	mkdir -p qt6/sdl3-desktop-build
	cd qt6/sdl3-desktop-build && cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/codeos-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
	cd qt6/sdl3-desktop-build && $(MAKE) -j$$(nproc)
	@printf "$(GREEN)SDL3 desktop built$(RESET)\n"

sdl3-clean:
	rm -rf qt6/sdl3-build qt6/sdl3-demo-build qt6/sdl3-desktop-build
	@printf "$(GREEN)SDL3 build cleaned$(RESET)\n"

# ── Icon Generation ──────────────────────────────────────────────

icons: icons-25d

icons-25d:
	@printf "$(CYAN)$(BOLD)==> Generating 2.5D icons...$(RESET)\n"
	python3 scripts/gen_25d_icons.py
	@printf "$(GREEN)2.5D icons generated$(RESET)\n"

icons-dedup:
	@printf "$(CYAN)$(BOLD)==> Deduplicating icon assets...$(RESET)\n"
	@# Remove duplicate RGBA files from panels/assets (canonical: pkgs/core/icons/src/)
	@rm -f pkgs/core/panels/assets/*.rgba
	@# Remove stale copies from kernel/userspace/icons/
	@rm -f kernel/userspace/icons/*.rgba
	@printf "$(GREEN)Icon duplicates removed$(RESET)\n"

# ── Xora application archives ───────────────────────────────────
# Usage: make xora-pack XORA_SOURCE=pkgs/core/terminal XORA_OUTPUT=terminal.xora

xora-pack:
	@test -n "$(XORA_SOURCE)" || { echo "set XORA_SOURCE=<app directory>" >&2; exit 2; }
	@test -n "$(XORA_OUTPUT)" || { echo "set XORA_OUTPUT=<file.xora>" >&2; exit 2; }
	python3 scripts/xora.py pack "$(XORA_SOURCE)" --output "$(XORA_OUTPUT)"

xora-inspect:
	@test -n "$(XORA_ARCHIVE)" || { echo "set XORA_ARCHIVE=<file.xora>" >&2; exit 2; }
	python3 scripts/xora.py inspect "$(XORA_ARCHIVE)"

xora-unpack:
	@test -n "$(XORA_ARCHIVE)" || { echo "set XORA_ARCHIVE=<file.xora>" >&2; exit 2; }
	@test -n "$(XORA_DESTINATION)" || { echo "set XORA_DESTINATION=<directory>" >&2; exit 2; }
	python3 scripts/xora.py unpack "$(XORA_ARCHIVE)" "$(XORA_DESTINATION)"

xora-install:
	@test -n "$(XORA_ARCHIVE)" || { echo "set XORA_ARCHIVE=<file.xora>" >&2; exit 2; }
	@test -n "$(XORA_ROOT)" || { echo "set XORA_ROOT=<CodeOS root>" >&2; exit 2; }
	python3 scripts/xora.py install "$(XORA_ARCHIVE)" --root "$(XORA_ROOT)"

xora-audit:
	@test -n "$(XORA_ROOT)" || { echo "set XORA_ROOT=<CodeOS root>" >&2; exit 2; }
	python3 scripts/xora.py audit --root "$(XORA_ROOT)" $(if $(XORA_REPAIR),--repair,)

xora-all:
	python3 scripts/build_xora_apps.py

# ── Help ─────────────────────────────────────────────────────────

help:
	@printf "$(CYAN)$(BOLD)CodeOS Build Targets:$(RESET)\n"
	@printf "\n  $(BOLD)Kernel/ISO:$(RESET)\n"
	@printf "    make              Build kernel + ISO\n"
	@printf "    make kernel       Build kernel only\n"
	@printf "    make iso          Create bootable ISO\n"
	@printf "    make run          Build and run in QEMU\n"
	@printf "    make run-iso      Boot ISO in QEMU\n"
	@printf "    make clean        Clean build artifacts\n"
	@printf "    make check        Validate compiler and build dependencies\n"
	@printf "\n  $(BOLD)Qt6 Support:$(RESET)\n"
	@printf "    make qt6          Build Qt6 support libraries\n"
	@printf "    make qt6-install  Install Qt6 support to sysroot\n"
	@printf "    make qt6-clean    Clean Qt6 build\n"
	@printf "    make qt6-download Download Qt6 source (~1GB)\n"
	@printf "    make qt6-configure Configure Qt6 for CodeOS\n"
	@printf "    make qt6-build-full  Build Qt6 from source\n"
	@printf "    make qt6-all      Full Qt6 build pipeline\n"
	@printf "\n  $(BOLD)SDL3 Support:$(RESET)\n"
	@printf "    make sdl3       Build SDL3 for CodeOS\n"
	@printf "    make sdl3-demo  Build SDL3 demo application\n"
	@printf "    make sdl3-desktop Build SDL3 desktop environment\n"
	@printf "    make sdl3-clean Clean SDL3 build\n"
	@printf "\n  $(BOLD)Xora apps:$(RESET)\n"
	@printf "    make xora-pack XORA_SOURCE=... XORA_OUTPUT=...\n"
	@printf "    make xora-inspect XORA_ARCHIVE=...\n"
	@printf "    make xora-unpack XORA_ARCHIVE=... XORA_DESTINATION=...\n"
	@printf "    make xora-install XORA_ARCHIVE=... XORA_ROOT=...\n"
	@printf "    make xora-audit XORA_ROOT=... [XORA_REPAIR=1]\n"
	@printf "    make xora-all       Package all built userspace apps\n"
	@printf "\n  $(BOLD)Config:$(RESET) ARCH=$(ARCH)\n"
