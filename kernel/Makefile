# Detect architecture
ARCH ?= x86_64
export ARCH

ifeq ($(ARCH),arm64)
    QEMU = qemu-system-aarch64
    QEMU_MEM = -m 512M
    QEMU_NET = -netdev user,id=net0 -device virtio-net-pci,netdev=net0

    # Use the system aarch64-linux-gnu cross-compiler (bare-metal freestanding)
    CC = aarch64-linux-gnu-gcc
    CXX = aarch64-linux-gnu-g++
    LD = aarch64-linux-gnu-ld
    AS = aarch64-linux-gnu-as
    OBJCOPY = aarch64-linux-gnu-objcopy

    CFLAGS = -ffreestanding -mcmodel=large -fno-stack-protector -fno-PIC \
             -nostdlib -nostartfiles -nodefaultlibs \
             -Wall -Wextra -O2 -g -MMD -MP \
             -I. -I./kernel -I./drivers -I../include -Iarch/arm64
    CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit
    LDFLAGS = -nostdlib -z max-page-size=0x1000

    USER_CFLAGS =
    USER_LDFLAGS =
    USER_ELF =

    # ARM64: only compile the minimal set of ARM64-compatible sources
    SRC_C = arch/arm64/serial.c \
            arch/arm64/fb.c \
            arch/arm64/fw_cfg.c \
            arch/arm64/display.c \
            arch/arm64/pci.c \
            arch/arm64/rtc.c \
            arch/arm64/kernel_main.c \
            arch/arm64/handlers.c \
            arch/arm64/string.c \
            kernel/kprintf.c
    SRC_CXX =
    SRC_ASM = $(wildcard arch/arm64/*.S)
    TARGET = codeos-1-kernel-arm64.bin
    LDSCRIPT = linker.arm64.ld
    EMBED_OBJ =
    STAGE1_TARGET =
    FLAT_BIN = $(TARGET:.bin=.flat.bin)
else
    QEMU = qemu-system-x86_64
    KVM ?= -accel kvm
    QEMU_MEM ?= -m 512M
    
    # FIXED: Re-routed from 8080 to 7070 to clear the SearXNG port conflict
    QEMU_NET ?= -netdev user,id=net0,hostfwd=tcp::7070-:80,hostfwd=tcp::2222-:22 -device e1000,netdev=net0
    QEMU_WIFI ?= -netdev user,id=wifi,hostfwd=tcp::7070-:80,hostfwd=tcp::2222-:22,dhcpstart=10.0.2.100 -device e1000,netdev=wifi
    
    QEMU_DISK ?= -hda disk.img
    # Prefer the freestanding cross toolchain, but make the documented gcc
    # fallback real.  This is useful on distributions that only package the
    # regular GNU compiler and also makes the selected toolchain overridable:
    #   make CROSS_COMPILE=my-x86_64- all
    CROSS_COMPILE ?= x86_64-elf-
    ifneq ($(shell command -v $(CROSS_COMPILE)gcc 2>/dev/null),)
        CC  := $(CROSS_COMPILE)gcc
        CXX := $(CROSS_COMPILE)g++
        LD  := $(CROSS_COMPILE)ld
        AS  := $(CROSS_COMPILE)as
    else
        $(warning $(CROSS_COMPILE)gcc not found; falling back to the host GCC toolchain)
        CC  := gcc
        CXX := g++
        LD  := ld
        AS  := as
    endif
    OBJCOPY ?= objcopy
    GRUB_MKRESCUE ?= grub-mkrescue
    GRUB_DIR ?= /usr/lib/grub/i386-pc
    # ── Auto-discover packages: scan pkgs/core/*/src for .c and .cpp ──
    # Any package with a KERN file in its src/ gets compiled into the kernel
    # Just touch pkgs/core/mypackage/src/KERN to inject your package
    # The legacy C/C++ panels package has been superseded by qt6/panels.
    # Keep other kernel-injected packages, but do not link a second GUI.
    KERN_PKG_DIRS = $(filter-out ../pkgs/core/panels/src/,$(sort $(dir $(wildcard ../pkgs/core/*/src/KERN))))
    PKG_DIRS = $(KERN_PKG_DIRS)
    ifneq ($(strip $(PKG_DIRS)),)
        PKG_SRC_C   = $(shell find $(PKG_DIRS) -name "*.c" -not -name "KERN" -not -name "*.d")
        PKG_SRC_CXX = $(shell find $(PKG_DIRS) -name "*.cpp" -not -name "KERN" -not -name "*.d")
        PKG_INCLUDES = $(addprefix -I,$(PKG_DIRS)) $(shell find $(PKG_DIRS) -mindepth 1 -type d | sed 's/^/-I/' )
    else
        PKG_SRC_C =
        PKG_SRC_CXX =
        PKG_INCLUDES =
    endif
    # PixelMan is retained as the kernel compositor backend.  The old panel
    # widgets are intentionally not linked; Qt owns the desktop UI now.
    QT_BACKEND_DIR = ../pkgs/core/panels/src

    CFLAGS = -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse \
             -mno-sse2 -nostdlib -nostartfiles -nodefaultlibs \
             -fno-stack-protector -fno-PIC \
 -Wall -Wextra -O2 -g -MMD -MP -ffunction-sections -fdata-sections \
              -I. -I./kernel -I./kernel/security -I./drivers -I../include -Iarch/x86_64 \
              -DLV_CONF_INCLUDE_SIMPLE -I./lvgl -isystem ./lvgl/shim \
              $(PKG_INCLUDES) -I$(QT_BACKEND_DIR) $(OPENWEB_INCLUDES) $(OPENSSL_CFLAGS)
    CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit
    LDFLAGS = -nostdlib -z max-page-size=0x1000 -z noexecstack -no-pie --gc-sections
    USER_CFLAGS = -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
                  -nostdlib -nostartfiles -fno-stack-protector -fno-PIC \
                  -Wall -Wextra -O2 -g -I./userspace/include
    USER_LDFLAGS = -nostdlib -z max-page-size=0x1000 -z noexecstack

    # Kernel infrastructure (stays in kernel/)
    SRC_C = $(shell find kernel drivers lvgl -name "*.c" \
        -not -path "kernel/gui/*" \
        -not -path "*/devstore/*" -not -name "initramfs_files.c" \
        -not -path "arch/arm64/*" -not -name "ahci.c" \
        -not -path "*/examples/*" -not -name "x11_server.c" \
        -not -name "x11_stubs.c" \
        -not -path "lvgl/src/draw/sdl/*" \
        -not -path "lvgl/src/draw/stm32_dma2d/*" \
        -not -path "lvgl/src/draw/renesas/*" \
        -not -path "lvgl/src/draw/swm341_dma2d/*" \
        -not -path "lvgl/src/draw/arm2d/*" \
        -not -path "lvgl/src/draw/nxp/*" \
        -not -path "lvgl/src/extra/libs/*" \
        -not -path "lvgl/src/extra/others/*") \
        arch/pci.c arch/serial.c arch/rtc.c arch/apic.c \
        lvgl_wm.c lvgl_port.c

    # Package sources (auto-discovered from pkgs/core/*/src/)
SRC_C   += $(PKG_SRC_C)
SRC_C   += $(QT_BACKEND_DIR)/updater.c
SRC_C   += $(QT_BACKEND_DIR)/lgame.c
SRC_C   += $(QT_BACKEND_DIR)/lgame_pong.c
SRC_C   += $(QT_BACKEND_DIR)/lgame_snake.c
SRC_C   += kernel/x11_server.c kernel/x11_stubs.c
    SRC_CXX = kernel/cxx_support.cpp kernel/stdcxx_support.cpp kernel/xserver.cpp arch/fb.cpp \
              kernel/desktop.cpp kernel/qt_desktop.cpp \
              $(QT_BACKEND_DIR)/pixelman.cpp $(PKG_SRC_CXX)
    SRC_ASM = $(shell find arch -maxdepth 1 -name "*.S")
    TARGET = codeos-1-kernel.bin
    LDSCRIPT = linker.ld
    EMBED_OBJ = kernel/embed_kernel.o kernel/embed_limine.o kernel/embed_limine_conf.o kernel/embed_splash.o
    STAGE1_TARGET = codeos-1-kernel-stage1.bin
endif

# Jengine is part of the native runtime so kernel-rendered pages and other
# CodeOS components use the same evaluator as the calculator APIs.
SRC_C += ../src/jengine.c

# OpenWeb is an optional checkout.  Search the common sibling locations and
# only add an include directory when it really exists; a stale absolute path
# otherwise makes clean builds fail on every other machine.
OPENWEB_LITEHTML_DIR ?= $(firstword $(wildcard \
    ../Openweb/litehtml/include \
    ../Openweb/eclipse/litehtml/include \
    /usr/include/litehtml))
ifneq ($(strip $(OPENWEB_LITEHTML_DIR)),)
    OPENWEB_INCLUDES = -I$(OPENWEB_LITEHTML_DIR)
else
    OPENWEB_INCLUDES =
endif

# ── Genuine OpenSSL TLS backend, linked freestanding ──────────────
# The kernel can build against upstream OpenSSL (pkgs/core/openssl) with no
# BSD-socket or hosted-libc dependency.  The static archives are produced by
# the freestanding OpenSSL build (see pkgs/core/openssl/README); when they are
# present we enable the OpenSSL https_get path and link libcrypto/libssl.
OPENSSL_LIBDIR ?= ../pkgs/core/openssl/lib
OPENSSL_LIBS := $(wildcard $(OPENSSL_LIBDIR)/libcrypto.a $(OPENSSL_LIBDIR)/libssl.a)
ifneq ($(strip $(OPENSSL_LIBS)),)
    OPENSSL_CFLAGS = -DHTTPS_USE_OPENSSL=1
    OPENSSL_LDLIBS = $(OPENSSL_LIBS)
else
    OPENSSL_CFLAGS =
    OPENSSL_LDLIBS =
endif

# The OpenSSL shim/client must resolve <stdio.h>/<stdlib.h>/<time.h> to the
# package's freestanding sysroot (freestd) first, otherwise mbedtls's minimal
# shims shadow it.  This include ordering applies ONLY to these two objects --
# never to the rest of the kernel.
../pkgs/core/openssl/src/ossl_shim.o ../pkgs/core/openssl/src/ossl_https.o: \
        CFLAGS := -I../pkgs/core/openssl/freestd -I../pkgs/core/openssl/freestd/sys $(CFLAGS)

USER_PROGS = init shell csl zircon-notify zircon-clock zircon-info apk-parser android-container android-apps code-music linux-runner crosvm-launcher ld-codeos terminal syscall_test openweb wineserver wine-runner linux-probe run-as dnslookup android-launcher android-clock android-calculator android-settings android-dialer android-music android-browser android-camera android-calendar android-keyboard
USER_ELF = $(addprefix userspace/,$(USER_PROGS))

# ── Rust backend (ow_http) ──
RUST_DIR = kernel/rust_ow
RUST_LIB = $(RUST_DIR)/target/x86_64-unknown-none/release/libow_http.a
RUST_TOOLCHAIN = /home/codeosuser/.rustup/toolchains/1.92.0-x86_64-unknown-linux-gnu/bin

$(RUST_LIB): $(wildcard $(RUST_DIR)/src/*.rs) $(RUST_DIR)/Cargo.toml
	cd $(RUST_DIR) && PATH="$(RUST_TOOLCHAIN):$$PATH" cargo build --target x86_64-unknown-none --release

# ── Rust compositor shell (HyperDE) ──
RUST_HD_DIR = kernel/rust_hyperde
RUST_HD_LIB = $(RUST_HD_DIR)/target/x86_64-unknown-none/release/libhyperde_shell.a

$(RUST_HD_LIB): $(wildcard $(RUST_HD_DIR)/src/*.rs) $(RUST_HD_DIR)/Cargo.toml
	cd $(RUST_HD_DIR) && PATH="$(RUST_TOOLCHAIN):$$PATH" cargo build --target x86_64-unknown-none --release

# ── Rust tiling window manager (penrose) ──
RUST_PR_DIR = kernel/rust_penrose
RUST_PR_LIB = $(RUST_PR_DIR)/target/x86_64-unknown-none/release/libpenrose.a

RUST_PR_SRCS = $(wildcard $(RUST_PR_DIR)/src/*.rs) \
               $(wildcard $(RUST_PR_DIR)/src/core/*.rs) \
               $(wildcard $(RUST_PR_DIR)/src/core/layout/*.rs) \
               $(wildcard $(RUST_PR_DIR)/src/pure/*.rs) \
               $(wildcard $(RUST_PR_DIR)/src/builtin/*.rs) \
               $(wildcard $(RUST_PR_DIR)/src/builtin/layout/*.rs)

$(RUST_PR_LIB): $(RUST_PR_SRCS) $(RUST_PR_DIR)/Cargo.toml
	cd $(RUST_PR_DIR) && PATH="$(RUST_TOOLCHAIN):$$PATH" cargo build --target x86_64-unknown-none --release

# ── Qt6 Panel Objects ──
QT6_DIR = ../qt6
QT6_PANELS_DIR = $(QT6_DIR)/panels
QT6_SYSROOT = $(QT6_DIR)/sysroot/usr
GCC_LIBDIR := $(dir $(shell $(CC) -print-libgcc-file-name 2>/dev/null))
QT6_LIBS = -L$(QT6_SYSROOT)/lib -lQt6Widgets -lQt6Gui -lQt6Core -lQt6BundledPcre2 -lQt6BundledZLIB -lcodeos_fonts -lcodeos_image -L$(GCC_LIBDIR) -lgcc
QT6_APP_OBJS = $(addprefix $(QT6_PANELS_DIR)/, \
    terminal.o app_host.o calc.o settings.o \
    about.o sys_info.o exit.o \
    sys_mon.o explorer.o \
    netbeam.o ziggy.o notes.o clock.o convert.o)

# Qt application sources live in pkgs/extra/qt_apps; their objects remain
# with the Qt frontend so the linker and incremental builds stay simple.

QT6_OBJS = $(QT6_PANELS_DIR)/codeos_platform.o $(QT6_PANELS_DIR)/codeos_plugin.o $(QT6_PANELS_DIR)/font_embed.o $(QT6_PANELS_DIR)/font_data.o $(QT6_PANELS_DIR)/codeos_fdb.o $(QT6_PANELS_DIR)/moc_qt_panels.o $(QT6_PANELS_DIR)/qt_panels_common.o $(QT6_PANELS_DIR)/qt_panels_shell.o $(QT6_APP_OBJS) $(QT6_PANELS_DIR)/qt_panels_openweb.o $(QT6_PANELS_DIR)/qt_panels_installer.o $(QT6_PANELS_DIR)/qt_panels_lt.o $(QT6_PANELS_DIR)/qt_panels_manager.o $(QT6_PANELS_DIR)/qt_panels_main.o $(QT6_PANELS_DIR)/ow_html.o $(QT6_PANELS_DIR)/openweb_core.o $(QT6_PANELS_DIR)/qt6_got.o $(QT6_PANELS_DIR)/codeos_font.o $(QT6_PANELS_DIR)/codeos_freetype.o $(QT6_PANELS_DIR)/codeos_image.o $(QT6_PANELS_DIR)/codeos_window_manager.o $(QT6_PANELS_DIR)/codeos_file_manager.o $(QT6_PANELS_DIR)/codeos_terminal.o $(QT6_PANELS_DIR)/moc_codeos_window_manager.o $(QT6_PANELS_DIR)/moc_codeos_file_manager.o $(QT6_PANELS_DIR)/moc_codeos_terminal.o

# Panels are built by a sub-make; a stamp file carries its result back to
# this make so source edits actually trigger recompilation (a bare
# "$(QT6_OBJS):" rule never fires once the .o files exist).
QT6_PANELS_STAMP = $(QT6_PANELS_DIR)/.built.stamp
QT6_PANELS_SRC = $(wildcard $(QT6_PANELS_DIR)/*.cpp) \
                 $(wildcard $(QT6_PANELS_DIR)/*.c) \
                 $(wildcard $(QT6_PANELS_DIR)/*.h) \
                 $(QT6_PANELS_DIR)/Makefile

# Panels objects are produced exclusively by the panels sub-make: they mix
# sources from qt6/platform, pkgs/core/panels/src and generated moc files,
# so this make must not compile them itself (a generic "%.o: %.cpp" match
# would silently rebuild them from stale duplicates with kernel flags).
# The stamp carries source-change information back to $(TARGET); the bare
# object rule below only fires when an object file is missing entirely.
QT6_PANELS_STAMP = $(QT6_PANELS_DIR)/.built.stamp
QT6_PANELS_SRC := $(shell find $(QT6_DIR) ../pkgs/core/panels ../pkgs/extra/qt_apps \
	\( -name '*.cpp' -o -name '*.c' -o -name '*.h' \) 2>/dev/null) \
	$(QT6_PANELS_DIR)/Makefile

$(QT6_PANELS_STAMP): $(QT6_PANELS_SRC)
	$(MAKE) -C $(QT6_PANELS_DIR)
	touch $@

$(QT6_OBJS):
	$(MAKE) -C $(QT6_PANELS_DIR)

OBJ_ALL = $(SRC_C:.c=.o) $(SRC_CXX:.cpp=.o) $(SRC_ASM:.S=.o) kernel/initramfs_files.o

# Auto-dependency files (rebuilt automatically by GCC -MMD -MP)
DEPFILES = $(OBJ_ALL:.o=.d)
-include $(DEPFILES)

# ── ARM64 overrides: nullify x86_64-only build dependencies ──
ifeq ($(ARCH),arm64)
    USER_ELF :=
    RUST_LIB :=
    RUST_HD_LIB :=
    RUST_PR_LIB :=
    QT6_OBJS :=
    QT6_LIBS :=
    OBJ_ALL  := $(SRC_C:.c=.o) $(SRC_CXX:.cpp=.o) $(SRC_ASM:.S=.o)
endif

.PHONY: all clean check run run-qemu iso arm64 arm64-run arm64-run-nographic sign-efi signed-iso run-sb sb-vars install-image install install-device run-installed

all: $(USER_ELF) $(TARGET) $(FLAT_BIN)

# Fast, side-effect-free environment validation for CI and contributors.
# Keep this separate from `all`: checking a checkout should not generate
# objects, download Rust crates, or require QEMU.
check:
	@command -v $(CC) >/dev/null || { echo "error: compiler not found: $(CC)" >&2; exit 1; }
	@command -v $(CXX) >/dev/null || { echo "error: C++ compiler not found: $(CXX)" >&2; exit 1; }
	@command -v $(LD) >/dev/null || { echo "error: linker not found: $(LD)" >&2; exit 1; }
	@command -v python3 >/dev/null || { echo "error: python3 not found" >&2; exit 1; }
	@command -v cargo >/dev/null || { echo "error: cargo not found (required for OpenWeb)" >&2; exit 1; }
	@echo "Build environment OK ($(CC), $(CXX), $(LD))"

arm64:
	$(MAKE) ARCH=arm64 all

$(USER_ELF):
	$(MAKE) -C userspace $(notdir $@)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Vendored LVGL: benign warnings (variables only used inside disabled asserts)
lvgl/src/%.o: CFLAGS += -Wno-unused-but-set-variable

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# cxx_support.cpp needs SSE for double return values (x86_64 ABI compliance)
kernel/cxx_support.o: kernel/cxx_support.cpp
	$(CXX) $(CXXFLAGS) -msse -msse2 -c $< -o $@

# iostream vtables: explicit instantiation against the host libstdc++
# headers (the x86_64-elf toolchain ships no libstdc++).  Must run in
# hosted mode (no -ffreestanding) so <sstream>/<locale> are available.
HOST_CXX ?= g++
HOST_CXX_VERSION := $(shell $(HOST_CXX) -dumpfullversion -dumpversion 2>/dev/null | cut -d. -f1)
HOST_CXX_MACHINE := $(shell $(HOST_CXX) -dumpmachine 2>/dev/null)
HOST_CXX_INCLUDE := /usr/include/c++/$(HOST_CXX_VERSION)
HOST_CXX_TARGET_INCLUDE := $(HOST_CXX_INCLUDE)/$(HOST_CXX_MACHINE)
HOST_GCC_INCLUDE := $(shell $(HOST_CXX) -print-file-name=include 2>/dev/null)
STDCXX_INCLUDES = \
    -isystem $(HOST_CXX_INCLUDE) \
    -isystem $(HOST_CXX_TARGET_INCLUDE) \
    -isystem $(QT6_SYSROOT)/include \
    -isystem $(QT6_SYSROOT)/include/stubs \
    -isystem $(HOST_GCC_INCLUDE)

kernel/stdcxx_support.o: kernel/stdcxx_support.cpp
	$(HOST_CXX) -mcmodel=large -mno-red-zone \
	    -fno-stack-protector -fno-PIC -fno-plt \
	    -Wall -Wextra -Wno-builtin-declaration-mismatch \
	    -O2 -g -MMD -MP \
	    -fno-exceptions -frtti -fno-threadsafe-statics \
	    -std=gnu++17 -msse -msse2 \
	    -c $< -o $@

arch/vectors.S: arch/genvectors.py
	python3 $< > $@

%.o: %.S
	$(AS) -g -o $@ $<

OBJ = $(OBJ_ALL) $(EMBED_OBJ)
PHASE1_OBJ = $(OBJ_ALL)

ifdef STAGE1_TARGET
$(STAGE1_TARGET): $(PHASE1_OBJ) $(RUST_LIB) $(RUST_HD_LIB) $(RUST_PR_LIB) $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ --start-group $(PHASE1_OBJ) $(RUST_LIB) $(RUST_HD_LIB) $(RUST_PR_LIB) $(OPENSSL_LDLIBS) --end-group
	@echo "Stage 1: $@"

kernel/embed_kernel.S: $(STAGE1_TARGET)
	python3 scripts/gen_embed.py $< $@

kernel/embed_kernel.o: kernel/embed_kernel.S
	$(AS) -g -o $@ $<

kernel/embed_limine.o: bootloader/limine-bios.sys
	$(LD) -r -b binary -o $@ $<

kernel/embed_limine_conf.o: bootloader/limine.conf
	$(LD) -r -b binary -o $@ $<

kernel/embed_splash.o: bootloader/splash.png
	$(LD) -r -b binary -o $@ $<
endif

$(TARGET): $(PHASE1_OBJ) $(EMBED_OBJ) $(RUST_LIB) $(RUST_HD_LIB) $(RUST_PR_LIB) $(QT6_OBJS) $(QT6_PANELS_STAMP) $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ --start-group $(PHASE1_OBJ) $(EMBED_OBJ) --whole-archive $(QT6_OBJS) --no-whole-archive $(RUST_LIB) $(RUST_HD_LIB) $(RUST_PR_LIB) $(QT6_LIBS) $(OPENSSL_LDLIBS) --end-group
	@echo "Built: $@"

# Flat binary for QEMU -kernel (multiboot2-compatible)
FLAT_BIN = $(TARGET:.bin=.flat.bin)
$(FLAT_BIN): $(TARGET)
	$(OBJCOPY) -O binary $< $@
	@echo "Flat binary: $@"

kernel/initramfs_files.c: $(USER_ELF)
	python3 scripts/gen_initramfs.py $^ > $@

# ─── ISO creation ───
# Override these if your grub-mkrescue and grub directory are elsewhere
GRUB_DIR ?= /usr/lib/grub/i386-pc
GRUB_MKRESCUE ?= grub-mkrescue

# GRUB-based ISO (works — multiboot2)
codeos-1-kernel-grub.iso: $(TARGET)
	rm -rf /tmp/grub-iso
	mkdir -p /tmp/grub-iso/boot/grub
	cp $(TARGET) /tmp/grub-iso/boot/
	printf 'set default=0\nset timeout=3\n\nmenuentry "CodeOS 1" {\n  multiboot2 /boot/codeos-1-kernel.bin\n}\n' \
		> /tmp/grub-iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -d $(GRUB_DIR) -o $@ /tmp/grub-iso
	@echo "GRUB ISO: $@"

# Limine-based ISO
ISODIR ?= /tmp/codeos-iso
LIMINE_DIR ?= .

codeos-1-kernel.iso: $(TARGET)
	rm -rf $(ISODIR)
	cp -r bootloader/iso_root $(ISODIR)
	cp $(TARGET) $(ISODIR)/boot/
	cp bootloader/limine.conf $(ISODIR)/boot/
	cp bootloader/splash.png $(ISODIR)/boot/
	cp bootloader/limine-bios-cd.bin $(ISODIR)/
	cp bootloader/limine-uefi-cd.bin $(ISODIR)/
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISODIR) -o $@
	./bootloader/limine-deploy bios-install $@
	@echo "Limine ISO: $@"

iso: codeos-1-kernel-grub.iso

# ─── QEMU run targets ───
QEMU_BASE ?= -machine q35 -m 16G -smp 4 -serial stdio
QEMU_USB ?= -device usb-ehci,id=ehci -device usb-tablet -device usb-kbd

run: $(TARGET) disk.img
	$(QEMU) $(QEMU_BASE) -kernel $(TARGET) $(QEMU_NET) $(QEMU_USB) $(QEMU_FULL_DISK) $(KVM)

run-nographic: $(TARGET) disk.img
	$(QEMU) -machine q35 -m 512M -nographic -kernel $(TARGET) $(QEMU_NET) $(QEMU_USB) $(KVM)

run-disk: $(TARGET) disk.img
	$(QEMU) $(QEMU_BASE) -kernel $(TARGET) $(QEMU_NET) $(QEMU_USB) $(QEMU_FULL_DISK) $(KVM)

run-wifi: $(TARGET)
	$(QEMU) $(QEMU_BASE) -kernel $(TARGET) $(QEMU_WIFI) $(QEMU_USB) $(QEMU_DISK) $(KVM)

run-qemu: $(TARGET)
	$(QEMU) -machine q35 -m 1G -serial stdio -kernel $(TARGET) $(KVM)

arm64-run: $(TARGET)
	$(QEMU) -machine virt -cpu max -m 512M -serial stdio -kernel $(TARGET)

arm64-run-nographic: $(TARGET)
	$(QEMU) -machine virt -cpu max -m 512M -nographic -kernel $(TARGET)

# Run with a display: ramfb framebuffer shown in a window (and reachable
# via 'screendump' through the QMP/monitor).
arm64-run-display: $(TARGET)
	$(QEMU) -machine virt -cpu max -m 512M -serial stdio \
		-device ramfb -kernel $(TARGET)

# Headless display verification: boots with ramfb and captures the rendered
# frame to /tmp/zircon.ppm for pixel inspection (useful in CI).
arm64-run-display-nogui: $(TARGET)
	$(QEMU) -machine virt -cpu max -m 512M -display none -device ramfb \
		-kernel $(TARGET) -serial file:/tmp/zircon-serial.log \
		-qmp unix:/tmp/zircon-qmp.sock,server,nowait

run-iso: codeos-1-kernel.iso
	$(QEMU) $(QEMU_BASE) -boot order=d -cdrom $< $(QEMU_NET) $(QEMU_USB) $(QEMU_DISK) $(KVM)

run-vbox: codeos-1-kernel.iso
	@echo "Running CodeOS in VirtualBox (provisions the 'CodeOS' VM, NAT net, IDE rootfs)..."
	@$(CURDIR)/../vbox.sh --headless

run-iso-grub: codeos-1-kernel-grub.iso
	$(QEMU) $(QEMU_BASE) -cdrom $< $(QEMU_NET) $(QEMU_USB) $(QEMU_DISK) $(KVM)

run-iso-nographic: codeos-1-kernel.iso
	$(QEMU) -machine q35 -m 512M -nographic -cdrom $< $(QEMU_NET) $(QEMU_DISK) $(KVM)

# ─── QEMU with ALL devices (AHCI, full hardware) ───
QEMU_FULL_DISK ?= -drive file=disk.img,format=raw,if=ide

run-full: $(TARGET) disk.img
	$(QEMU) -machine q35 -m 512M -serial stdio \
		-kernel $(TARGET) \
		$(QEMU_NET) \
		$(QEMU_FULL_DISK) \
		$(QEMU_USB) \
		$(KVM)

run-iso-full: codeos-1-kernel.iso
	$(QEMU) -machine q35 -m 512M -serial stdio \
		-cdrom codeos-1-kernel.iso \
		$(QEMU_NET) \
		$(QEMU_FULL_DISK) \
		$(QEMU_USB) \
		$(KVM)

# ─── QEMU with virtio-gpu (no VGA) ───
run-gpu: $(TARGET) disk.img
	$(QEMU) -machine q35 -m 4G -smp 4 -serial stdio \
		-device virtio-gpu-pci -vga none \
		-kernel $(TARGET) \
		$(QEMU_NET) $(QEMU_USB) $(QEMU_FULL_DISK) $(KVM)

# ─── Disk image creation ───
DISK_IMG ?= disk.img
DISK_SIZE_M ?= 64

disk.img:
	@echo "Creating disk image ($(DISK_SIZE_M) MB)..."
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE_M) 2>/dev/null
	parted -s $(DISK_IMG) mklabel msdos 2>/dev/null
	parted -s $(DISK_IMG) mkpart primary ext2 2048s 100% 2>/dev/null
	mkfs.ext2 -F -q -E offset=$$((2048 * 512)) $(DISK_IMG) 2>/dev/null
	@echo "Disk image ready ($(DISK_SIZE_M) MB, ext2 on partition 1)"

disk-img-format: disk.img
	@echo "Reformatting ext2 partition..."
	mkfs.ext2 -F -q -E offset=$$((2048 * 512)) $(DISK_IMG) 2>/dev/null
	@echo "Done."

disk-img-populate: disk.img
	@echo "Populating disk image with content..."
	-sudo losetup -P -f $(DISK_IMG) 2>/dev/null
	$(eval LOOP := $(shell sudo losetup -j $(CURDIR)/$(DISK_IMG) | cut -d: -f1))
	sudo mount $${LOOP}p1 /mnt 2>/dev/null
	sudo bash -c 'mkdir -p /mnt/scripts /mnt/etc /mnt/bin /mnt/home /mnt/lib'
	sudo cp /tmp/test_hello /mnt/bin/test_hello
	sudo cp $(CURDIR)/userspace/ld-codeos /mnt/lib/ld-codeos.so
	echo 'print "Hello from CodeOS disk!"' | sudo tee /mnt/scripts/test1.script >/dev/null
	echo 'let x = 42; print "The answer is " + x' | sudo tee /mnt/scripts/test2.script >/dev/null
	echo '#!/bin/script\nprint "CodeOS Scripting Engine Test"\nlet counter = 0\nwhile counter < 5 { print "Count: " + counter; let counter = counter + 1 }' | sudo tee /mnt/scripts/complex.script >/dev/null
	echo '# CodeOS configuration\nhostname=codeos\nuser=codeos' | sudo tee /mnt/etc/codeos.conf >/dev/null
	sudo chmod -R 755 /mnt/scripts /mnt/bin/test_hello /mnt/lib/ld-codeos.so
	sudo umount /mnt
	sudo losetup -d $${LOOP} 2>/dev/null
	@echo "Disk image populated."

disk-img-all: disk.img disk-img-format disk-img-populate

# ─── Installer: persistent disk install (not live-ISO). See scripts/install-codesos.sh ───
INSTALL_IMG ?= $(CURDIR)/../codeos-installed.img

# Build a bootable, installed HDD image you can boot directly (no -kernel/-cdrom).
install-image: $(TARGET)
	@bash $(CURDIR)/../scripts/install-codesos.sh make-image 128 $(INSTALL_IMG)

# Install onto a real block device.  Set INSTALL_DEV=/dev/sdX and run.
install: $(TARGET)
	@bash $(CURDIR)/../scripts/install-codesos.sh install $(INSTALL_DEV)

# Boot the installed image directly in QEMU (proves the install is bootable).
run-installed: install-image
	$(QEMU) $(QEMU_BASE) $(QEMU_NET) $(QEMU_USB) \
	-drive file=$(INSTALL_IMG),format=raw,if=ide \
	-boot c $(KVM)

virt-install: codeos-1-kernel.iso
	virt-install --connect qemu:///system \
		--name codeos-1 \
		--memory 256 \
		--vcpus 1 \
		--disk none \
		--cdrom $(CURDIR)/codeos-1-kernel.iso \
		--osinfo detect=on,require=off \
		--boot menu=on \
		--graphics vnc,listen=127.0.0.1 \
		--noautoconsole \
		--import

virt-manager:
	virt-manager &

PKG_NAME = codeos-1-learning-kernel
PKG_VER = 0.3
PKG_FILE = $(PKG_NAME)-$(PKG_VER).ftech

package: $(TARGET)
	rm -rf /tmp/$(PKG_NAME)
	mkdir -p /tmp/$(PKG_NAME)/pkg/usr/src/$(PKG_NAME)
	cp $(TARGET) /tmp/$(PKG_NAME)/pkg/usr/src/$(PKG_NAME)/
	cp ftech.json Makefile link.ld README /tmp/$(PKG_NAME)/pkg/usr/src/$(PKG_NAME)/ 2>/dev/null || true
	cp -r kernel drivers arch /tmp/$(PKG_NAME)/pkg/usr/src/$(PKG_NAME)/ 2>/dev/null || true
	cp -r fs /tmp/$(PKG_NAME)/pkg/usr/src/$(PKG_NAME)/ 2>/dev/null || true
	cp ftech.json /tmp/$(PKG_NAME)
	cd /tmp/$(PKG_NAME) && tar -czf $(CURDIR)/$(PKG_FILE) ftech.json pkg
	rm -rf /tmp/$(PKG_NAME)
	@echo "Package: $(PKG_FILE)"

# ─── Secure Boot ───
SB_DIR = bootloader/secureboot
SB_KEY = $(SB_DIR)/db.key
SB_CERT = $(SB_DIR)/db.crt
SB_UEFI_SIGNED = bootloader/limine-uefi-cd-signed.bin

$(SB_KEY) $(SB_CERT):
	mkdir -p $(SB_DIR)
	openssl req -new -x509 -newkey rsa:2048 \
		-subj "/CN=CodeOS Secure Boot Key/" \
		-keyout $(SB_DIR)/db.key -out $(SB_DIR)/db.crt \
		-days 3650 -nodes -sha256
	openssl req -new -x509 -newkey rsa:2048 \
		-subj "/CN=CodeOS KEK/" \
		-keyout $(SB_DIR)/KEK.key -out $(SB_DIR)/KEK.crt \
		-days 3650 -nodes -sha256
	openssl req -new -x509 -newkey rsa:2048 \
		-subj "/CN=CodeOS Platform Key/" \
		-keyout $(SB_DIR)/PK.key -out $(SB_DIR)/PK.crt \
		-days 3650 -nodes -sha256
	@echo "Secure Boot certificates created in $(SB_DIR)"

gen-certs: $(SB_KEY)

sign-efi: $(SB_UEFI_SIGNED)

$(SB_UEFI_SIGNED): bootloader/limine-uefi-cd.bin $(SB_KEY)
	@echo "Signing Limine UEFI bootloader..."
	mdel -i $< ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true
	mcopy -i $< ::/EFI/BOOT/BOOTX64.EFI /tmp/BOOTX64_unsigned.EFI
	sbsign --key $(SB_KEY) --cert $(SB_CERT) /tmp/BOOTX64_unsigned.EFI
	cp $< $@
	mdel -i $@ ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true
	mcopy -i $@ /tmp/BOOTX64_unsigned.EFI.signed ::/EFI/BOOT/BOOTX64.EFI
	@echo "Signed UEFI CD image: $@"

# Create a signed ISO (only the UEFI boot path is signed; BIOS path unchanged)
codeos-1-kernel-signed.iso: $(TARGET) $(SB_UEFI_SIGNED)
	rm -rf $(ISODIR)
	cp -r bootloader/iso_root $(ISODIR)
	cp $(TARGET) $(ISODIR)/boot/
	cp bootloader/limine.conf $(ISODIR)/boot/
	cp bootloader/splash.png $(ISODIR)/boot/
	cp bootloader/limine-bios-cd.bin $(ISODIR)/
	cp $(SB_UEFI_SIGNED) $(ISODIR)/limine-uefi-cd.bin
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISODIR) -o $@
	./bootloader/limine-deploy bios-install $@
	@echo "Signed ISO: $@"

signed-iso: codeos-1-kernel-signed.iso

# Create OVMF vars with our Secure Boot keys enrolled
# Uses OVMF+SecureBoot firmware and boots QEMU briefly to enroll keys
OVMF_SB_VARS = /tmp/sb-vars.fd

sb-vars: $(OVMF_SB_VARS)

$(OVMF_SB_VARS): $(SB_KEY)
	cp /usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd $@
	@echo "Created OVMF vars: $@"
	@echo "Boot with 'run-sb' to enroll keys via UEFI shell, or use:"
	@echo "  printf 'setvar -db -bs -nv -rt %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x -f $(SB_CERT)\n' 0xd719b2cb 0x3d3a 0x4596 0xa3 0xbc 0xda 0xd0 0x0e 0x67 0x65 0x6f"

# Run with OVMF secure boot firmware (uses signed ISO if available)
OVMF_CODE = /usr/share/edk2-ovmf/x64/OVMF_CODE.secboot.4m.fd
OVMF_VARS = /usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd

run-sb: codeos-1-kernel-signed.iso disk.img
	cp $(OVMF_VARS) /tmp/sb-vars.fd
	$(QEMU) -machine q35 -m 512M -serial stdio \
		-bios $(OVMF_CODE) \
		-drive file=/tmp/sb-vars.fd,if=pflash,format=raw,unit=1 \
		-cdrom codeos-1-kernel-signed.iso \
		$(QEMU_NET) $(QEMU_USB) $(QEMU_FULL_DISK) $(KVM)

run-sb-setup: codeos-1-kernel-signed.iso disk.img
	cp /usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd /tmp/sb-vars.fd
	$(QEMU) -machine q35 -m 512M -serial stdio \
		-bios $(OVMF_CODE) \
		-drive file=/tmp/sb-vars.fd,if=pflash,format=raw,unit=1 \
		-cdrom codeos-1-kernel-signed.iso \
		-fw_cfg name=opt/ovmf/PkPubKeyFile,file=$(SB_DIR)/PK.crt \
		-fw_cfg name=opt/ovmf/KEKPubKeyFile,file=$(SB_DIR)/KEK.crt \
		-fw_cfg name=opt/ovmf/EnrollDbPubKeyFile,file=$(SB_DIR)/db.crt \
		$(QEMU_NET) $(QEMU_USB) $(QEMU_FULL_DISK) $(KVM)

clean:
	rm -f $(OBJ) $(DEPFILES) codeos-1-kernel.bin codeos-1-kernel-arm64.bin $(STAGE1_TARGET) kernel/embed_kernel.S *.ftech
	rm -f codeos-1-kernel-signed.iso bootloader/limine-uefi-cd-signed.bin
	cd $(RUST_DIR) && cargo clean 2>/dev/null || true
	cd $(RUST_HD_DIR) && cargo clean 2>/dev/null || true
	$(MAKE) -C userspace clean
