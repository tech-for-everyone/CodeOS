# CodeOS Root Makefile
# Define target architecture
ARCH ?= x86_64
export ARCH

# Version info
-include version

# Parallel build jobs.  nproc is GNU/Linux-only; macOS and Termux provide
# getconf, and every platform has a single-CPU fallback.  Override with
# `make JOBS=2` on a constrained machine.
JOBS ?= $(shell (nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2))

# Rust tools: PATH first, then the standard rustup shim directory.  Nightly on
# an aarch64 host is a normal supported setup; no x86_64 toolchain path here.
RUSTC ?= $(shell command -v rustc 2>/dev/null || printf '%s' "$(HOME)/.cargo/bin/rustc")
CARGO ?= $(shell command -v cargo 2>/dev/null || printf '%s' "$(HOME)/.cargo/bin/cargo")

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

.PHONY: all check jengine-check codec-check codec-fuzz-check kernel iso codeos-1.0.iso clean run run-iso help
.PHONY: offline-ide offline-ide-package
.PHONY: ncvm ncvm-build ncvm-run
.PHONY: netbeam-check netbeam-pkg
.PHONY: qt6 qt6-build qt6-install qt6-clean
.PHONY: qt6-download qt6-configure qt6-build-full qt6-all
.PHONY: icons icons-25d icons-dedup
.PHONY: xora-pack xora-inspect xora-unpack xora-install xora-audit xora-all
.PHONY: sdl3 sdl3-build sdl3-install sdl3-clean sdl3-demo sdl3-desktop
.PHONY: gtk6 gtk6-glib gtk6-check gtk6-clean
.PHONY: gtk6-all gtk6-gtk4 gtk6-gtk4-check
.PHONY: bas bas-check bas-target-check swift-check vsnprintf-check device-check hid-parser-check xhci-trb-check rtl8139-rx-check rtl8169-desc-check move-on-check apk-parser-check openweb-check
.PHONY: ace-check ace-boot-check
.PHONY: driver-check python-check rust-fallback-check rust-target-check compositor-check cmatrix cmatrix-swift linker-check

all: kernel
	$(MAKE) iso
	@printf "\n$(CYAN)$(BOLD)Build complete!$(RESET)\n"

check:
	@printf "$(CYAN)$(BOLD)==> Checking build environment...$(RESET)\n"
	$(MAKE) -C kernel check
	$(MAKE) -C kernel ace-check
	$(MAKE) linker-check
	$(MAKE) jengine-check
	$(MAKE) ncvm-check
	$(MAKE) vsnprintf-check
	$(MAKE) device-check
	$(MAKE) hid-parser-check
	$(MAKE) xhci-trb-check
	$(MAKE) rtl8139-rx-check
	$(MAKE) rtl8169-desc-check
	$(MAKE) rust-fallback-check
	$(MAKE) rust-target-check
	$(MAKE) compositor-check
	$(MAKE) driver-check
	$(MAKE) python-check
	$(MAKE) move-on-check
	$(MAKE) codec-check
	$(MAKE) apk-parser-check
	$(MAKE) bas-check
	$(MAKE) swift-check
	$(MAKE) bas-target-check
	$(MAKE) netbeam-check
	$(MAKE) openweb-check

linker-check:
	@printf "$(CYAN)$(BOLD)==> Checking x86_64 linker discovery...$(RESET)\n"
	@python3 scripts/linker_check.py

driver-check:
	@printf "$(CYAN)$(BOLD)==> Checking automated driver detection & hardware support...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -I include -I kernel/kernel -I kernel/drivers \
	    -o /tmp/codeos-driver-test kernel/tests/driver_detect_test.c
	@/tmp/codeos-driver-test
	@cc -std=c11 -Wall -Wextra -Werror -I include -I kernel/kernel -I kernel/drivers \
	    -o /tmp/codeos-peripheral-driver-test kernel/tests/peripheral_driver_test.c \
	    kernel/drivers/mouse_g305.c kernel/drivers/kbd_aula_f87.c
	@/tmp/codeos-peripheral-driver-test
	@printf "$(GREEN)driver detection & hardware checks passed$(RESET)\n"

python-check:
	@printf "$(CYAN)$(BOLD)==> Checking Python CLI utilities & test suite...$(RESET)\n"
	@python3 tests/test_cmatrix.py
	@python3 tests/test_driver_subsystem.py
	@printf "$(GREEN)Python CLI checks passed$(RESET)\n"

cmatrix:
	@python3 tools/cmatrix.py

cmatrix-swift:
	@if command -v swiftc >/dev/null 2>&1; then \
	    swiftc swift/cmatrix.swift -o tools/cmatrix-swift && ./tools/cmatrix-swift; \
	elif [ -x /usr/bin/swiftc ]; then \
	    swiftc swift/cmatrix.swift -o tools/cmatrix-swift && ./tools/cmatrix-swift; \
	elif [ -x "$$HOME/.local/share/swiftly/bin/swiftc" ]; then \
	    "$$HOME/.local/share/swiftly/bin/swiftc" swift/cmatrix.swift -o tools/cmatrix-swift && ./tools/cmatrix-swift; \
	else \
	    printf "$(DIM)cmatrix-swift: swiftc compiler not found; Swift source ready at swift/cmatrix.swift$(RESET)\n"; \
	fi

# kernel/kernel/device.c is the device table: the phones and tablets the
# Zircon kernel knows how to drive, and the rules for picking one (an
# explicit "zircon.device=" on the command line, then a panel that matches
# a profile, then a generic profile from the geometry).  The host test
# compiles the REAL source and drives it, so the table and the selection
# are covered without a device.
device-check:
	@printf "$(CYAN)$(BOLD)==> Checking the device table...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -I include -I kernel/kernel -o /tmp/codeos-device-test \
	    kernel/tests/device_test.c kernel/kernel/device.c
	@/tmp/codeos-device-test
	@printf "$(GREEN)device checks passed$(RESET)\n"

# The HID report-descriptor parser used to compare a 4-bit bTag against the
# full HID_TAG_* item prefixes, so every real descriptor produced zero fields.
# This compiles the REAL kernel/drivers/hid_parser.c against real descriptor
# bytes: reverting just the `>> 4` on the case labels fails 9 assertions.
hid-parser-check:
	@printf "$(CYAN)$(BOLD)==> Checking the HID report parser...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -I kernel/drivers -I kernel/kernel \
	    -o /tmp/codeos-hid-parser-test \
	    kernel/tests/hid_parser_test.c kernel/drivers/hid_parser.c
	@/tmp/codeos-hid-parser-test
	@printf "$(GREEN)HID parser checks passed$(RESET)\n"

# The xHCI TRB layout bugs (length in DW1 instead of DW2, missing TRT,
# unreachable Link TRB slot, cycle bit forced on, wheel gated on a status
# return) are only observable in the bytes the driver hands the controller.
# This harness compiles the REAL driver source with the x86 port-I/O headers
# replaced by empty files, then checks those bytes.  A mechanically reverted
# copy must fail the same assertions -- otherwise the check proves nothing.
xhci-trb-check:
	@printf "$(CYAN)$(BOLD)==> Checking xHCI TRB encoding...$(RESET)\n"
	@H=/tmp/codeos-xhci-harness; \
	 rm -rf $$H; \
	 mkdir -p $$H/arch/x86_64 $$H/kernel $$H/drivers; \
	 : > $$H/arch/x86_64/io.h; \
	 : > $$H/arch/x86_64/pci.h; \
	 : > $$H/kernel/kprintf.h; \
	 : > $$H/kernel/string.h; \
	 : > $$H/kernel/vmm.h; \
	 : > $$H/kernel/pmm.h; \
	 cp kernel/drivers/usb_xhci.h kernel/drivers/usb_ehci.h kernel/drivers/mouse.h \
	    kernel/drivers/keyboard.h kernel/drivers/hid_keys.h $$H/drivers/; \
	 cp kernel/drivers/usb_xhci.c $$H/drivers/usb_xhci_under_test.c; \
	 sed -i 's/asm volatile("pause")/(void)0/g' $$H/drivers/usb_xhci_under_test.c; \
	 cc -std=gnu11 -Wall -Wextra -fno-stack-protector -I$$H/drivers -I kernel/kernel \
	    -o /tmp/codeos-xhci-trb-test kernel/tests/xhci_trb_test.c; \
	 /tmp/codeos-xhci-trb-test || exit 1; \
	 python3 kernel/tests/xhci_revert.py kernel/drivers/usb_xhci.c \
	    $$H/drivers/usb_xhci_under_test.c; \
	 sed -i 's/asm volatile("pause")/(void)0/g' $$H/drivers/usb_xhci_under_test.c; \
	 cc -std=gnu11 -w -fno-stack-protector -I$$H/drivers -I kernel/kernel \
	    -o /tmp/codeos-xhci-trb-buggy kernel/tests/xhci_trb_test.c; \
	 if /tmp/codeos-xhci-trb-buggy >/tmp/codeos-xhci-buggy.log 2>&1; then \
	   echo "FAIL: negative control passed -- assertions are not sensitive" >&2; \
	   exit 1; \
	 fi; \
	 echo "negative control: reverted driver fails $$(grep -c '^FAIL:' /tmp/codeos-xhci-buggy.log) assertions (expected)"
	@printf "$(GREEN)xHCI TRB checks passed$(RESET)\n"

# RTL8139 RX ring bugs: a 4112-byte ring allocated from one page, and a
# cursor advanced by the clamped copy length (so the next read re-read the
# same frame).  The harness compiles the REAL driver with in-memory port I/O
# and checks CAPR plus the cursor after each frame, with a reverted control.
rtl8139-rx-check:
	@printf "$(CYAN)$(BOLD)==> Checking RTL8139 RX ring...$(RESET)\n"
	@H=/tmp/codeos-rtl8139-harness; \
	 rm -rf $$H; \
	 mkdir -p $$H/arch/x86_64 $$H/kernel $$H/drivers; \
	 : > $$H/arch/x86_64/io.h; \
	 : > $$H/arch/x86_64/pci.h; \
	 : > $$H/kernel/kprintf.h; \
	 : > $$H/kernel/string.h; \
	 : > $$H/kernel/vmm.h; \
	 : > $$H/kernel/pmm.h; \
	 cp kernel/drivers/rtl8139.h kernel/drivers/nic.h $$H/drivers/; \
	 cp kernel/drivers/rtl8139.c $$H/drivers/rtl8139_under_test.c; \
	 cc -std=gnu11 -Wall -Wextra -fno-stack-protector -I$$H/drivers -I kernel/kernel \
	    -o /tmp/codeos-rtl8139-rx-test kernel/tests/rtl8139_rx_test.c; \
	 /tmp/codeos-rtl8139-rx-test || exit 1; \
	 python3 kernel/tests/rtl8139_revert.py kernel/drivers/rtl8139.c \
	    $$H/drivers/rtl8139_under_test.c; \
	 cc -std=gnu11 -w -fno-stack-protector -I$$H/drivers -I kernel/kernel \
	    -o /tmp/codeos-rtl8139-rx-buggy kernel/tests/rtl8139_rx_test.c; \
	 if /tmp/codeos-rtl8139-rx-buggy >/tmp/codeos-rtl8139-buggy.log 2>&1; then \
	   echo "FAIL: negative control passed -- assertions are not sensitive" >&2; \
	   exit 1; \
	 fi; \
	 echo "negative control: reverted driver fails $$(grep -c '^FAIL:' /tmp/codeos-rtl8139-buggy.log) assertions (expected)"
	@printf "$(GREEN)RTL8139 RX checks passed$(RESET)\n"

# RTL8169: the driver used RTL8139-era register offsets and omitted the RX
# buffer-size field / TX End-of-Ring bit.  The harness backs MMIO with memory
# so every write is checked at its literal address, with a reverted control.
rtl8169-desc-check:
	@printf "$(CYAN)$(BOLD)==> Checking RTL8169 register map and descriptors...$(RESET)\n"
	@H=/tmp/codeos-rtl8169-harness; \
	 rm -rf $$H; \
	 mkdir -p $$H/arch/x86_64 $$H/kernel $$H/drivers; \
	 : > $$H/arch/x86_64/io.h; \
	 : > $$H/arch/x86_64/pci.h; \
	 : > $$H/kernel/kprintf.h; \
	 : > $$H/kernel/string.h; \
	 : > $$H/kernel/vmm.h; \
	 : > $$H/kernel/pmm.h; \
	 cp kernel/drivers/rtl8169.h kernel/drivers/nic.h $$H/drivers/; \
	 cp kernel/drivers/rtl8169.c $$H/drivers/rtl8169_under_test.c; \
	 cc -std=gnu11 -Wall -Wextra -fno-stack-protector -I$$H/drivers -I kernel/kernel \
	    -o /tmp/codeos-rtl8169-desc-test kernel/tests/rtl8169_desc_test.c; \
	 /tmp/codeos-rtl8169-desc-test || exit 1; \
	 python3 kernel/tests/rtl8169_revert.py kernel/drivers/rtl8169.c \
	    kernel/drivers/rtl8169.h $$H/drivers/rtl8169_under_test.c $$H/drivers/rtl8169.h; \
	 cc -std=gnu11 -w -fno-stack-protector -I$$H/drivers -I kernel/kernel \
	    -o /tmp/codeos-rtl8169-desc-buggy kernel/tests/rtl8169_desc_test.c; \
	 if /tmp/codeos-rtl8169-desc-buggy >/tmp/codeos-rtl8169-buggy.log 2>&1; then \
	   echo "FAIL: negative control passed -- assertions are not sensitive" >&2; \
	   exit 1; \
	 fi; \
	 echo "negative control: reverted driver fails $$(grep -c '^FAIL:' /tmp/codeos-rtl8169-buggy.log) assertions (expected)"
	@printf "$(GREEN)RTL8169 descriptor checks passed$(RESET)\n"

move-on-check:
	@printf "$(CYAN)$(BOLD)==> Checking Move On migration...$(RESET)\n"
	@PYTHONDONTWRITEBYTECODE=1 python3 Zircon/scripts/test_move_on.py
	@printf "$(GREEN)Move On checks passed$(RESET)\n"

# ArkUI/ACE is a real runtime component: ace-host compiles the OpenHarmony
# engine into a CodeOS ELF, kernel/Makefile embeds it, and ace_probe.c runs it
# at boot.  The host check proves the payload exists; the boot check proves the
# kernel loaded it and the engine's own layout assertions passed.
ace-check:
	@printf "$(CYAN)$(BOLD)==> Checking ArkUI/ACE engine payload...$(RESET)\n"
	$(MAKE) -C kernel ace-check

ace-boot-check:
	@printf "$(CYAN)$(BOLD)==> Checking ArkUI/ACE at boot...$(RESET)\n"
	@python3 kernel/scripts/ace_boot_check.py

# kernel/userspace/lib/stdio.c is the freestanding libc every CodeOS
# userspace program formats through, and its vsnprintf had to be fixed to
# honour C99's "size == 0 measures and returns the required length" -- without
# it the bas runtime's format! allocated a one-byte buffer and every format! on
# CodeOS printed an empty string.  The host test compiles the REAL source
# against a real libc and compares the measured length, the written length and
# the rendered text with glibc's for the same format, which is the only way to
# catch a wrong length: on CodeOS nothing formats a string and compares the
# result to anything.
vsnprintf-check:
	@printf "$(CYAN)$(BOLD)==> Checking the userspace vsnprintf...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -Wno-format-zero-length -Wno-format-extra-args \
	    -o /tmp/codeos-vsnprintf-test kernel/userspace/tests/vsnprintf_test.c
	@/tmp/codeos-vsnprintf-test
	@printf "$(GREEN)vsnprintf checks passed$(RESET)\n"

jengine-check:
	@cc -std=c11 -Wall -Wextra -Werror -Iinclude src/jengine.c tests/jengine_test.c \
		-o /tmp/codeos-jengine-test
	@/tmp/codeos-jengine-test
	@printf "$(CYAN)$(BOLD)==> Checking jengine freestanding build...$(RESET)\n"
	@clang -ffreestanding -nostdlib -nodefaultlibs -fno-stack-protector \
		-Wall -Wextra -Werror -Iinclude -c src/jengine.c \
		-o /tmp/codeos-jengine-freestanding.o
	@unks=$$(nm -u /tmp/codeos-jengine-freestanding.o | awk '{print $$2}' | grep -v '^memcpy$$' || true); \
		if [ -n "$$unks" ]; then \
			printf "unexpected external symbols in freestanding jengine: %s\n" "$$unks" >&2; \
			exit 1; \
		fi
	@printf "$(GREEN)Jengine checks passed (host + freestanding)$(RESET)\n"

# OpenWeb: URL joining in the tty browser, the live Rust renderer's table /
# entity / wrap behaviour, the canonical adblock matcher behind native fetch
# enforcement, and the PWA asset/script contract. The browser UI is plain
# static files, so a missing app.js is invisible to every other check in this
# tree until a user clicks a dead button.
OPENWEB_CC ?= clang
openweb-check:
	@printf "$(CYAN)$(BOLD)==> Checking OpenWeb...$(RESET)\n"
	@$(OPENWEB_CC) -std=c99 -Wall -Wextra -Werror -g \
	    -o /tmp/codeos-openweb-url-test pkgs/core/openweb/tests/ow_url_test.c
	@/tmp/codeos-openweb-url-test
	@$(OPENWEB_CC) -std=c99 -Wall -Wextra -Werror -g -Ikernel/kernel \
	    -o /tmp/codeos-ad-block-test kernel/tests/ad_block_test.c
	@/tmp/codeos-ad-block-test
	@if command -v $(RUSTC) >/dev/null 2>&1; then \
	    $(RUSTC) --edition 2021 -Awarnings \
	        -o /tmp/codeos-openweb-render-test \
	        pkgs/core/openweb/tests/ow_render_fixture.rs && \
	    /tmp/codeos-openweb-render-test; \
	else \
	    printf "rustc not found; skipping OpenWeb renderer fixture\n"; \
	fi
	@python3 scripts/openweb_ui_check.py
	@printf "$(GREEN)OpenWeb checks passed$(RESET)\n"

# The x86_64 image links a C fallback when the x86_64-unknown-none Rust target
# is unavailable.  This compiles the REAL fallback against the REAL public
# headers, drives all 27 exported entry points, and then breaks tab accounting
# and compositor framing in copied sources to prove the assertions are
# sensitive.
rust-fallback-check:
	@printf "$(CYAN)$(BOLD)==> Checking the Rust-free fallback...$(RESET)\n"
	@cc -std=c11 -Wall -Wextra -Werror -I qt6/panels -I pkgs/core/panels/src \
	    -I kernel/kernel -I include \
	    -o /tmp/codeos-rust-fallback-test \
	    kernel/tests/rust_fallback_test.c kernel/rust_fallback.c
	@/tmp/codeos-rust-fallback-test
	@python3 kernel/tests/rust_fallback_revert.py kernel/rust_fallback.c \
	    /tmp/codeos-rust-fallback-buggy.c
	@cc -std=c11 -w -I qt6/panels -I pkgs/core/panels/src \
	    -I kernel/kernel -I include \
	    -o /tmp/codeos-rust-fallback-buggy \
	    kernel/tests/rust_fallback_test.c /tmp/codeos-rust-fallback-buggy.c
	@if /tmp/codeos-rust-fallback-buggy >/tmp/codeos-rust-fallback-buggy.log 2>&1; then \
	    echo "FAIL: negative control passed -- assertions are not sensitive" >&2; \
	    exit 1; \
	 fi; \
	 echo "negative control: broken fallback tab accounting fails $$(grep -c '^FAIL:' /tmp/codeos-rust-fallback-buggy.log) assertions (expected)"
	@python3 kernel/tests/rust_fallback_revert_frame.py kernel/rust_fallback.c \
	    /tmp/codeos-rust-fallback-nopresent.c
	@cc -std=c11 -w -I qt6/panels -I pkgs/core/panels/src \
	    -I kernel/kernel -I include \
	    -o /tmp/codeos-rust-fallback-nopresent \
	    kernel/tests/rust_fallback_test.c /tmp/codeos-rust-fallback-nopresent.c
	@if /tmp/codeos-rust-fallback-nopresent >/tmp/codeos-rust-fallback-nopresent.log 2>&1; then \
	    echo "FAIL: compositor-frame negative control passed -- assertions are not sensitive" >&2; \
	    exit 1; \
	 fi; \
	 echo "negative control: skipped compositor frame fails $$(grep -c '^FAIL:' /tmp/codeos-rust-fallback-nopresent.log) assertions (expected)"
	@printf "$(GREEN)Rust-free fallback checks passed$(RESET)\n"

# Both no-std Rust crates must compile for the x86_64 and aarch64 kernel
# triples with the nightly/rustup install on an aarch64 host. The script also
# reverts the architecture guard and the toolchain detection in copies to show
# the assertions fail.
rust-target-check:
	@printf "$(CYAN)$(BOLD)==> Checking Rust target support...$(RESET)\n"
	@python3 kernel/scripts/rust_target_check.py
	@printf "$(GREEN)Rust target checks passed$(RESET)\n"

# Windowland must be the only per-frame presentation entry point. Pin the
# Rust bridge, C fallback and Qt call site together, then remove the chrome
# stage from a checked copy to prove the wiring assertions are sensitive.
compositor-check:
	@printf "$(CYAN)$(BOLD)==> Checking compositor wiring...$(RESET)\n"
	@python3 kernel/scripts/compositor_check.py
	@python3 kernel/scripts/compositor_check.py --negative
	@printf "$(GREEN)Compositor wiring checks passed$(RESET)\n"

apk-parser-check:
	@printf "$(CYAN)$(BOLD)==> Checking the APK parser...$(RESET)\n"
	$(MAKE) -C kernel/userspace apk-parser-check

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

# bas is an optional sibling checkout, like OpenWeb: the language lives in its
# own repository (CodeOS-Comunity/bas) and CodeOS integrates it as a host
# toolchain rather than vendoring the compiler, so `make check` keeps working on
# a tree without it. Program sources use the `.ba` extension; tests/run.sh
# rebuilds the compiler with cargo, then runs every examples/*.ba program and
# rejects tests/errors/*.ba with the matching diagnostic.
BAS_DIR ?= $(firstword $(wildcard ../bas /usr/local/share/bas))

bas:
	@if [ -z "$(strip $(BAS_DIR))" ] || [ ! -f "$(BAS_DIR)/compiler/Cargo.toml" ]; then \
	    echo "bas: no checkout at '$(BAS_DIR)'; set BAS_DIR=/path/to/bas"; \
	    exit 1; \
	fi
	@cargo build --manifest-path "$(BAS_DIR)/compiler/Cargo.toml" -p bas-cli
	@echo "bas: $(BAS_DIR)/compiler/target/debug/bas"

bas-check:
	@if [ -z "$(strip $(BAS_DIR))" ] || [ ! -f "$(BAS_DIR)/tests/run.sh" ]; then \
	    printf "$(DIM)bas-check: no bas checkout at '$(BAS_DIR)' (optional); skipping$(RESET)\n"; \
	elif [ ! -w "$(BAS_DIR)/compiler" ] || { [ -e "$(BAS_DIR)/compiler/target" ] && [ ! -w "$(BAS_DIR)/compiler/target" ]; }; then \
	    printf "$(DIM)bas-check: checkout at '$(BAS_DIR)' is read-only; skipping$(RESET)\n"; \
	else \
	    printf "$(CYAN)$(BOLD)==> Checking bas language...$(RESET)\n"; \
	    bash "$(BAS_DIR)/tests/run.sh"; \
	fi

swift-check:
	@./tools/swift-codeos

# Cross-build check: every pkgs/core/bas/src/*.ba must compile into a real
# CodeOS userland ELF for BOTH targets.  A target can break without the host
# suite noticing -- the Zircon target was missing strstr from its libc and only
# this build said so -- so both are built here rather than waiting for the next
# image build.
#
# Skipped (not failed) with no bas checkout, matching bas-check.
bas-target-check:
	@if [ -z "$(strip $(BAS_DIR))" ] || [ ! -f "$(BAS_DIR)/compiler/target/debug/bas" ]; then \
	    printf "$(DIM)bas-target-check: no bas compiler at '$(BAS_DIR)' (optional); skipping$(RESET)\n"; \
	else \
	    printf "$(CYAN)$(BOLD)==> Cross-building bas programs for CodeOS and Zircon...$(RESET)\n"; \
	    set -e; for b in pkgs/core/bas/src/*.ba; do \
	        stem=$$(basename $$b .ba); \
	        "$(BAS_DIR)/compiler/target/debug/bas" build $$b \
	            --target codeos --target-dir pkgs/core/bas/sdk/targets \
	            --sysroot . -o /tmp/codeos-bas-target-$$stem || exit 1; \
	        "$(BAS_DIR)/compiler/target/debug/bas" build $$b \
	            --target zircon --target-dir pkgs/core/bas/sdk/targets \
	            --sysroot . -o /tmp/zircon-bas-target-$$stem || exit 1; \
	        file /tmp/codeos-bas-target-$$stem | grep -q "ELF 64-bit" || { \
	            echo "codeos build of $$b is not a 64-bit ELF"; exit 1; }; \
	        file /tmp/zircon-bas-target-$$stem | grep -q "ELF 64-bit" || { \
	            echo "zircon build of $$b is not a 64-bit ELF"; exit 1; }; \
	        echo "  OK  $$stem (codeos, zircon)"; \
	        rm -f /tmp/codeos-bas-target-$$stem /tmp/zircon-bas-target-$$stem; \
	    done; \
	    printf "$(GREEN)bas cross-target checks passed$(RESET)\n"; \
	fi

# Runtime proof. `bas-target-check` only proves the ELF links; this boots the
# ISO and asserts on what /bin/bas-selftest actually printed (see
# kernel/scripts/bas_boot_check.py).  Separate and not part of `check` because it
# needs QEMU and a full image.
.PHONY: bas-boot-check
bas-boot-check:
	@printf "$(CYAN)$(BOLD)==> Booting CodeOS to run the bas programs...$(RESET)\n"
	@cd kernel && python3 scripts/bas_boot_check.py

# The writable filesystem: the disk the kernel mounts is the writable layer,
# so a file the shell writes has to be *on the disk* afterwards and still be
# there after the next boot.  The check writes in the guest, reads the image
# back on the host with debugfs, and boots again.
.PHONY: wfs-check
wfs-check:
	@printf "$(CYAN)$(BOLD)==> Booting CodeOS to check the writable filesystem...$(RESET)\n"
	@cd kernel && python3 scripts/wfs_check.py

# The shell's pipelines: a program's output has to travel down a real pipe,
# so the file a redirected pipeline writes has to hold it -- exactly once.
.PHONY: shell-check
shell-check:
	@printf "$(CYAN)$(BOLD)==> Booting CodeOS to check the shell's pipelines...$(RESET)\n"
	@cd kernel && python3 scripts/shell_check.py

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

# `iso` only copies the kernel's ISO to the tree root, so the file has to be
# phony: with a real (prerequisite-less) rule, `make` reports it "up to date"
# forever and a kernel rebuild never reaches the root ISO.  The staleness
# decision stays in kernel/Makefile, where `codeos-1.0.iso` correctly depends
# on the arch-dependent $(TARGET) -- a name this Makefile must not spell out.
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
	cd qt6/support-build && $(MAKE) -j$(JOBS)
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

# ── GTK6 toolkit (GTK4 API on the Qt6 engine) ─────────────────────
# GTK6 is a first-class package at pkgs/core/gtk6: it ships GTK4's widget
# layer and public API with the platform subsystems (GDK/GSK/text) rebuilt on
# Qt6.  Two builds make it up:
#   - pkgs/core/gtk6/Makefile        the freestanding Qt-backed C++ layer
#   - pkgs/core/gtk6/scripts/build-glib.sh   the CodeOS port of glib/gobject/gio
# glib is kept inside the package (pkgs/core/gtk6/subprojects/glib).

gtk6:
	@printf "$(CYAN)$(BOLD)==> Building gtk6 toolkit (GTK4 on Qt6)...$(RESET)\n"
	$(MAKE) -C pkgs/core/gtk6 all
	@printf "$(GREEN)gtk6 toolkit ready$(RESET)\n"

gtk6-glib:
	@printf "$(CYAN)$(BOLD)==> Building gtk6's glib (CodeOS port)...$(RESET)\n"
	cd pkgs/core/gtk6 && ./scripts/build-glib.sh
	@printf "$(GREEN)gtk6 glib ready$(RESET)\n"

gtk6-all: gtk6 gtk6-glib
	@printf "$(GREEN)gtk6 complete$(RESET)\n"

gtk6-gtk4:
	@printf "$(CYAN)$(BOLD)==> Generating the GTK4 headers (phase 5)...$(RESET)\n"
	$(MAKE) -C pkgs/core/gtk6 gtk4-generated
	@printf "$(GREEN)gtk6 GTK4 generated headers ready$(RESET)\n"

gtk6-gtk4-check:
	$(MAKE) -C pkgs/core/gtk6 gtk4-check

gtk6-check:
	@printf "$(CYAN)$(BOLD)==> Checking gtk6...$(RESET)\n"
	$(MAKE) -C pkgs/core/gtk6 check

gtk6-clean:
	$(MAKE) -C pkgs/core/gtk6 clean
	rm -rf pkgs/core/gtk6/build
	@printf "$(GREEN)gtk6 cleaned$(RESET)\n"

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
	cd qt6/sdl3-demo-build && $(MAKE) -j$(JOBS)
	@printf "$(GREEN)SDL3 demo built$(RESET)\n"

sdl3-desktop: sdl3-install
	@printf "$(CYAN)$(BOLD)==> Building SDL3 desktop...$(RESET)\n"
	mkdir -p qt6/sdl3-desktop-build
	cd qt6/sdl3-desktop-build && cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/codeos-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
	cd qt6/sdl3-desktop-build && $(MAKE) -j$(JOBS)
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

offline-ide:
	$(MAKE) -C kernel/userspace offline-ide

offline-ide-package: offline-ide
	python3 scripts/build_xora_apps.py --app offline-ide

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
	@printf "    make offline-ide-package  Build and package the offline IDE\n"
	@printf "\n  $(BOLD)Config:$(RESET) ARCH=$(ARCH)\n"
