/* tar — create, list and extract ustar archives on CodeOS.
 *
 * Port notes:
 *   - This is a compact freestanding implementation, not a copy of GNU tar.
 *     GNU tar's 1.24 tree is ~200 source files and is built on gnulib/POSIX;
 *     none of that is available in the CodeOS freestanding toolchain. The
 *     downloaded upstream archive is kept in ports/tar/upstream/ as reference.
 *   - The on-disk format is POSIX ustar: 512-byte headers, octal numeric
 *     fields, data padded to 512 bytes, two zero blocks at the end. Archives
 *     written here are readable by GNU/BSD tar and Python's tarfile.
 *   - Directories are discovered with sys_readdir (NUL-packed names, byte
 *     count), NOT sys_stat: measurements in core/shell show sys_stat always
 *     fails on this kernel. open() on a directory fails, so the same call
 *     distinguishes files from directories.
 *   - The kernel caps a file at 64 KiB, so archive and member sizes are bounded
 *     by TAR_MAX_ARCHIVE/TAR_MAX_FILE. Compression is deliberately separate:
 *     `gzip archive.tar` produces the usual .tar.gz.
 *
 * Usage: tar -cf ARCHIVE FILE... | tar -tf ARCHIVE | tar -xf ARCHIVE
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include <stdint.h>

#define TAR_BLOCK       512
#define TAR_MAX_ARCHIVE 65536         /* kernel FS_CONTENT_MAX */
#define TAR_MAX_FILE    64512         /* leaves room for header + trailer */
#define TAR_MAX_ENTRIES 128
#define TAR_MAX_DEPTH   4
#define TAR_MAX_PATH    240
#define TAR_READDIR_BUF 4096

#define H_NAME    0
#define H_MODE    100
#define H_UID     108
#define H_GID     116
#define H_SIZE    124
#define H_MTIME   136
#define H_CHKSUM  148
#define H_TYPE    156
#define H_MAGIC   257
#define H_VERSION 263
#define H_PREFIX  345

static uint8_t archive[TAR_MAX_ARCHIVE];
static size_t archive_len;
static int entries_written;

static int append_bytes(const uint8_t *src, size_t len) {
    if (archive_len + len > TAR_MAX_ARCHIVE) {
        puts("tar: archive exceeds the 64 KiB file limit");
        return -1;
    }
    memcpy(archive + archive_len, src, len);
    archive_len += len;
    return 0;
}

/* POSIX octal field: leading zeros, right aligned, NUL terminated. */
static void put_octal(char *field, size_t width, unsigned value) {
    char tmp[16];
    int p = 0;
    if (value == 0) tmp[p++] = '0';
    while (value) { tmp[p++] = (char)('0' + (value & 7)); value >>= 3; }
    size_t n = (size_t)p;
    if (n > width - 1) n = width - 1;
    for (size_t i = 0; i < width - 1 - n; i++) field[i] = '0';
    for (size_t i = 0; i < n; i++) field[width - 1 - n + i] = tmp[p - 1 - i];
    field[width - 1] = 0;
}

static unsigned get_octal(const char *field, size_t width) {
    unsigned v = 0;
    size_t i = 0;
    while (i < width && (field[i] == ' ' || field[i] == 0)) i++;
    for (; i < width && field[i] >= '0' && field[i] <= '7'; i++)
        v = (v << 3) | (unsigned)(field[i] - '0');
    return v;
}

static unsigned header_checksum(const uint8_t *h) {
    unsigned sum = 0;
    for (size_t i = 0; i < TAR_BLOCK; i++)
        sum += (i >= H_CHKSUM && i < H_CHKSUM + 8) ? (unsigned)' ' : (unsigned)h[i];
    return sum;
}

static int put_header(const char *path, unsigned size, char type) {
    uint8_t h[TAR_BLOCK];
    memset(h, 0, sizeof(h));
    size_t len = strlen(path);

    if (len < 100) {
        memcpy(h + H_NAME, path, len);
    } else {
        /* ustar split: find the last slash that leaves both fields legal. */
        size_t cut = len;
        while (cut > 0 && path[cut - 1] != '/') cut--;
        while (cut > 0 && (len - cut > 100 || cut - 1 > 155)) {
            size_t prev = cut - 1;
            while (prev > 0 && path[prev - 1] != '/') prev--;
            if (prev == cut) break;
            cut = prev;
        }
        if (cut == 0 || cut - 1 > 155 || len - cut > 100) {
            puts("tar: path too long for ustar header");
            return -1;
        }
        memcpy(h + H_PREFIX, path, cut - 1);
        memcpy(h + H_NAME, path + cut, len - cut);
    }

    put_octal((char *)h + H_MODE, 8, 0644);
    put_octal((char *)h + H_UID, 8, 0);
    put_octal((char *)h + H_GID, 8, 0);
    put_octal((char *)h + H_SIZE, 12, size);
    put_octal((char *)h + H_MTIME, 12, 0);
    h[H_TYPE] = (uint8_t)type;
    memcpy(h + H_MAGIC, "ustar\0", 6);
    memcpy(h + H_VERSION, "00", 2);

    unsigned sum = header_checksum(h);
    char field[9];
    put_octal(field, 8, sum);
    memcpy(h + H_CHKSUM, field, 7);
    h[H_CHKSUM + 7] = ' ';

    if (append_bytes(h, TAR_BLOCK)) return -1;
    entries_written++;
    return 0;
}

static int put_padding(size_t size) {
    size_t pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
    if (!pad) return 0;
    static const uint8_t zeros[TAR_BLOCK];
    return append_bytes(zeros, pad);
}

static int read_member(const char *path, uint8_t *buf, size_t cap, size_t *len) {
    int fd = sys_open(path, 0);
    if (fd < 0) return -1;
    size_t got = 0;
    for (;;) {
        if (got >= cap) { sys_close(fd); return -2; }
        int n = sys_read(fd, buf + got, (int)(cap - got));
        if (n <= 0) break;
        got += (size_t)n;
        if (got > TAR_MAX_FILE) { sys_close(fd); return -2; }
    }
    sys_close(fd);
    *len = got;
    return 0;
}

static int add_path(const char *path, int depth);

static int add_directory(const char *path, int depth) {
    char names[TAR_READDIR_BUF];
    int n = sys_readdir(path, names, (int)sizeof(names) - 1);
    if (n <= 0) return 0;

    if (put_header(path, 0, '5')) return -1;

    /* Copy before recursing: the readdir buffer is a single shared array. */
    char local[TAR_READDIR_BUF];
    memcpy(local, names, (size_t)n);
    int pos = 0;
    while (pos < n) {
        const char *name = local + pos;
        size_t nl = strlen(name);
        pos += (int)nl + 1;
        if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) continue;

        char child[TAR_MAX_PATH + 1];
        size_t pl = strlen(path);
        int slash = (pl > 0 && path[pl - 1] == '/');
        if (pl + (slash ? 0 : 1) + nl > TAR_MAX_PATH) {
            puts("tar: path too long, skipping entry");
            continue;
        }
        size_t k = 0;
        for (size_t i = 0; i < pl; i++) child[k++] = path[i];
        if (!slash) child[k++] = '/';
        for (size_t i = 0; i < nl; i++) child[k++] = name[i];
        child[k] = 0;

        if (depth < TAR_MAX_DEPTH) {
            if (add_path(child, depth + 1)) return -1;
        } else {
            printf("tar: depth limit reached at %s\n", child);
        }
    }
    return 0;
}

static int add_path(const char *path, int depth) {
    static char probe[64];
    static uint8_t data[TAR_MAX_FILE + 1];

    if (archive_len / TAR_BLOCK >= TAR_MAX_ENTRIES) {
        puts("tar: too many entries");
        return -1;
    }
    /* sys_stat is broken on this kernel; readdir-on-a-file failing is the
     * reliable way to tell a regular file from a directory. */
    if (sys_readdir(path, probe, (int)sizeof(probe) - 1) >= 0)
        return add_directory(path, depth);

    size_t len = 0;
    int rc = read_member(path, data, sizeof(data), &len);
    if (rc) {
        printf("tar: cannot read %s (%s)\n", path,
               rc == -2 ? "larger than 64 KiB" : "open failed");
        return -1;
    }
    if (put_header(path, (unsigned)len, '0')) return -1;
    if (append_bytes(data, len)) return -1;
    return put_padding(len);
}

static int write_archive(const char *path) {
    static const uint8_t zeros[2 * TAR_BLOCK];
    if (append_bytes(zeros, sizeof(zeros))) return -1;

    int fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { puts("tar: cannot create archive"); return -1; }
    size_t off = 0;
    while (off < archive_len) {
        int chunk = (int)(archive_len - off);
        if (chunk > 1024) chunk = 1024;
        int n = sys_pwrite(fd, archive + off, chunk);
        if (n <= 0) { sys_close(fd); puts("tar: write failed"); return -1; }
        off += (size_t)n;
    }
    sys_close(fd);
    printf("tar: wrote %s (%u bytes, %d entries)\n", path,
           (unsigned)archive_len, entries_written);
    return 0;
}

static int read_archive(const char *path) {
    int fd = sys_open(path, 0);
    if (fd < 0) { puts("tar: cannot open archive"); return -1; }
    archive_len = 0;
    for (;;) {
        if (archive_len >= sizeof(archive)) { sys_close(fd); puts("tar: archive too large"); return -1; }
        int n = sys_read(fd, archive + archive_len,
                         (int)(sizeof(archive) - archive_len));
        if (n <= 0) break;
        archive_len += (size_t)n;
    }
    sys_close(fd);
    if (archive_len < TAR_BLOCK) { puts("tar: archive is empty or truncated"); return -1; }
    return 0;
}

static int safe_path(const uint8_t *h, char *out, size_t cap) {
    const char *prefix = (const char *)h + H_PREFIX;
    const char *name = (const char *)h + H_NAME;
    size_t plen = 0;
    while (plen < 155 && prefix[plen]) plen++;
    size_t nlen = 0;
    while (nlen < 100 && name[nlen]) nlen++;

    size_t k = 0;
    if (plen) {
        if (plen + 1 + nlen >= cap) return -1;
        for (size_t i = 0; i < plen; i++) out[k++] = prefix[i];
        out[k++] = '/';
    } else {
        if (nlen >= cap) return -1;
    }
    for (size_t i = 0; i < nlen; i++) out[k++] = name[i];
    out[k] = 0;

    if (out[0] == '/' || !out[0]) return -1;
    /* Reject traversal components before touching the filesystem. */
    size_t start = 0;
    while (start <= k) {
        size_t end = start;
        while (end < k && out[end] != '/') end++;
        if (end - start == 2 && out[start] == '.' && out[start + 1] == '.') return -1;
        if (end == k) break;
        start = end + 1;
    }
    return 0;
}

static int make_parents(const char *path) {
    char dir[TAR_MAX_PATH + 1];
    for (size_t i = 1; path[i]; i++) {
        if (path[i] != '/') continue;
        size_t n = i < TAR_MAX_PATH ? i : TAR_MAX_PATH;
        memcpy(dir, path, n);
        dir[n] = 0;
        /* Already-existing parents simply fail, which is harmless here. */
        sys_mkdir(dir);
    }
    return 0;
}

static int write_member(const char *path, const uint8_t *data, size_t len) {
    int fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("tar: cannot create %s\n", path); return -1; }
    size_t off = 0;
    while (off < len) {
        int chunk = (int)(len - off);
        if (chunk > 1024) chunk = 1024;
        int n = sys_pwrite(fd, data + off, chunk);
        if (n <= 0) { sys_close(fd); printf("tar: write failed for %s\n", path); return -1; }
        off += (size_t)n;
    }
    sys_close(fd);
    return 0;
}

static int process_archive(int extract) {
    size_t off = 0;
    while (off + TAR_BLOCK <= archive_len) {
        const uint8_t *h = archive + off;
        int zero = 1;
        for (size_t i = 0; i < TAR_BLOCK; i++) if (h[i]) { zero = 0; break; }
        if (zero) return 0;

        if (memcmp(h + H_MAGIC, "ustar", 5) != 0 ||
            get_octal((const char *)h + H_CHKSUM, 8) != header_checksum(h)) {
            printf("tar: corrupt header at block %u\n", (unsigned)(off / TAR_BLOCK));
            return -1;
        }
        char path[TAR_MAX_PATH + 1];
        if (safe_path(h, path, sizeof(path))) {
            printf("tar: unsafe path at block %u\n", (unsigned)(off / TAR_BLOCK));
            return -1;
        }
        unsigned size = get_octal((const char *)h + H_SIZE, 12);
        char type = (char)h[H_TYPE];
        size_t data_off = off + TAR_BLOCK;
        size_t blocks = (size + TAR_BLOCK - 1) / TAR_BLOCK;
        if (size > TAR_MAX_ARCHIVE || blocks > (archive_len - data_off) / TAR_BLOCK) {
            printf("tar: truncated member %s\n", path);
            return -1;
        }

        int display_dir = (type == '5' || (size == 0 && path[0] && path[strlen(path) - 1] == '/'));
        if (!extract) {
            printf("%s%s\n", path, display_dir ? "/" : "");
        } else if (type == '5') {
            make_parents(path);
            sys_mkdir(path);
        } else if (type == '0' || type == 0) {
            make_parents(path);
            if (write_member(path, archive + data_off, size)) return -1;
        } else {
            printf("tar: skipping unsupported member type '%c': %s\n", type, path);
        }
        off = data_off + blocks * TAR_BLOCK;
    }
    puts("tar: missing end-of-archive blocks");
    return -1;
}

int main(int argc, char **argv) {
    int mode = 0;                       /* 'c', 't' or 'x' */
    const char *archive_path = 0;
    int first_operand = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-') {
            for (int j = 1; a[j]; j++) {
                if (a[j] == 'c' || a[j] == 't' || a[j] == 'x') {
                    if (mode && mode != a[j]) { puts("tar: conflicting modes"); return 2; }
                    mode = a[j];
                } else if (a[j] == 'f') {
                    if (i + 1 >= argc) { puts("tar: -f needs an archive path"); return 2; }
                    archive_path = argv[++i];
                } else if (a[j] == 'v') {
                    /* Listing is already verbose; accept -v for familiarity. */
                } else {
                    printf("tar: unknown option -%c\n", a[j]);
                    return 2;
                }
            }
        } else if (!first_operand) {
            first_operand = i;
            break;
        }
    }
    if (!mode || !archive_path) {
        puts("usage: tar -cf ARCHIVE FILE... | -tf ARCHIVE | -xf ARCHIVE");
        return 2;
    }
    if (mode == 'c') {
        if (!first_operand) { puts("tar: nothing to archive"); return 2; }
        for (int i = first_operand; i < argc; i++)
            if (add_path(argv[i], 0)) return 1;
        return write_archive(archive_path);
    }
    if (read_archive(archive_path)) return 1;
    return process_archive(mode == 'x');
}
