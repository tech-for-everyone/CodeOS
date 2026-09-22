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

USER_CFLAGS = -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
              -nostdlib -nostartfiles -fno-stack-protector -fno-PIC \
              -Wall -Wextra -Werror -O2 -g \
              -I./include -I../../include -I$(PKG_SRC)/panels/src

USER_LDFLAGS = -nostdlib -z max-page-size=0x1000 -z noexecstack

PROGRAMS = init shell csl zircon-notify zircon-clock zircon-info \
           apk-parser android-container android-apps code-music \
           linux-runner ld-codeos terminal syscall_test openweb \
           wineserver wine-runner linux-probe run-as dnslookup \
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
             code-music linux-runner syscall_test crosvm-launcher android-apps \
             wineserver wine-runner run-as dnslookup
# Programs that also need android lib
ANDROID_PROGS = apk-parser android-container android-launcher \
                android-clock android-calculator android-settings \
                android-dialer android-music android-browser \
                android-camera android-calendar android-keyboard

define core-prog-rule
$(1): $(PKG_SRC)/$(1)/src/$(1).c crt0.o $$(LIB_OBJ)
	$$(CC) $$(USER_CFLAGS) -c -o $(1).o $$(PKG_SRC)/$(1)/src/$(1).c
	$$(LD) $$(USER_LDFLAGS) -T link.ld -o $$@ $(1).o crt0.o $$(LIB_OBJ)
endef
$(foreach p,$(CORE_PROGS),$(eval $(call core-prog-rule,$(p))))

define android-prog-rule
$(1): $(PKG_SRC)/$(1)/src/$(1).c crt0.o $$(LIB_OBJ) $$(LIB_ANDROID)
	$$(CC) $$(USER_CFLAGS) -c -o $(1).o $$(PKG_SRC)/$(1)/src/$(1).c
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

.PHONY: all clean
