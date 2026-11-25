/* unzip — list and extract stored ZIP archives on CodeOS.
 *
 * Port notes:
 *   - Info-ZIP 6.0 is a large POSIX/C tree; this is a compact freestanding
 *     reader for the same format. The upstream tarball is kept in
 *     ports/unzip/upstream/ for reference.
 *   - Only method 0 (stored) is supported. Deflate-compressed members are
 *     identified and rejected with a clear message rather than silently
 *     extracted as garbage. `gzip`/`zip` archives made by this tree work.
 *   - The End Of Central Directory is found by a backward scan that validates
 *     its comment length, so archives with comments are handled correctly.
 *   - The kernel caps files at 64 KiB, so the whole archive must fit in memory.
 *
 * Usage: unzip [-l|-p] ARCHIVE [MEMBER...]
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include <stdint.h>

#define ZIP_MAX_ARCHIVE 65536         /* kernel FS_CONTENT_MAX */
#define ZIP_MAX_NAME    200
#define ZIP_MAX_PATH    240
#define ZIP_LOCAL_HDR   30
#define ZIP_CENTRAL_HDR 46
#define ZIP_EOCD        22

static uint8_t arc[ZIP_MAX_ARCHIVE];
static size_t arc_len;

static unsigned read16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

static int read_archive(const char *path) {
    int fd = sys_open(path, 0);
    if (fd < 0) { puts("unzip: cannot open archive"); return -1; }
    arc_len = 0;
    for (;;) {
        if (arc_len >= sizeof(arc)) { sys_close(fd); puts("unzip: archive too large"); return -1; }
        int n = sys_read(fd, arc + arc_len, (int)(sizeof(arc) - arc_len));
        if (n <= 0) break;
        arc_len += (size_t)n;
    }
    sys_close(fd);
    if (arc_len < ZIP_EOCD) { puts("unzip: not a ZIP archive"); return -1; }
    return 0;
}

/* Accept a candidate only when its declared comment ends exactly at EOF;
 * the signature also occurs by chance inside compressed payloads. */
static long find_eocd(void) {
    for (long i = (long)arc_len - ZIP_EOCD; i >= 0; i--) {
        if (arc[i] != 0x50 || arc[i + 1] != 0x4B ||
            arc[i + 2] != 0x05 || arc[i + 3] != 0x06) continue;
        unsigned comment = read16(arc + i + 20);
        if (i + ZIP_EOCD + comment == (long)arc_len) return i;
    }
    return -1;
}

static int member_name(const uint8_t *h, char *out, size_t cap) {
    unsigned nlen = read16(h + 28);
    if (nlen == 0 || nlen >= cap) return -1;
    size_t k = 0;
    for (; k < nlen; k++) out[k] = (char)h[ZIP_CENTRAL_HDR + k];
    out[k] = 0;

    unsigned char *p = (unsigned char *)out;
    if (p[0] == '/' || p[0] == '\\') return -1;
    size_t start = 0;
    for (size_t i = 0; i <= k; i++) {
        if (i != k && out[i] != '/' && out[i] != '\\') continue;
        if (i - start == 2 && out[start] == '.' && out[start + 1] == '.') return -1;
        start = i + 1;
    }
    return 0;
}

static int make_parents(const char *path) {
    char dir[ZIP_MAX_PATH + 1];
    for (size_t i = 1; path[i]; i++) {
        if (path[i] != '/') continue;
        size_t n = i < ZIP_MAX_PATH ? i : ZIP_MAX_PATH;
        memcpy(dir, path, n);
        dir[n] = 0;
        sys_mkdir(dir);
    }
    return 0;
}

static int write_member(const char *path, const uint8_t *data, size_t len) {
    int fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("unzip: cannot create %s\n", path); return -1; }
    size_t off = 0;
    while (off < len) {
        int chunk = (int)(len - off);
        if (chunk > 1024) chunk = 1024;
        int n = sys_pwrite(fd, data + off, chunk);
        if (n <= 0) { sys_close(fd); printf("unzip: write failed for %s\n", path); return -1; }
        off += (size_t)n;
    }
    sys_close(fd);
    return 0;
}

static int wanted(const char *name, int argc, char **argv, int first) {
    if (first >= argc) return 1;
    for (int i = first; i < argc; i++) if (strcmp(argv[i], name) == 0) return 1;
    return 0;
}

static int process(int list_only, int to_stdout, int argc, char **argv, int first) {
    long eocd = find_eocd();
    if (eocd < 0) { puts("unzip: no ZIP End Of Central Directory found"); return -1; }

    unsigned count = read16(arc + eocd + 10);
    long p = (long)read32(arc + eocd + 16);
    int handled = 0;

    for (unsigned i = 0; i < count; i++) {
        if (p + ZIP_CENTRAL_HDR > (long)arc_len ||
            read32(arc + p) != 0x02014b50u) {
            puts("unzip: corrupt central directory");
            return -1;
        }
        char name[ZIP_MAX_NAME + 1];
        if (member_name(arc + p, name, sizeof(name))) {
            puts("unzip: unsafe or invalid member name");
            return -1;
        }
        unsigned method = read16(arc + p + 10);
        uint32_t crc = read32(arc + p + 16);
        uint32_t comp = read32(arc + p + 20);
        uint32_t size = read32(arc + p + 24);
        unsigned nlen = read16(arc + p + 28);
        unsigned xlen = read16(arc + p + 30);
        unsigned clen = read16(arc + p + 32);
        uint32_t local = read32(arc + p + 42);
        if (p + ZIP_CENTRAL_HDR + nlen + xlen + clen > (long)arc_len) {
            puts("unzip: truncated central directory");
            return -1;
        }

        int is_dir = (name[0] && name[strlen(name) - 1] == '/');
        if (!wanted(name, argc, argv, first)) {
            p += ZIP_CENTRAL_HDR + nlen + xlen + clen;
            continue;
        }
        handled++;

        if (list_only) {
            printf("%s  %u bytes  method %u\n", name, size, method);
            p += ZIP_CENTRAL_HDR + nlen + xlen + clen;
            continue;
        }
        if (is_dir) {
            make_parents(name);
            sys_mkdir(name);
            p += ZIP_CENTRAL_HDR + nlen + xlen + clen;
            continue;
        }
        if (method != 0) {
            printf("unzip: %s uses compression method %u (only stored supported)\n",
                   name, method);
            return -1;
        }
        if (local + ZIP_LOCAL_HDR > (long)arc_len ||
            read32(arc + local) != 0x04034b50u) {
            printf("unzip: %s has a bad local header\n", name);
            return -1;
        }
        unsigned lnlen = read16(arc + local + 26);
        unsigned lxlen = read16(arc + local + 28);
        long data_off = (long)local + ZIP_LOCAL_HDR + lnlen + lxlen;
        if (comp != size || size > ZIP_MAX_ARCHIVE ||
            data_off + size > (long)arc_len) {
            printf("unzip: %s has inconsistent sizes\n", name);
            return -1;
        }
        const uint8_t *data = arc + data_off;
        if (crc32(data, size) != crc) {
            printf("unzip: %s failed its CRC check\n", name);
            return -1;
        }
        if (to_stdout) {
            size_t off = 0;
            while (off < size) {
                int chunk = (int)(size - off);
                if (chunk > 1024) chunk = 1024;
                if (sys_write(data + off, chunk) != chunk) {
                    puts("unzip: stdout write failed");
                    return -1;
                }
                off += (size_t)chunk;
            }
        } else {
            make_parents(name);
            if (write_member(name, data, size)) return -1;
        }
        p += ZIP_CENTRAL_HDR + nlen + xlen + clen;
    }

    if (handled == 0) {
        puts("unzip: no matching members");
        return -1;
    }
    /* -p is a raw-data stream: any status text would corrupt it. */
    if (!to_stdout)
        printf("unzip: %d member(s) %s\n", handled,
               list_only ? "listed" : "processed");
    return 0;
}

int main(int argc, char **argv) {
    int list_only = 0, to_stdout = 0, i = 1;
    while (i < argc && argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
        if (strcmp(argv[i], "-l") == 0) list_only = 1;
        else if (strcmp(argv[i], "-p") == 0) to_stdout = 1;
        else { puts("usage: unzip [-l|-p] ARCHIVE [MEMBER...]"); return 2; }
        i++;
    }
    if (i >= argc) { puts("usage: unzip [-l|-p] ARCHIVE [MEMBER...]"); return 2; }
    if (read_archive(argv[i])) return 1;
    return process(list_only, to_stdout, argc, argv, i + 1);
}
