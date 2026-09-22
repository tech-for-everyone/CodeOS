/* xora.c — In-kernel .xora package management
 *
 * A .xora archive is a gzip-compressed tar containing:
 *   manifest.json  — package metadata
 *   payload/<path> — files installed below filesystem root
 *
 * This kernel implementation reads raw tar directly (no gzip decompression)
 * and is used for .xora files that are either uncompressed or have been
 * pre-extracted by the host-side tools.  For compressed archives, use the
 * host-side `python3 scripts/xora.py` tool.
 *
 * Additionally, the kernel can install packages from the built-in static
 * repo tables (pkg.c) and materialize .ftech payloads without any archive
 * at all — this is the primary flow for `fetch -S`.
 */
#include "xora.h"
#include "kprintf.h"
#include "string.h"
#include "slab.h"
#include "fs.h"

/* ── Minimal tar header (ustar, 512-byte blocks) ── */
#define TAR_BLOCK_SIZE 512

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
} tar_header_t;

/* Parse an octal field from a tar header */
static uint32_t tar_oct(const char *s, int len) {
    uint32_t v = 0;
    for (int i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++)
        v = v * 8 + (uint32_t)(s[i] - '0');
    return v;
}

/* Check if data looks like a tar archive (ustar magic at block 0 or 257 offset) */
static int is_tar(const uint8_t *data, int len) {
    if (len < TAR_BLOCK_SIZE) return 0;
    const tar_header_t *th = (const tar_header_t *)data;
    if (memcmp(th->magic, "ustar", 5) == 0) return 1;
    /* Also check at offset 257 (some tar variants) */
    if (len > 257 + 5 && memcmp(data + 257, "ustar", 5) == 0) return 1;
    return 0;
}

/* Check if data looks like gzip */
static int is_gzip(const uint8_t *data, int len) {
    return (len >= 2 && data[0] == 0x1f && data[1] == 0x8b);
}

/* ── xora inspect ── */
void xora_inspect(const char *path) {
    if (!path || !path[0]) { kprintf("usage: xora inspect <file.xora>\n"); return; }

    char buf[128 * 1024];
    int flen = fs_read(path, buf, sizeof(buf) - 1);
    if (flen <= 0) { kprintf("xora: cannot read '%s'\n", path); return; }
    buf[flen] = 0;

    uint8_t *data = (uint8_t *)buf;
    int dlen = flen;

    if (is_gzip(data, dlen)) {
        kprintf("xora: '%s' is gzip-compressed — use host tools to decompress:\n", path);
        kprintf("  gzip -dc %s | tar tf -\n", path);
        kprintf("  Or: python3 scripts/xora.py inspect %s\n", path);
        return;
    }

    if (!is_tar(data, dlen)) {
        kprintf("xora: '%s' is not a valid tar or xora archive\n", path);
        return;
    }

    /* Parse tar — look for manifest.json */
    int found = 0;
    int count = 0;
    for (int off = 0; off + TAR_BLOCK_SIZE <= dlen; off += TAR_BLOCK_SIZE) {
        const tar_header_t *th = (const tar_header_t *)(data + off);
        if (th->name[0] == 0) break;
        if (memcmp(th->magic, "ustar", 5) != 0) break;

        uint32_t fsize = tar_oct(th->size, 12);

        if (strcmp(th->name, "manifest.json") == 0 && th->typeflag == '0') {
            int mlen = fsize;
            if (mlen > dlen - off - TAR_BLOCK_SIZE)
                mlen = dlen - off - TAR_BLOCK_SIZE;
            const char *mstart = (const char *)(data + off + TAR_BLOCK_SIZE);
            kprintf("═══════════════════════════════════════\n");
            kprintf(" xora archive: %s\n", path);
            kprintf("═══════════════════════════════════════\n");
            if (mlen > 2048) mlen = 2048;
            kprintf("%.*s\n", mlen, mstart);
            kprintf("───────────────────────────────────────\n");
            found = 1;
        }
        count++;

        int data_blocks = (fsize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        off += data_blocks * TAR_BLOCK_SIZE;
    }

    if (!found) {
        kprintf("xora: '%s' has no manifest.json (%d entries)\n", path, count);
    }
}

/* ── xora install ── */
void xora_install(const char *path) {
    if (!path || !path[0]) { kprintf("usage: xora install <file.xora>\n"); return; }

    char buf[128 * 1024];
    int flen = fs_read(path, buf, sizeof(buf) - 1);
    if (flen <= 0) { kprintf("xora: cannot read '%s'\n", path); return; }
    buf[flen] = 0;

    uint8_t *data = (uint8_t *)buf;
    int dlen = flen;

    if (is_gzip(data, dlen)) {
        kprintf("xora: cannot install gzip-compressed archives in-kernel.\n");
        kprintf("  Decompress first: gzip -dc %s > /tmp/pkg.tar && xora install /tmp/pkg.tar\n", path);
        return;
    }

    if (!is_tar(data, dlen)) {
        kprintf("xora: '%s' is not a valid tar archive\n", path);
        return;
    }

    /* First pass: read manifest for package name */
    char pkg_name[64] = {0};
    char pkg_version[64] = {0};
    for (int off = 0; off + TAR_BLOCK_SIZE <= dlen; off += TAR_BLOCK_SIZE) {
        const tar_header_t *th = (const tar_header_t *)(data + off);
        if (th->name[0] == 0) break;
        if (memcmp(th->magic, "ustar", 5) != 0) break;
        uint32_t fsize = tar_oct(th->size, 12);

        if (strcmp(th->name, "manifest.json") == 0 && th->typeflag == '0') {
            const char *m = (const char *)(data + off + TAR_BLOCK_SIZE);
            int mlen = fsize;
            if (mlen > dlen - off - TAR_BLOCK_SIZE)
                mlen = dlen - off - TAR_BLOCK_SIZE;
            /* Quick JSON parse for "name" and "version" */
            for (int i = 0; i + 6 < mlen; i++) {
                if (memcmp(m + i, "\"name\"", 6) == 0) {
                    const char *c = m + i + 6;
                    while (c < m + mlen && (*c == ' ' || *c == ':')) c++;
                    if (*c == '"') {
                        c++;
                        int j = 0;
                        while (c < m + mlen && *c != '"' && j < 63) pkg_name[j++] = *c++;
                        pkg_name[j] = 0;
                    }
                    break;
                }
            }
            for (int i = 0; i + 8 < mlen; i++) {
                if (memcmp(m + i, "\"version\"", 8) == 0) {
                    const char *c = m + i + 8;
                    while (c < m + mlen && (*c == ' ' || *c == ':')) c++;
                    if (*c == '"') {
                        c++;
                        int j = 0;
                        while (c < m + mlen && *c != '"' && j < 63) pkg_version[j++] = *c++;
                        pkg_version[j] = 0;
                    }
                    break;
                }
            }
            break;
        }
        int data_blocks = (fsize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        off += data_blocks * TAR_BLOCK_SIZE;
    }

    if (!pkg_name[0]) {
        /* Fallback: use filename without extension */
        const char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        strncpy_safe(pkg_name, base, sizeof(pkg_name));
        char *dot = strrchr(pkg_name, '.');
        if (dot) *dot = 0;
    }

    kprintf("Installing %s %s...\n", pkg_name, pkg_version);

    /* Second pass: extract payload/ files */
    int extracted = 0;
    for (int off = 0; off + TAR_BLOCK_SIZE <= dlen; off += TAR_BLOCK_SIZE) {
        const tar_header_t *th = (const tar_header_t *)(data + off);
        if (th->name[0] == 0) break;
        if (memcmp(th->magic, "ustar", 5) != 0) break;
        uint32_t fsize = tar_oct(th->size, 12);
        const uint8_t *fdata = data + off + TAR_BLOCK_SIZE;

        if (th->typeflag == '0' || th->typeflag == '\0') {
            if (strncmp(th->name, "payload/", 8) == 0) {
                const char *relpath = th->name + 7; /* skip "payload" */
                char fullpath[256];
                snprintf(fullpath, sizeof(fullpath), "/%s", relpath);
                fs_mkfile(fullpath);
                int w = fs_write(fullpath, (const char *)fdata, (int)fsize);
                if (w >= 0) extracted++;
                else kprintf("  warning: failed to write %s\n", fullpath);
            }
        }

        int data_blocks = (fsize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        off += data_blocks * TAR_BLOCK_SIZE;
    }

    /* Record in package database */
    extern int pkg_install(const char *name, const char *version,
                           const char *desc, uint32_t size);
    pkg_install(pkg_name, pkg_version, "installed via xora", (uint32_t)flen);

    kprintf("✓ %s %s installed (%d files)\n", pkg_name, pkg_version, extracted);
}

/* ── xora pack ── */
void xora_pack(const char *source_dir, const char *output) {
    if (!source_dir || !output) {
        kprintf("usage: xora pack <source_dir> <output.xora>\n");
        return;
    }
    kprintf("xora: use host-side tools to create .xora archives:\n");
    kprintf("  make xora-pack XORA_SOURCE=%s XORA_OUTPUT=%s\n", source_dir, output);
    kprintf("  Or: python3 scripts/xora.py pack %s --output %s\n", source_dir, output);
}

/* ── xora list ── */
void xora_list(void) {
    extern void pkg_list(void);
    pkg_list();
}

/* ── Shell command ── */
void cmd_xora(int argc, char **argv) {
    if (argc < 2) {
        kprintf("xora - CodeOS package archive tool\n");
        kprintf("\nCommands:\n");
        kprintf("  xora inspect <file.xora>  Show archive contents\n");
        kprintf("  xora install <file.xora>  Install package from archive\n");
        kprintf("  xora pack <dir> <out>     Pack directory (host-side only)\n");
        kprintf("  xora list                 List installed packages\n");
        kprintf("\nNote: compressed .xora files need host-side tools.\n");
        kprintf("  python3 scripts/xora.py install <file.xora> --root .\n");
        return;
    }

    if (strcmp(argv[1], "inspect") == 0) {
        if (argc < 3) { kprintf("usage: xora inspect <file.xora>\n"); return; }
        xora_inspect(argv[2]);
    } else if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) { kprintf("usage: xora install <file.xora>\n"); return; }
        xora_install(argv[2]);
    } else if (strcmp(argv[1], "pack") == 0) {
        if (argc < 4) { kprintf("usage: xora pack <source_dir> <output.xora>\n"); return; }
        xora_pack(argv[2], argv[3]);
    } else if (strcmp(argv[1], "list") == 0) {
        xora_list();
    } else {
        kprintf("xora: unknown subcommand '%s'\n", argv[1]);
        kprintf("Try 'xora' for help\n");
    }
}

/* ── net-test: layer-by-layer network diagnostic ── */
#include "icmp.h"
#include "tcp.h"
#include "dns.h"
#include "net_internal.h"
#include "nic.h"
#include "arp.h"

/* Extern declarations for net.c functions */
extern int http_get(const char *host, uint16_t port,
                    const char *path, void *buf, uint16_t max_len);

void cmd_nettest(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("═══ Network Diagnostic ═══\n\n");

    /* Layer 1: NIC */
    uint8_t mac[6];
    nic_get_mac(mac);
    kprintf("[NIC]     MAC=%02x:%02x:%02x:%02x:%02x:%02x  up=%d\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], nic_ready());

    /* Layer 2: ARP */
    uint32_t gw = net_get_gateway();
    uint8_t gw_mac[6];
    int arp_ok = arp_resolve(gw, gw_mac);
    kprintf("[ARP]     gateway=%d.%d.%d.%d -> %s",
            (gw >> 0) & 0xFF, (gw >> 8) & 0xFF,
            (gw >> 16) & 0xFF, (gw >> 24) & 0xFF,
            arp_ok == 0 ? "OK" : "FAIL");
    if (arp_ok == 0)
        kprintf(" [%02x:%02x:%02x:%02x:%02x:%02x]",
                gw_mac[0], gw_mac[1], gw_mac[2], gw_mac[3], gw_mac[4], gw_mac[5]);
    kprintf("\n");

    /* Layer 3: ICMP ping gateway */
    int ping_ms = icmp_ping(gw, 2000);
    kprintf("[ICMP]    ping gateway: %s",
            ping_ms >= 0 ? "OK" : "FAIL");
    if (ping_ms >= 0) kprintf(" (%d ms)", ping_ms);
    kprintf("\n");

    /* Layer 4: DNS */
    uint32_t dns_server = net_get_dns();
    uint32_t resolved;
    int dns_ok = dns_resolve("10.0.2.3", &resolved);
    kprintf("[DNS]     server=%d.%d.%d.%d resolve(10.0.2.3): %s",
            (dns_server >> 0) & 0xFF, (dns_server >> 8) & 0xFF,
            (dns_server >> 16) & 0xFF, (dns_server >> 24) & 0xFF,
            dns_ok == 0 ? "OK" : "FAIL");
    if (dns_ok == 0)
        kprintf(" -> %d.%d.%d.%d",
                (resolved >> 0) & 0xFF, (resolved >> 8) & 0xFF,
                (resolved >> 16) & 0xFF, (resolved >> 24) & 0xFF);
    kprintf("\n");

    /* Layer 5: TCP connect to gateway (port 80) */
    int tcp_fd = tcp_connect(gw, 80);
    kprintf("[TCP]     connect gateway:80: %s\n",
            tcp_fd >= 0 ? "OK" : "FAIL");
    if (tcp_fd >= 0) tcp_close(tcp_fd);

    /* Layer 6: HTTP GET */
    char http_buf[256];
    int http_len = http_get("10.0.2.2", 80, "/", http_buf, sizeof(http_buf) - 1);
    kprintf("[HTTP]    GET 10.0.2.2:80/: %s",
            http_len > 0 ? "OK" : "FAIL");
    if (http_len > 0) {
        http_buf[http_len] = 0;
        kprintf(" (%d bytes)", http_len);
        char *nl = memchr(http_buf, '\n', http_len);
        if (nl) *nl = 0;
        kprintf("\n          response: %.80s", http_buf);
    }
    kprintf("\n");

    kprintf("\n═══ done ═══\n");
}
