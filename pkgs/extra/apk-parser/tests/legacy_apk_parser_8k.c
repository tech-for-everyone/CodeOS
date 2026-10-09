/* Negative control for the apk-parser test.
 *
 * This is NOT shipped and is NOT part of any build. It reproduces the
 * algorithm the real apk-parser used before it was fixed: read the first
 * 8192 bytes once, then look for the ZIP End Of Central Directory record
 * inside that one buffer. EOCD sits at the end of the archive, so this
 * fails on every archive larger than 8 KiB -- and, crucially, reports it as
 * "not a valid ZIP/APK file", blaming the input for what is really its own
 * truncation.
 *
 * It is kept as a fixture rather than pulled from git history so the test
 * stays meaningful after the fix is committed: once HEAD contains the fixed
 * parser, `git show HEAD:...` would return the fixed version and the control
 * would quietly stop controlling anything.
 *
 * The test asserts that this fixture FAILS on the same input where the real
 * parser succeeds. If it ever starts passing, the test is no longer
 * sensitive to the bug it exists to detect.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define ZIP_END_CENTRAL_DIR_SIG 0x06054B50

static unsigned int read32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int main(int argc, char **argv) {
    if (argc < 2) { puts("Usage: legacy_apk_parser_8k <file.apk>"); return 1; }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { printf("cannot open '%s'\n", argv[1]); return 1; }

    char buf[8192];
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) { puts("apk-parser: empty file"); return 1; }

    int eocd_off = -1;
    for (int i = n - 22; i >= 0; i--) {
        if (read32((const unsigned char *)buf + i) == ZIP_END_CENTRAL_DIR_SIG) {
            eocd_off = i;
            break;
        }
    }
    if (eocd_off < 0) {
        /* The exact wrong message the old parser emitted. */
        puts("Error: not a valid ZIP/APK file");
        return 1;
    }

    printf("Entries: %u\n",
           (unsigned)((unsigned char)buf[eocd_off + 10]) |
           ((unsigned)((unsigned char)buf[eocd_off + 11]) << 8));
    return 0;
}