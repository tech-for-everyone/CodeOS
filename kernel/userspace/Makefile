ARCH ?= x86_64

ifeq ($(ARCH),arm64)
CC = aarch64-linux-musl-gcc
LD = aarch64-linux-musl-ld
AS = aarch64-linux-musl-gcc
else
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
AS = x86_64-elf-as
endif

PKG_SRC = ../../pkgs/core

# Packages live under two roots: pkgs/core is canonical, pkgs/extra holds
# the optional/Android/Wine tier plus the Qt apps.  PROGRAMS below lists
# bare names, so each one has to be resolved against both roots in order
# -- otherwise moving a package into pkgs/extra silently removes it from
# the build ("No rule to make target '../../pkgs/core/<name>/src/<name>.c'")
# even though the source is present in the tree.
PKG_ROOTS = $(PKG_SRC) ../../pkgs/extra

# Path to package $(1)'s main source, first existing root wins.  The
# wildcard has to run per root inside a foreach: writing it as
# $(wildcard $(PKG_ROOTS)/$(1)/...) expands PKG_ROOTS to a space-separated
# list first, so the "/$(1)/src/$(1).c" suffix would attach only to the
# last root and the bare directory "pkgs/core" would win -- which make
# accepts, so the build would compile a directory instead of failing.
#
# The trailing fallback keeps the canonical pkgs/core path in the error
# message when the package is in neither root, which is a far more useful
# diagnostic than a bare "No rule to make target".
pkg_main = $(firstword $(foreach r,$(PKG_ROOTS),$(wildcard $(r)/$(1)/src/$(1).c)) $(PKG_SRC)/$(1)/src/$(1).c)

USER_CFLAGS = -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
              -nostdlib -nostartfiles -fno-stack-protector -fno-PIC \
              -Wall -Wextra -Werror -O2 -g \
              -I./include -I../../include -I$(PKG_SRC)/panels/src

USER_LDFLAGS = -nostdlib -z max-page-size=0x1000 -z noexecstack

PROGRAMS = init shell csl zircapp zircon-notify zircon-clock zircon-info \
           apk-parser android-container android-apps code-music \
           linux-runner ld-codeos terminal syscall_test openweb \
           wineserver wine-runner linux-probe run-as dnslookup \
           crosvm-launcher ncvm \
           android-launcher android-clock android-calculator \
           android-settings android-dialer android-music android-browser \
           android-camera android-calendar android-keyboard

all: $(PROGRAMS)

crt0.o: crt0.S
	$(AS) -g -o $@ $<

lib/string.o: lib/string.c include/string.h
	$(CC) $(USER_CFLAGS) -c -o $@ $<

lib/stdio.o: lib/stdio.c include/unistd.h include/string.h include/stdlib.h
	$(CC) $(USER_CFLAGS) -c -o $@ $<

lib/stdlib.o: lib/stdlib.c include/unistd.h
	$(CC) $(USER_CFLAGS) -c -o $@ $<

lib/android.o: lib/android.c include/android.h include/unistd.h include/string.h
	$(CC) $(USER_CFLAGS) -c -o $@ $<

lib/android_ui.o: lib/android_ui.c include/android_ui.h include/wm_protocol.h include/unistd.h include/string.h include/stdio.h
	$(CC) $(USER_CFLAGS) -c -o $@ $<

lib/errno.o: lib/errno.c include/unistd.h include/errno.h include/string.h
	$(CC) $(USER_CFLAGS) -c -o $@ $<

LIB_OBJ = lib/string.o lib/stdio.o lib/stdlib.o lib/errno.o
LIB_ANDROID = lib/android.o lib/android_ui.o

# Programs that only need core libs
CORE_PROGS = init shell csl zircon-notify zircon-clock zircon-info \
             code-music linux-runner syscall_test crosvm-launcher ncvm android-apps \
             wineserver wine-runner run-as dnslookup leveltest netbeam
# Programs that also need android lib
ANDROID_PROGS = apk-parser android-container android-launcher \
                android-clock android-calculator android-settings \
                android-dialer android-music android-browser \
                android-camera android-calendar android-keyboard

define core-prog-rule
$(1): $$(call pkg_main,$(1)) crt0.o $$(LIB_OBJ)
	$$(CC) $$(USER_CFLAGS) -c -o $(1).o $$(call pkg_main,$(1))
	$$(LD) $$(USER_LDFLAGS) -T link.ld -o $$@ $(1).o crt0.o $$(LIB_OBJ)
endef
$(foreach p,$(CORE_PROGS),$(eval $(call core-prog-rule,$(p))))

# zircapp is not a normal single-source package.  The CodeOS host
# (pkgs/core/zircapp) links the *unmodified* Zircon application framework
# and its apps from ../../Zircon, so it needs both include trees.  The
# ZircApp port directory is listed first so its <time.h> shim (ctime)
# shadows the CodeOS one; -Werror is dropped because the Zircon sources
# are not warning-clean under this freestanding toolchain.
ZC_ROOT = ../../Zircon
ZC_CFLAGS = -I$(ZC_ROOT)/zircapp/port -I$(ZC_ROOT) \
            -I$(ZC_ROOT)/gui -I$(ZC_ROOT)/apps \
            $(USER_CFLAGS) -Wno-error
ZC_OBJS = zircapp_host.o zircapp_platform.o zircapp_ow_stub.o \
          zc_zircapp.o zc_windows.o \
          zc_home.o zc_phone.o zc_sms.o zc_settings.o zc_camera.o \
          zc_clock.o zc_calculator.o zc_music.o zc_gallery.o zc_browser.o

zircapp: $(PKG_SRC)/zircapp/src/zircapp.c \
         $(PKG_SRC)/zircapp/src/zircapp_platform.c \
         $(PKG_SRC)/zircapp/src/zircapp_ow_stub.c \
         $(ZC_ROOT)/zircapp/zircapp.c $(ZC_ROOT)/gui/windows.c \
         $(ZC_ROOT)/apps/home.c $(ZC_ROOT)/apps/phone.c \
         $(ZC_ROOT)/apps/sms.c $(ZC_ROOT)/apps/settings.c \
         $(ZC_ROOT)/apps/camera.c $(ZC_ROOT)/apps/clock.c \
         $(ZC_ROOT)/apps/calculator.c $(ZC_ROOT)/apps/music.c \
         $(ZC_ROOT)/apps/gallery.c $(ZC_ROOT)/apps/browser.c \
         $(wildcard $(ZC_ROOT)/zircapp/port/*.h) \
         crt0.o $(LIB_OBJ)
	$(CC) $(ZC_CFLAGS) -c -o zircapp_host.o $(PKG_SRC)/zircapp/src/zircapp.c
	$(CC) $(ZC_CFLAGS) -c -o zircapp_platform.o $(PKG_SRC)/zircapp/src/zircapp_platform.c
	$(CC) $(ZC_CFLAGS) -c -o zircapp_ow_stub.o $(PKG_SRC)/zircapp/src/zircapp_ow_stub.c
	$(CC) $(ZC_CFLAGS) -c -o zc_zircapp.o $(ZC_ROOT)/zircapp/zircapp.c
	$(CC) $(ZC_CFLAGS) -c -o zc_windows.o $(ZC_ROOT)/gui/windows.c
	$(CC) $(ZC_CFLAGS) -c -o zc_home.o $(ZC_ROOT)/apps/home.c
	$(CC) $(ZC_CFLAGS) -c -o zc_phone.o $(ZC_ROOT)/apps/phone.c
	$(CC) $(ZC_CFLAGS) -c -o zc_sms.o $(ZC_ROOT)/apps/sms.c
	$(CC) $(ZC_CFLAGS) -c -o zc_settings.o $(ZC_ROOT)/apps/settings.c
	$(CC) $(ZC_CFLAGS) -c -o zc_camera.o $(ZC_ROOT)/apps/camera.c
	$(CC) $(ZC_CFLAGS) -c -o zc_clock.o $(ZC_ROOT)/apps/clock.c
	$(CC) $(ZC_CFLAGS) -c -o zc_calculator.o $(ZC_ROOT)/apps/calculator.c
	$(CC) $(ZC_CFLAGS) -c -o zc_music.o $(ZC_ROOT)/apps/music.c
	$(CC) $(ZC_CFLAGS) -c -o zc_gallery.o $(ZC_ROOT)/apps/gallery.c
	$(CC) $(ZC_CFLAGS) -c -o zc_browser.o $(ZC_ROOT)/apps/browser.c
	$(LD) $(USER_LDFLAGS) -T link.ld -o $@ $(ZC_OBJS) crt0.o $(LIB_OBJ)

define android-prog-rule
$(1): $$(call pkg_main,$(1)) crt0.o $$(LIB_OBJ) $$(LIB_ANDROID)
	$$(CC) $$(USER_CFLAGS) -c -o $(1).o $$(call pkg_main,$(1))
	$$(LD) $$(USER_LDFLAGS) -T link.ld -o $$@ $(1).o crt0.o $$(LIB_OBJ) $$(LIB_ANDROID)
endef
$(foreach p,$(ANDROID_PROGS),$(eval $(call android-prog-rule,$(p))))

TERM = $(PKG_SRC)/terminal/src
terminal: $(TERM)/terminal.c crt0.o $(LIB_OBJ)
	$(CC) $(USER_CFLAGS) -c -o terminal.o $(TERM)/terminal.c
	$(LD) $(USER_LDFLAGS) -T link.ld -o $@ terminal.o crt0.o $(LIB_OBJ)

OPENWEB_SRC = $(PKG_SRC)/openweb/src
openweb: $(OPENWEB_SRC)/openweb.c $(OPENWEB_SRC)/ow_html.c crt0.o $(LIB_OBJ)
	$(CC) $(USER_CFLAGS) -c -o openweb.o $(OPENWEB_SRC)/openweb.c
	$(CC) $(USER_CFLAGS) -c -o ow_html.o $(OPENWEB_SRC)/ow_html.c
	$(LD) $(USER_LDFLAGS) -T link.ld -o $@ openweb.o ow_html.o crt0.o $(LIB_OBJ)

# ld-codeos: userspace dynamic linker (standalone, no crt0 or libc)
LD_SRC = $(PKG_SRC)/ld-codeos/src
ld-codeos: $(LD_SRC)/ld-codeos.c
	$(CC) $(USER_CFLAGS) -c -o ld-codeos.o $(LD_SRC)/ld-codeos.c
	$(LD) $(USER_LDFLAGS) -T link-ld-codeos.ld -o $@ ld-codeos.o

# linux-probe: freestanding Linux-ABI syscall probe (own _start, no crt0/libc)
PSRC = $(PKG_SRC)/linux-probe/src
linux-probe: $(PSRC)/linux-probe.c
	$(CC) $(USER_CFLAGS) -fno-pic -c -o linux-probe.o $(PSRC)/linux-probe.c
	$(LD) $(USER_LDFLAGS) -T link.ld -o $@ linux-probe.o

clean:
	rm -f *.o lib/*.o $(PROGRAMS)

# Host tests. These build the *real* source against a POSIX shim rather than
# the freestanding CodeOS target, because the only other way to exercise these
# programs is to boot the ISO (~170 s per run), which makes "built but never
# run" the path of least resistance. apk-parser is the worked example: the
# first version of this file read 8 KiB from offset 0 and so could never find
# a ZIP End Of Central Directory record, yet it compiled clean and rejected
# every real APK. See pkgs/extra/apk-parser/tests/run.sh.
check: apk-parser-check

apk-parser-check:
	@sh ../../pkgs/extra/apk-parser/tests/run.sh

.PHONY: all clean check apk-parser-check
