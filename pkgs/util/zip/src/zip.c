/* zip — create a ZIP archive on CodeOS (stored entries, no compression).
 *
 * Port notes:
 *   - Info-ZIP 3.0 is a large POSIX/C make-based tree; this is a compact
 *     freestanding implementation of the same on-disk format. The downloaded
 *     upstream tarball is kept in ports/zip/upstream/ for reference.
 *   - Entries use method 0 (stored). The result is a valid ZIP readable by
 *     unzip, Python's zipfile, 7zip, etc.; it simply does not compress.
 *   - The kernel caps files at 64 KiB, so the archive and every member are
 *     bounded by ZIP_MAX_ARCHIVE / ZIP_MAX_FILE.
 *
 * Usage: zip ARCHIVE FILE... | zip -l ARCHIVE
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include <stdint.h>

#define ZIP_MAX_ARCHIVE  65536         /* kernel FS_CONTENT_MAX */
#define ZIP_MAX_FILE     64512
#define ZIP_MAX_ENTRIES  64
#define ZIP_MAX_NAME     200
#define ZIP_LOCAL_HDR    30
#define ZIP_CENTRAL_HDR  46
#define ZIP_EOCD         22

#define SIG_LOCAL  0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_EOCD   0x06054b50u

typedef struct {
    char name[ZIP_MAX_NAME];
    uint32_t crc;
    uint32_t size;
    uint32_t local_off;
} entry_t;

static uint8_t arc[ZIP_MAX_ARCHIVE];
static size_t arc_len;
static entry_t entries[ZIP_MAX_ENTRIES];
static int nentries;

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static int append(const uint8_t *src, size_t len) {
    if (arc_len + len > ZIP_MAX_ARCHIVE) {
        puts("zip: archive exceeds the 64 KiB file limit");
        return -1;
    }
    memcpy(arc + arc_len, src, len);
    arc_len += len;
    return 0;
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

static int read_member(const char *path, uint8_t *buf, size_t bufcap, size_t *len) {
    int fd = sys_open(path, 0);
    if (fd < 0) return -1;
    size_t got = 0;
    for (;;) {
        if (got >= bufcap) { sys_close(fd); return -2; }
        int n = sys_read(fd, buf + got, (int)(bufcap - got));
        if (n <= 0) break;
        got += (size_t)n;
        if (got > ZIP_MAX_FILE) { sys_close(fd); return -2; }
    }
    sys_close(fd);
    *len = got;
    return 0;
}

/* Keep relative directory components (Info-ZIP does too), but drop a leading
 * '/' so the archive never contains an absolute path that an extractor should
 * refuse. An empty name after that is an error. */
static const char *member_name(const char *path) {
    while (*path == '/') path++;
    return path;
}

static int add_file(const char *path) {
    static uint8_t data[ZIP_MAX_FILE + 1];
    if (nentries >= ZIP_MAX_ENTRIES) { puts("zip: too many entries"); return -1; }
    const char *name = member_name(path);
    size_t nlen = strlen(name);
    if (!nlen || nlen >= ZIP_MAX_NAME) { puts("zip: bad member name"); return -1; }

    size_t len = 0;
    int rc = read_member(path, data, sizeof(data), &len);
    if (rc) {
        printf("zip: cannot read %s (%s)\n", path,
               rc == -2 ? "larger than 64 KiB" : "open failed");
        return -1;
    }
    uint32_t crc = crc32(data, len);
    uint32_t off = (uint32_t)arc_len;

    uint8_t h[ZIP_LOCAL_HDR];
    memset(h, 0, sizeof(h));
    put32(h, SIG_LOCAL);
    put16(h + 4, 20);                   /* version needed */
    put32(h + 14, crc);
    put32(h + 18, (uint32_t)len);
    put32(h + 22, (uint32_t)len);
    put16(h + 26, (unsigned)nlen);
    if (append(h, sizeof(h)) || append((const uint8_t *)name, nlen) ||
        append(data, len)) return -1;

    entry_t *e = &entries[nentries++];
    size_t k = 0;
    for (; k < nlen; k++) e->name[k] = name[k];
    e->name[k] = 0;
    e->crc = crc;
    e->size = (uint32_t)len;
    e->local_off = off;
    return 0;
}

static int finish(const char *path) {
    size_t cd_start = arc_len;
    for (int i = 0; i < nentries; i++) {
        const entry_t *e = &entries[i];
        size_t nlen = strlen(e->name);
        uint8_t h[ZIP_CENTRAL_HDR];
        memset(h, 0, sizeof(h));
        put32(h, SIG_CENTRAL);
        put16(h + 4, 20);
        put16(h + 6, 20);
        put32(h + 16, e->crc);
        put32(h + 20, e->size);
        put32(h + 24, e->size);
        put16(h + 28, (unsigned)nlen);
        put32(h + 42, e->local_off);
        if (append(h, sizeof(h)) || append((const uint8_t *)e->name, nlen)) return -1;
    }
    size_t cd_size = arc_len - cd_start;

    uint8_t eocd[ZIP_EOCD];
    memset(eocd, 0, sizeof(eocd));
    put32(eocd, SIG_EOCD);
    put16(eocd + 8, (unsigned)nentries);
    put16(eocd + 10, (unsigned)nentries);
    put32(eocd + 12, (uint32_t)cd_size);
    put32(eocd + 16, (uint32_t)cd_start);
    if (append(eocd, sizeof(eocd))) return -1;

    int fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { puts("zip: cannot create archive"); return -1; }
    size_t off = 0;
    while (off < arc_len) {
        int chunk = (int)(arc_len - off);
        if (chunk > 1024) chunk = 1024;
        int n = sys_pwrite(fd, arc + off, chunk);
        if (n <= 0) { sys_close(fd); puts("zip: write failed"); return -1; }
        off += (size_t)n;
    }
    sys_close(fd);
    printf("zip: wrote %s (%u bytes, %d entries)\n", path,
           (unsigned)arc_len, nentries);
    return 0;
}

static int list(const char *path) {
    int fd = sys_open(path, 0);
    if (fd < 0) { puts("zip: cannot open archive"); return -1; }
    arc_len = 0;
    for (;;) {
        if (arc_len >= sizeof(arc)) { sys_close(fd); puts("zip: archive too large"); return -1; }
        int n = sys_read(fd, arc + arc_len, (int)(sizeof(arc) - arc_len));
        if (n <= 0) break;
        arc_len += (size_t)n;
    }
    sys_close(fd);

    long eocd = -1;
    for (long i = (long)arc_len - ZIP_EOCD; i >= 0; i--) {
        if (arc[i] == 0x50 && arc[i + 1] == 0x4B &&
            arc[i + 2] == 0x05 && arc[i + 3] == 0x06) { eocd = i; break; }
    }
    if (eocd < 0) { puts("zip: not a ZIP archive"); return -1; }
    unsigned count = (unsigned)arc[eocd + 10] | ((unsigned)arc[eocd + 11] << 8);
    long p = (long)((unsigned)arc[eocd + 16] | ((unsigned)arc[eocd + 17] << 8) |
                    ((unsigned)arc[eocd + 18] << 16) | ((unsigned)arc[eocd + 19] << 24));
    for (unsigned i = 0; i < count; i++) {
        if (p + ZIP_CENTRAL_HDR > (long)arc_len ||
            arc[p] != 0x50 || arc[p + 1] != 0x4B ||
            arc[p + 2] != 0x01 || arc[p + 3] != 0x02) {
            puts("zip: corrupt central directory");
            return -1;
        }
        unsigned nlen = (unsigned)arc[p + 28] | ((unsigned)arc[p + 29] << 8);
        unsigned xlen = (unsigned)arc[p + 30] | ((unsigned)arc[p + 31] << 8);
        unsigned clen = (unsigned)arc[p + 32] | ((unsigned)arc[p + 33] << 8);
        unsigned size = (unsigned)arc[p + 24] | ((unsigned)arc[p + 25] << 8) |
                        ((unsigned)arc[p + 26] << 16) | ((unsigned)arc[p + 27] << 24);
        if (p + ZIP_CENTRAL_HDR + nlen + xlen + clen > (long)arc_len) {
            puts("zip: truncated central directory");
            return -1;
        }
        char name[ZIP_MAX_NAME + 1];
        size_t k = 0;
        for (; k < nlen && k < ZIP_MAX_NAME; k++)
            name[k] = (char)arc[p + ZIP_CENTRAL_HDR + k];
        name[k] = 0;
        printf("%s  %u bytes\n", name, size);
        p += ZIP_CENTRAL_HDR + nlen + xlen + clen;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "-l") == 0)
        return list(argv[2]);
    if (argc < 3) {
        puts("usage: zip ARCHIVE FILE... | zip -l ARCHIVE");
        return 2;
    }
    for (int i = 2; i < argc; i++) if (add_file(argv[i])) return 1;
    return finish(argv[1]);
}
