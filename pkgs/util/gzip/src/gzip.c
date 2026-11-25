/* gzip — compress and decompress gzip (RFC 1952) files on CodeOS.
 *
 * Port notes:
 *   - The DEFLATE decoder is the tree's tested puff-style decoder, vendored
 *     in inflate.c so this package has no cross-package dependency.
 *   - Compression emits *stored* DEFLATE blocks. That is a valid DEFLATE
 *     stream: gzip -d and zlib both accept it. It trades ratio for a tiny,
 *     obviously-correct encoder; a real Huffman compressor can be added later
 *     without changing the file format.
 *   - The kernel caps a file at 64 KiB (FS_CONTENT_MAX), so both the reader
 *     and the writer work within GZIP_MAX_FILE. Larger inputs are reported as
 *     an error rather than silently truncated.
 *
 * Usage: gzip [-c|-d|-l] FILE...   (a `gunzip` argv[0] implies -d)
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "gzip_port.h"

#define GZIP_HEADER_LEN 10
#define GZIP_TRAILER_LEN 8
#define DEFLATE_MAX_STORED 65535

static int ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return m <= n && strcmp(s + n - m, suffix) == 0;
}

static unsigned read_le32(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8)
         | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void write_le32(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint32_t gzip_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

/* Read up to `limit` bytes. The caller supplies `bufcap` >= limit + 1 so one
 * extra read can distinguish "exactly at the cap" from "longer than the cap":
 * the kernel silently truncates on open, so we cannot ask it for the real size
 * and must detect the overflow ourselves. */
static int read_file(const char *path, uint8_t *buf, size_t bufcap,
                     size_t limit, size_t *len) {
    int fd = sys_open(path, 0);
    if (fd < 0) return -1;
    size_t got = 0;
    for (;;) {
        if (got >= bufcap) { sys_close(fd); return -2; }
        int n = sys_read(fd, buf + got, (int)(bufcap - got));
        if (n <= 0) break;
        got += (size_t)n;
        if (got > limit) { sys_close(fd); return -2; }
    }
    sys_close(fd);
    *len = got;
    return 0;
}

static int write_file(const char *path, const uint8_t *buf, size_t len) {
    int fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        int chunk = (int)(len - off);
        if (chunk > 1024) chunk = 1024;
        int n = sys_pwrite(fd, buf + off, chunk);
        if (n <= 0) { sys_close(fd); return -2; }
        off += (size_t)n;
    }
    sys_close(fd);
    return 0;
}

static size_t emit_stored_block(uint8_t *out, const uint8_t *src,
                                size_t len, int final) {
    size_t pos = 0;
    out[pos++] = (uint8_t)(final ? 1 : 0);
    out[pos++] = (uint8_t)(len & 0xFF);
    out[pos++] = (uint8_t)((len >> 8) & 0xFF);
    unsigned inv = (unsigned)(~len) & 0xFFFFu;
    out[pos++] = (uint8_t)(inv & 0xFF);
    out[pos++] = (uint8_t)((inv >> 8) & 0xFF);
    for (size_t i = 0; i < len; i++) out[pos++] = src[i];
    return pos;
}

static int do_compress(const char *path, int to_stdout) {
    static uint8_t in[GZIP_MAX_FILE + 1];
    static uint8_t out[GZIP_MAX_FILE + 1024];
    size_t inlen = 0;
    int rc = read_file(path, in, sizeof(in), GZIP_MAX_FILE, &inlen);
    if (rc) {
        printf("gzip: cannot read %s (%s)\n", path,
               rc == -2 ? "larger than 64 KiB" : "open failed");
        return 1;
    }

    size_t pos = 0;
    static const uint8_t header[GZIP_HEADER_LEN] = {
        0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 255
    };
    for (size_t i = 0; i < GZIP_HEADER_LEN; i++) out[pos++] = header[i];

    size_t off = 0;
    do {
        size_t chunk = inlen - off;
        if (chunk > DEFLATE_MAX_STORED) chunk = DEFLATE_MAX_STORED;
        int final = (off + chunk >= inlen);
        pos += emit_stored_block(out + pos, in + off, chunk, final);
        off += chunk;
    } while (off < inlen);

    uint32_t crc = gzip_crc32(in, inlen);
    write_le32(out + pos, crc);          pos += 4;
    write_le32(out + pos, (unsigned)inlen); pos += 4;

    if (to_stdout) {
        int n = sys_write((const char *)out, (int)pos);
        if (n != (int)pos) { puts("gzip: write failed"); return 1; }
    } else {
        char target[256];
        size_t len = strlen(path);
        if (len + 4 > sizeof(target)) { puts("gzip: path too long"); return 1; }
        for (size_t i = 0; i < len; i++) target[i] = path[i];
        target[len] = '.'; target[len + 1] = 'g';
        target[len + 2] = 'z'; target[len + 3] = '\0';
        if (write_file(target, out, pos)) { puts("gzip: cannot write output"); return 1; }
    }
    return 0;
}

static int skip_gzip_header(const uint8_t *buf, size_t len, size_t *pos) {
    if (len < GZIP_HEADER_LEN + GZIP_TRAILER_LEN) return -1;
    if (buf[0] != 0x1F || buf[1] != 0x8B || buf[2] != 8) return -2;
    unsigned flags = buf[3];
    size_t p = GZIP_HEADER_LEN;

    if (flags & 4) {                      /* FEXTRA */
        if (p + 2 > len) return -1;
        size_t xlen = (size_t)buf[p] | ((size_t)buf[p + 1] << 8);
        p += 2;
        if (p + xlen > len) return -1;
        p += xlen;
    }
    if (flags & 8) {                      /* FNAME */
        while (p < len && buf[p] != 0) p++;
        if (p >= len) return -1;
        p++;
    }
    if (flags & 16) {                     /* FCOMMENT */
        while (p < len && buf[p] != 0) p++;
        if (p >= len) return -1;
        p++;
    }
    if (flags & 2) {                      /* FHCRC */
        if (p + 2 > len) return -1;
        p += 2;
    }
    *pos = p;
    return 0;
}

static int do_decompress(const char *path, int to_stdout, int list_only) {
    static uint8_t in[GZIP_MAX_FILE + 1];
    static uint8_t out[GZIP_MAX_FILE];
    size_t inlen = 0;
    int rc = read_file(path, in, sizeof(in), GZIP_MAX_FILE, &inlen);
    if (rc) {
        printf("gunzip: cannot read %s (%s)\n", path,
               rc == -2 ? "larger than 64 KiB" : "open failed");
        return 1;
    }

    size_t start = 0;
    rc = skip_gzip_header(in, inlen, &start);
    if (rc) { puts("gunzip: not a gzip file"); return 1; }

    size_t outlen = sizeof(out), consumed = 0;
    rc = gzip_inflate(in + start, inlen - start, out, &outlen, &consumed);
    if (rc) { printf("gunzip: bad deflate stream (%d)\n", rc); return 1; }

    size_t trailer = start + consumed;
    if (trailer + GZIP_TRAILER_LEN > inlen) { puts("gunzip: missing trailer"); return 1; }
    uint32_t want_crc = read_le32(in + trailer);
    unsigned want_size = read_le32(in + trailer + 4);
    if (gzip_crc32(out, outlen) != want_crc || outlen != want_size) {
        puts("gunzip: CRC or size mismatch");
        return 1;
    }

    if (list_only) {
        printf("%s  %u bytes  crc %08x\n", path, (unsigned)outlen, want_crc);
        return 0;
    }
    if (to_stdout) {
        int n = sys_write((const char *)out, (int)outlen);
        if (n != (int)outlen) { puts("gunzip: write failed"); return 1; }
    } else {
        char target[256];
        size_t len = strlen(path);
        if (ends_with(path, ".gz")) len -= 3;
        if (len + 1 > sizeof(target)) { puts("gunzip: path too long"); return 1; }
        for (size_t i = 0; i < len; i++) target[i] = path[i];
        target[len] = '\0';
        if (write_file(target, out, outlen)) { puts("gunzip: cannot write output"); return 1; }
    }
    return 0;
}

int main(int argc, char **argv) {
    int decompress = ends_with(argv[0], "gunzip");
    int to_stdout = 0, list_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decompress") == 0) decompress = 1;
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--stdout") == 0) to_stdout = 1;
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_only = 1;
            decompress = 1;             /* listing reads a gzip payload */
        }
        else if (argv[i][0] == '-') { puts("gzip: unknown option"); return 2; }
        else {
            int rc = decompress ? do_decompress(argv[i], to_stdout, list_only)
                                : do_compress(argv[i], to_stdout);
            if (rc) return rc;
        }
    }
    if (argc < 2) {
        puts("usage: gzip [-c|-d|-l] FILE...   (gunzip FILE = gzip -d FILE)");
        return 2;
    }
    return 0;
}
