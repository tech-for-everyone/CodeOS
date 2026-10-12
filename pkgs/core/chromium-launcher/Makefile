# Chromium Launcher package for CodeOS
# Builds the CDP bridge for the debian-minimal container

CC = x86_64-linux-gnu-gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -lpthread

all: cdp_bridge chromium_launcher

# CDP Bridge - runs inside debian-minimal container
cdp_bridge: src/cdp_bridge.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Chromium launcher - runs on CodeOS host
chromium_launcher: src/chromium_launcher.c
	$(CC) $(CFLAGS) -o $@ $<

# Embedder library (for future use)
chromium_embedder.o: src/chromium_embedder.c
	$(CC) $(CFLAGS) -c -fPIC -o $@ $<

clean:
	rm -f cdp_bridge chromium_launcher chromium_embedder.o

install: cdp_bridge
	# Copy cdp_bridge to container image location
	mkdir -p ../../../kernel/disk/usr/share/codeos/chromium
	cp cdp_bridge ../../../kernel/disk/usr/share/codeos/chromium/

.PHONY: all clean install