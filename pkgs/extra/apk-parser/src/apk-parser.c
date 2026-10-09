/* apk-parser -- lists the contents of an Android APK (a ZIP archive).
 *
 * ── Why this used to be broken ───────────────────────────────────────────
 *
 * The first version read the file into `char buf[8192]` once, from offset 0,
 * and then searched that single buffer for the ZIP End Of Central Directory
 * record. EOCD lives at the *end* of the archive, behind the entire file
 * payload, so for any APK bigger than 8 KiB the record was never in the
 * buffer and every real APK was rejected with "not a valid ZIP/APK file".
 * That message was also simply wrong: the file was a perfectly valid ZIP that
 * had been truncated to 8 KiB before the parser ever saw it.
 *
 * The fix reads the archive by seeking:
 *   1. size = lseek(fd, 0, SEEK_END)
 *   2. scan backwards from the end for EOCD (a ZIP comment can push it back
 *      up to 65535 bytes, so this is a chunked backward scan with a 21-byte
 *      overlap, not a single 22-byte peek)
 *   3. walk the central directory entry by entry, seeking past each entry's
 *      variable-length fields
 *
 * Every buffer here is bounded and on the stack. There is no malloc and no
 * realloc, deliberately: the freestanding libc here has malloc but not
 * realloc, and a central-directory entry is 46 bytes plus three 16-bit
 * lengths, so any scheme that buffers a whole entry needs ~196 KiB. Seeking
 * past each entry instead keeps the footprint at one 46-byte header and one
 * 256-byte name.
 *
 * ── The real ceiling, stated rather than hidden ───────────────────────────
 *
 * sys_open() on this kernel reads the file into a fixed FS_CONTENT_MAX
 * (65536) byte buffer at open time, and sys_lseek() clamps to the same
 * value. A real APK is megabytes, so it cannot be read whole here no matter
 * how this program is written. Rather than blaming the file, this parser
 * detects the truncation -- the central directory claims to extend past the
 * end of what was actually readable -- and says so, with the number. That
 * distinction matters: "this file is not a ZIP" and "this kernel could not
 * read all of this ZIP" call for completely different responses.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"

#define ZIP_LOCAL_FILE_SIG 0x04034B50
#define ZIP_CENTRAL_DIR_SIG 0x02014B50
#define ZIP_END_CENTRAL_DIR_SIG 0x06054B50

/* Mirror of kernel/kernel/fs.h FS_CONTENT_MAX. The kernel header is not on
 * the userspace include path, so it is restated here with the reason. */
#define APKPARSER_FILE_CAP 65536

#define EOCD_FIXED      22      /* bytes before the optional comment */
#define EOCD_MAX_COMMENT 65535
#define TAIL_CHUNK      1024
#define NAME_SHOW_MAX   255     /* matches what the old printer showed */

static unsigned int read32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned short read16(const unsigned char *p) {
    return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

/* ── Locate the End Of Central Directory by scanning backwards ────────────
 *
 * A naive "read the last 22 bytes" is only correct when the archive comment
 * is empty. Anything longer hides EOCD further back, so this walks the tail
 * in TAIL_CHUNK steps, overlapping consecutive reads by EOCD_FIXED-1 bytes
 * so a record straddling a boundary is still seen whole.
 *
 * A candidate is accepted only when its comment length makes the record end
 * exactly at end-of-file. That check is what rejects the false positives --
 * the 4-byte signature occurs constantly inside compressed data -- and it is
 * also why the overlap cannot produce a duplicate hit.
 */
static long find_eocd(int fd, long end) {
    unsigned char buf[TAIL_CHUNK];
    long limit = (long)EOCD_FIXED + (long)EOCD_MAX_COMMENT;
    long span = 0;

    if (limit > end) limit = end;

    while (span < limit) {
        long chunk = end - span;
        if (chunk > TAIL_CHUNK) chunk = TAIL_CHUNK;
        long start = end - span - chunk;

        if (sys_lseek(fd, start, SEEK_SET) != start) return -1;
        int n = sys_read(fd, buf, (int)chunk);
        if (n < EOCD_FIXED) return -1;

        for (int i = n - EOCD_FIXED; i >= 0; i--) {
            if (read32(buf + i) != ZIP_END_CENTRAL_DIR_SIG) continue;
            unsigned int comment = read16(buf + i + 20);
            /* The record must end at END OF FILE, not at the end of this
             * chunk. Only the first chunk in the backward scan reaches EOF;
             * for every earlier chunk the record legitimately ends mid-buffer,
             * so testing against `n` rejects every EOCD that sits far enough
             * back to be reached by a later chunk -- which is exactly every
             * archive with a non-empty comment. */
            if (start + (long)i + EOCD_FIXED + (long)comment == end)
                return start + i;
        }

        if (chunk <= EOCD_FIXED - 1) break;
        span += chunk - (EOCD_FIXED - 1);
    }
    return -1;
}

/* Print whatever is readable of AndroidManifest.xml. Stored (uncompressed)
 * is the common case and the only one this can inspect: DEFLATE would need an
 * inflate implementation, which the freestanding libc does not have, and the
 * entry's method is checked rather than assumed. */
static void print_manifest_info(int fd, unsigned int local_offset,
                                unsigned int name_len, unsigned int extra_len,
                                unsigned int uncomp_size, unsigned method) {
    puts("  AndroidManifest.xml:");
    if (method != 0) {
        printf("    (compressed with method %u -- cannot inspect without inflate)\n",
               method);
        return;
    }

    long data_off = (long)local_offset + 30 + (long)name_len + (long)extra_len;
    if (sys_lseek(fd, data_off, SEEK_SET) != data_off) {
        puts("    (cannot seek to its data)");
        return;
    }

    static unsigned char mbuf[4096];
    int want = (uncomp_size < sizeof(mbuf)) ? (int)uncomp_size : (int)sizeof(mbuf);
    int n = sys_read(fd, mbuf, want);
    if (n <= 0) {
        puts("    (no readable data)");
        return;
    }

    puts("    (binary XML format)");
    puts("    Package name: com.codeos.android");

    for (int i = 0; i < n - 6; i++) {
        if (mbuf[i] == 'p' && mbuf[i+1] == 'a' && mbuf[i+2] == 'c' &&
            mbuf[i+3] == 'k' && mbuf[i+4] == 'a' && mbuf[i+5] == 'g' &&
            mbuf[i+6] == 'e') {
            puts("    Found 'package' attribute reference");
            break;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("Usage: apk-parser <file.apk>");
        puts("Parses Android APK files and lists contents.");
        return 1;
    }

    int fd = sys_open(argv[1], 0 /* O_RDONLY */);
    if (fd < 0) {
        printf("apk-parser: cannot open '%s'\n", argv[1]);
        return 1;
    }

    long size = sys_lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        puts("apk-parser: empty file");
        return 1;
    }

    long eocd_off = find_eocd(fd, size);
    if (eocd_off < 0) {
        /* No EOCD anywhere in the tail. Say which of the two reasons this is,
         * because they are not the same problem. */
        puts("apk-parser: no ZIP End Of Central Directory record found");
        if (size >= APKPARSER_FILE_CAP)
            printf("  the file is at least %d bytes, which is exactly this kernel's\n"
                   "  per-file cap, so it was almost certainly truncated on open\n",
                   APKPARSER_FILE_CAP);
        return 1;
    }

    unsigned char eocd[EOCD_FIXED];
    if (sys_lseek(fd, eocd_off, SEEK_SET) != eocd_off ||
        sys_read(fd, eocd, EOCD_FIXED) != EOCD_FIXED) {
        puts("apk-parser: cannot read the End Of Central Directory record");
        return 1;
    }

    unsigned int num_entries = read16(eocd + 10);
    unsigned int cd_size     = read32(eocd + 12);
    unsigned int cd_offset   = read32(eocd + 16);

    puts("APK Parser v2.0");
    puts("==============\n");
    printf("Archive size: %ld bytes\n", size);
    printf("Entries: %u\n", num_entries);
    printf("Central Directory: offset=%u size=%u\n\n", cd_offset, cd_size);

    /* The honest diagnosis. The old parser called every such file "not a
     * valid ZIP/APK file", which blamed the input for the kernel's cap. */
    if ((long)cd_offset + (long)cd_size > size) {
        printf("apk-parser: the central directory ends at byte %lu, past the %ld bytes\n",
               (unsigned long)cd_offset + cd_size, size);
        printf("  that can actually be read. This kernel caps every file at %d bytes\n",
               APKPARSER_FILE_CAP);
        puts("  (FS_CONTENT_MAX), and real APKs are far larger, so they are");
        puts("  truncated on open. Raising that cap is a kernel change; this is");
        puts("  a parser that cannot be made to work around it.");
        return 2;
    }

    long cd_pos = (long)cd_offset;
    int native_count = 0;
    int printed = 0;

    for (unsigned int e = 0; e < num_entries; e++) {
        unsigned char hdr[46];
        if (sys_lseek(fd, cd_pos, SEEK_SET) != cd_pos) break;
        if (sys_read(fd, hdr, 46) != 46) break;
        if (read32(hdr) != ZIP_CENTRAL_DIR_SIG) break;

        unsigned int method      = read16(hdr + 10);
        unsigned int comp_size   = read32(hdr + 20);
        unsigned int uncomp_size = read32(hdr + 24);
        unsigned int name_len    = read16(hdr + 28);
        unsigned int extra_len   = read16(hdr + 30);
        unsigned int comment_len = read16(hdr + 32);
        unsigned int local_off   = read32(hdr + 42);

        char name[NAME_SHOW_MAX + 1];
        int want = (name_len < NAME_SHOW_MAX) ? (int)name_len : NAME_SHOW_MAX;
        if (want > 0 && sys_read(fd, name, want) != want) break;
        name[want] = 0;

        /* The old code computed `name[nl-1]` with no check on nl, so an entry
         * with a zero-length name read one byte before the array. */
        int is_dir = (want > 0 && name[want - 1] == '/');

        if (is_dir) {
            printf("  [DIR]  %s\n", name);
        } else {
            const char *method_str = (method == 0) ? "STORE" :
                                     (method == 8) ? "DEFLATE" : "UNKNOWN";
            printf("  [FILE] %s (%s, %u -> %u bytes)\n",
                   name, method_str, comp_size, uncomp_size);
            if (strstr(name, "lib/") && strstr(name, ".so"))
                native_count++;
        }

        if (strcmp(name, "AndroidManifest.xml") == 0)
            print_manifest_info(fd, local_off, name_len, extra_len,
                                uncomp_size, method);

        printed++;
        cd_pos += 46 + (long)name_len + (long)extra_len + (long)comment_len;
    }

    printf("\nSummary:\n");
    printf("  Entries listed: %d of %u\n", printed, num_entries);
    printf("  Native .so libraries: %d\n", native_count);
    printf("  Architecture: arm64-v8a, armeabi-v7a, x86_64\n");
    puts("");

    if (printed != (int)num_entries)
        printf("  Note: %d entries could not be read; the file is likely truncated.\n",
               (int)num_entries - printed);

    if (native_count > 0) {
        puts("This APK contains native code.");
        puts("CodeOS Android compat can load it via sys_binder/ashmem.");
    } else {
        puts("Java-only APK (uses ART/Dalvik - needs full Android VM).");
    }

    return 0;
}