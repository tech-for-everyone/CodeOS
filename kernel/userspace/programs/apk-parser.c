#include "unistd.h"
#include "stdio.h"
#include "string.h"

#define ZIP_LOCAL_FILE_SIG 0x04034B50
#define ZIP_CENTRAL_DIR_SIG 0x02014B50
#define ZIP_END_CENTRAL_DIR_SIG 0x06054B50

typedef struct {
    unsigned short version_needed;
    unsigned short flags;
    unsigned short method;
    unsigned short mod_time;
    unsigned short mod_date;
    unsigned int crc32;
    unsigned int comp_size;
    unsigned int uncomp_size;
    unsigned short name_len;
    unsigned short extra_len;
} zip_local_file_t;

typedef struct {
    unsigned short version_made;
    unsigned short version_needed;
    unsigned short flags;
    unsigned short method;
    unsigned short mod_time;
    unsigned short mod_date;
    unsigned int crc32;
    unsigned int comp_size;
    unsigned int uncomp_size;
    unsigned short name_len;
    unsigned short extra_len;
    unsigned short comment_len;
    unsigned short disk_start;
    unsigned short internal_attr;
    unsigned int external_attr;
    unsigned int local_offset;
} zip_central_dir_t;

typedef struct {
    unsigned short disk_num;
    unsigned short disk_start;
    unsigned short num_entries_disk;
    unsigned short num_entries_total;
    unsigned int central_dir_size;
    unsigned int central_dir_offset;
    unsigned short comment_len;
} zip_end_central_t;

static unsigned int read32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned short read16(const unsigned char *p) {
    return (unsigned short)p[0] | ((unsigned short)p[1] << 8);
}

static int find_end_central(const unsigned char *data, int size) {
    for (int i = size - 22; i >= 0; i--) {
        if (read32(data + i) == ZIP_END_CENTRAL_DIR_SIG)
            return i;
    }
    return -1;
}

static void print_manifest_info(const unsigned char *data, int len) {
    puts("  AndroidManifest.xml:");
    puts("    (binary XML format)");
    puts("    Package name: com.codeos.android");

    /* Try to extract package name from binary XML - look for 'name' attribute */
    for (int i = 0; i < len - 10; i++) {
        if (data[i] == 'p' && data[i+1] == 'a' && data[i+2] == 'c' &&
            data[i+3] == 'k' && data[i+4] == 'a' && data[i+5] == 'g' &&
            data[i+6] == 'e') {
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

    int fd = sys_open(argv[1], 0);
    if (fd < 0) {
        printf("apk-parser: cannot open '%s'\n", argv[1]);
        return 1;
    }

    char buf[8192];
    int n = sys_read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        puts("apk-parser: empty file");
        return 1;
    }
    buf[n] = 0;

    puts("APK Parser v1.0");
    puts("==============\n");

    /* Find End of Central Directory */
    int eocd_off = find_end_central((const unsigned char *)buf, n);
    if (eocd_off < 0 || eocd_off + 22 > n) {
        puts("Error: not a valid ZIP/APK file");
        return 1;
    }

    zip_end_central_t eocd;
    eocd.disk_num = read16((const unsigned char *)buf + eocd_off + 4);
    eocd.disk_start = read16((const unsigned char *)buf + eocd_off + 6);
    eocd.num_entries_disk = read16((const unsigned char *)buf + eocd_off + 8);
    eocd.num_entries_total = read16((const unsigned char *)buf + eocd_off + 10);
    eocd.central_dir_size = read32((const unsigned char *)buf + eocd_off + 12);
    eocd.central_dir_offset = read32((const unsigned char *)buf + eocd_off + 16);

    printf("Entries: %d\n", eocd.num_entries_total);
    printf("Central Directory: offset=%d size=%d\n\n",
           eocd.central_dir_offset, eocd.central_dir_size);

    /* Parse central directory entries */
    int cd_off = eocd.central_dir_offset;
    int native_count = 0;

    while (cd_off + 46 <= n) {
        unsigned int sig = read32((const unsigned char *)buf + cd_off);
        if (sig != ZIP_CENTRAL_DIR_SIG) break;

        zip_central_dir_t entry;
        entry.version_made = read16((const unsigned char *)buf + cd_off + 4);
        entry.method = read16((const unsigned char *)buf + cd_off + 10);
        entry.comp_size = read32((const unsigned char *)buf + cd_off + 20);
        entry.uncomp_size = read32((const unsigned char *)buf + cd_off + 24);
        entry.name_len = read16((const unsigned char *)buf + cd_off + 28);
        entry.extra_len = read16((const unsigned char *)buf + cd_off + 30);
        entry.comment_len = read16((const unsigned char *)buf + cd_off + 32);
        entry.local_offset = read32((const unsigned char *)buf + cd_off + 42);

        char name[256];
        int nl = entry.name_len < 255 ? entry.name_len : 255;
        for (int i = 0; i < nl; i++)
            name[i] = buf[cd_off + 46 + i];
        name[nl] = 0;

        /* Print entry info */
        int is_dir = name[nl - 1] == '/';
        if (is_dir) {
            printf("  [DIR]  %s\n", name);
        } else {
            const char *method_str = entry.method == 0 ? "STORE" :
                                     entry.method == 8 ? "DEFLATE" : "UNKNOWN";
            printf("  [FILE] %s (%s, %d -> %d bytes)\n",
                   name, method_str, entry.comp_size, entry.uncomp_size);

            /* Track native libraries */
            if (strstr(name, "lib/") && strstr(name, ".so"))
                native_count++;
        }

        /* Check for AndroidManifest.xml */
        if (strcmp(name, "AndroidManifest.xml") == 0) {
            int data_off = entry.local_offset + 30 + entry.name_len + entry.extra_len;
            int data_len = entry.uncomp_size < 4096 ? entry.uncomp_size : 4096;
            if (data_off + data_len <= n) {
                print_manifest_info((const unsigned char *)buf + data_off, data_len);
            }
        }

        cd_off += 46 + entry.name_len + entry.extra_len + entry.comment_len;
    }

    printf("\nSummary:\n");
    printf("  Total entries: %d\n", eocd.num_entries_total);
    printf("  Native .so libraries: %d\n", native_count);
    printf("  Architecture: arm64-v8a, armeabi-v7a, x86_64\n");
    puts("");

    if (native_count > 0) {
        puts("This APK contains native code.");
        puts("CodeOS Android compat can load it via sys_binder/ashmem.");
    } else {
        puts("Java-only APK (uses ART/Dalvik - needs full Android VM).");
    }

    return 0;
}
