#include "rootfs.h"
#include "container.h"
#include "string.h"
#include "kernel/kprintf.h"

static rootfs_entry_t rootfs_table[ROOTFS_MAX];
static int rootfs_count = 0;
static int rootfs_next_id = 1;

/* ─── Helpers ─── */

static int path_join(const char *a, const char *b, char *out, int max) {
    size_t len = strlen(a);
    if (len + 1 + strlen(b) >= (size_t)max) return -1;
    memcpy(out, a, len);
    if (len > 0 && out[len - 1] != '/') out[len++] = '/';
    memcpy(out + len, b, strlen(b) + 1);
    return 0;
}

static int ensure_dir(const char *path) {
    if (!path) return -1;
    /* In a real implementation, this would call the VFS mkdir.
     * For now, log the attempt so we know when real I/O is needed. */
    kprintf("[rootfs] ensure_dir: %s (stub)\n", path);
    return 0;
}

/* ─── Init ─── */

int rootfs_init(void) {
    memset(rootfs_table, 0, sizeof(rootfs_table));
    rootfs_count = 0;
    rootfs_next_id = 1;

    kprintf("[rootfs] initialized\n");

    rootfs_add_debian_minimal();
    rootfs_add_alpine_minimal();
    rootfs_add_android_stock();

    return 0;
}

/* ─── Registration ─── */

int rootfs_register(const char *name, rootfs_type_t type, const char *url,
                    const char *hash, const char *arch) {
    if (rootfs_count >= ROOTFS_MAX) return -1;
    if (!name || !url) return -1;

    for (int i = 0; i < ROOTFS_MAX; i++) {
        if (rootfs_table[i].id && strcmp(rootfs_table[i].name, name) == 0)
            return -1;
    }

    rootfs_entry_t *e = &rootfs_table[rootfs_count];
    memset(e, 0, sizeof(*e));

    e->id = rootfs_next_id++;
    strlcpy(e->name, name, ROOTFS_NAME_MAX);
    e->type = type;
    e->state = ROOTFS_STATE_NONE;
    strlcpy(e->url, url, ROOTFS_URL_MAX);
    if (hash) strlcpy(e->hash, hash, ROOTFS_HASH_MAX);
    if (arch) strlcpy(e->arch, arch, sizeof(e->arch));
    else strlcpy(e->arch, "x86_64", sizeof(e->arch));

    path_join(ROOTFS_CACHE_DIR, name, e->path, ROOTFS_PATH_MAX);

    rootfs_count++;
    return e->id;
}

int rootfs_unregister(int id) {
    for (int i = 0; i < ROOTFS_MAX; i++) {
        if (rootfs_table[i].id == id) {
            if (rootfs_table[i].ref_count > 0) return -1;
            memmove(&rootfs_table[i], &rootfs_table[i + 1],
                    (rootfs_count - i - 1) * sizeof(rootfs_entry_t));
            rootfs_count--;
            return 0;
        }
    }
    return -1;
}

/* ─── Download / Extract ─── */

int rootfs_download(int id) {
    rootfs_entry_t *e = rootfs_get(id);
    if (!e) return -1;

    e->state = ROOTFS_STATE_DOWNLOADING;
    kprintf("[rootfs] downloading '%s' from %s...\n", e->name, e->url);

    /* TODO: Implement actual HTTP download using net.c TCP/HTTP stack.
     * The network stack (net.c) already supports HTTP GET with progress
     * callbacks. Once integrated, this should:
     *   1. Open a TCP connection to the URL host
     *   2. Send HTTP GET request
     *   3. Stream response to ROOTFS_CACHE_DIR/<name>/
     *   4. Verify hash if provided
     *   5. Set state to READY on success, ERROR on failure
     */
    kprintf("[rootfs] download stub: would fetch %s\n", e->url);
    e->state = ROOTFS_STATE_READY;
    return 0;
}

int rootfs_extract(int id) {
    rootfs_entry_t *e = rootfs_get(id);
    if (!e) return -1;

    e->state = ROOTFS_STATE_EXTRACTING;
    ensure_dir(e->path);

    kprintf("[rootfs] extracting '%s' to %s...\n", e->name, e->path);

    /* TODO: Implement archive extraction based on rootfs type:
     *   - ROOTFS_LINUX_DEBIAN: ISO extraction (loop mount + copy)
     *   - ROOTFS_LINUX_ALPINE: tar.gz extraction
     *   - ROOTFS_ANDROID_STOCK: ZIP extraction
     * Should populate the extracted content into e->path and
     * set e->installed_size to the total extracted size.
     */
    kprintf("[rootfs] extract stub: would unpack to %s\n", e->path);
    e->state = ROOTFS_STATE_READY;
    return 0;
}

int rootfs_update(int id) {
    rootfs_entry_t *e = rootfs_get(id);
    if (!e) return -1;

    e->state = ROOTFS_STATE_OUTDATED;
    int rc = rootfs_download(id);
    if (rc < 0) return rc;
    rc = rootfs_extract(id);
    if (rc < 0) return rc;
    e->last_updated = 0;
    return 0;
}

int rootfs_remove(int id) {
    rootfs_entry_t *e = rootfs_get(id);
    if (!e) return -1;
    if (e->ref_count > 0) return -1;
    e->state = ROOTFS_STATE_NONE;
    return 0;
}

/* ─── Container rootfs setup ─── */

int rootfs_create_container_rootfs(const char *rootfs_name,
                                   const char *container_name,
                                   char *out_path, int max) {
    rootfs_entry_t *root = rootfs_find(rootfs_name);
    if (!root || root->state != ROOTFS_STATE_READY) return -1;

    char container_dir[ROOTFS_PATH_MAX];
    path_join(ROOTFS_CONTAINER_DIR, container_name, container_dir, ROOTFS_PATH_MAX);
    ensure_dir(container_dir);

    if (out_path && max > 0) {
        strlcpy(out_path, root->path, max);
    }

    root->ref_count++;
    return 0;
}

int rootfs_setup_mounts(const char *rootfs_path, const char *container_path) {
    if (!rootfs_path || !container_path) return -1;
    return 0;
}

int rootfs_teardown_mounts(const char *container_path) {
    if (!container_path) return -1;
    return 0;
}

/* ─── Queries ─── */

rootfs_entry_t *rootfs_get(int id) {
    for (int i = 0; i < ROOTFS_MAX; i++) {
        if (rootfs_table[i].id == id) return &rootfs_table[i];
    }
    return NULL;
}

rootfs_entry_t *rootfs_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < ROOTFS_MAX; i++) {
        if (rootfs_table[i].id && strcmp(rootfs_table[i].name, name) == 0)
            return &rootfs_table[i];
    }
    return NULL;
}

int rootfs_list(char names[][ROOTFS_NAME_MAX], int max) {
    int count = 0;
    for (int i = 0; i < ROOTFS_MAX && count < max; i++) {
        if (rootfs_table[i].id) {
            strlcpy(names[count], rootfs_table[i].name, ROOTFS_NAME_MAX);
            count++;
        }
    }
    return count;
}

int rootfs_list_all(char names[][ROOTFS_NAME_MAX], int max) {
    return rootfs_list(names, max);
}

int rootfs_get_state(int id) {
    rootfs_entry_t *e = rootfs_get(id);
    if (!e) return -1;
    return (int)e->state;
}

const char *rootfs_type_str(rootfs_type_t type) {
    switch (type) {
        case ROOTFS_LINUX_DEBIAN:    return "debian";
        case ROOTFS_LINUX_ALPINE:    return "alpine";
        case ROOTFS_LINUX_FEDORA:    return "fedora";
        case ROOTFS_ANDROID_STOCK:   return "android-stock";
        case ROOTFS_ANDROID_LINEAGE: return "android-lineage";
        case ROOTFS_CUSTOM:          return "custom";
        default:                     return "unknown";
    }
}

const char *rootfs_state_str(rootfs_state_t state) {
    switch (state) {
        case ROOTFS_STATE_NONE:        return "none";
        case ROOTFS_STATE_DOWNLOADING: return "downloading";
        case ROOTFS_STATE_EXTRACTING:  return "extracting";
        case ROOTFS_STATE_READY:       return "ready";
        case ROOTFS_STATE_ERROR:       return "error";
        case ROOTFS_STATE_OUTDATED:    return "outdated";
        default:                       return "unknown";
    }
}

/* ─── Pre-configured templates ─── */

int rootfs_add_debian_minimal(void) {
    return rootfs_register(
        "debian-minimal", ROOTFS_LINUX_DEBIAN,
        "https://cdimage.debian.org/debian-cd/8.11.1/amd64/iso-cd/debian-8.11.1-amd64-netinst.iso",
        "", "x86_64"
    );
}

/* ─── Extract a minimal Debian 8 (Jessie) rootfs into the container image dir ─── */
int rootfs_extract_debian_minimal(void) {
    const char *base = "/containers/images/debian-minimal";
    int is_dir;

    /* Already extracted? */
    if (fs_resolve(base, &is_dir) >= 0 && is_dir) {
        char shpath[FS_PATH_MAX];
        snprintf(shpath, sizeof(shpath), "%s/bin/sh", base);
        if (fs_resolve(shpath, &is_dir) >= 0) {
            kprintf("[rootfs] debian-minimal already extracted\n");
            return 0;
        }
    }

    kprintf("[rootfs] extracting debian 8 (jessie) minimal rootfs...\n");

    /* Core directories */
    fs_mkdir("/containers");
    fs_mkdir("/containers/images");
    fs_mkdir(base);

    char path[FS_PATH_MAX];
    const char *dirs[] = {
        "bin", "sbin", "etc", "lib", "lib64", "usr", "usr/bin", "usr/sbin",
        "usr/lib", "usr/share", "var", "var/log", "var/lib", "var/tmp",
        "tmp", "proc", "sys", "dev", "run", "opt", "root", "home",
        "mnt", "media", "srv", "boot", "etc/default", "etc/init.d",
        "etc/network", "etc/apt", "lib/modules", "lib/x86_64-linux-gnu",
        NULL
    };
    for (int i = 0; dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", base, dirs[i]);
        fs_mkdir(path);
    }

    /* /etc/hostname */
    snprintf(path, sizeof(path), "%s/etc/hostname", base);
    fs_mkfile(path);
    fs_write(path, "codeos-lt", 9);

    /* /etc/hosts */
    snprintf(path, sizeof(path), "%s/etc/hosts", base);
    fs_mkfile(path);
    fs_write(path, "127.0.0.1 localhost codeos-lt\n::1 localhost ip6-localhost ip6-loopback\n", 75);

    /* /etc/passwd */
    snprintf(path, sizeof(path), "%s/etc/passwd", base);
    fs_mkfile(path);
    fs_write(path,
        "root:x:0:0:root:/root:/bin/sh\n"
        "daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin\n"
        "nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin\n", 120);

    /* /etc/shadow */
    snprintf(path, sizeof(path), "%s/etc/shadow", base);
    fs_mkfile(path);
    fs_write(path, "root:::0:99999:7:::\n", 19);

    /* /etc/group */
    snprintf(path, sizeof(path), "%s/etc/group", base);
    fs_mkfile(path);
    fs_write(path,
        "root:x:0:\n"
        "daemon:x:1:\n"
        "bin:x:2:\n"
        "sys:x:3:\n"
        "adm:x:4:\n"
        "tty:x:5:\n"
        "disk:x:6:\n"
        "sudo:x:27:\n"
        "nobody:x:65534:\n", 99);

    /* /etc/fstab */
    snprintf(path, sizeof(path), "%s/etc/fstab", base);
    fs_mkfile(path);
    fs_write(path, "# <filesystem>  <mount>  <type>  <opts>  <dump> <pass>\nproc /proc proc defaults 0 0\n", 81);

    /* /etc/resolv.conf */
    snprintf(path, sizeof(path), "%s/etc/resolv.conf", base);
    fs_mkfile(path);
    fs_write(path, "nameserver 8.8.8.8\nnameserver 8.8.4.4\n", 40);

    /* /etc/apt/sources.list (Jessie) */
    snprintf(path, sizeof(path), "%s/etc/apt/sources.list", base);
    fs_mkfile(path);
    fs_write(path,
        "deb http://deb.debian.org/debian jessie main\n"
        "deb http://deb.debian.org/debian jessie-updates main\n"
        "deb http://security.debian.org/debian-security jessie/updates main\n", 161);

    /* /etc/os-release */
    snprintf(path, sizeof(path), "%s/etc/os-release", base);
    fs_mkfile(path);
    fs_write(path,
        "PRETTY_NAME=\"Debian GNU/Linux 8 (jessie)\"\n"
        "NAME=\"Debian GNU/Linux\"\n"
        "VERSION_ID=\"8\"\n"
        "VERSION=\"8 (jessie)\"\n"
        "ID=debian\n"
        "HOME_URL=\"https://www.debian.org/\"\n"
        "SUPPORT_URL=\"https://www.debian.org/support\"\n"
        "BUG_REPORT_URL=\"https://bugs.debian.org/\"\n", 211);

    /* /etc/issue */
    snprintf(path, sizeof(path), "%s/etc/issue", base);
    fs_mkfile(path);
    fs_write(path, "Debian GNU/Linux 8 (jessie) \\n \\l\n\n", 35);

    /* /etc/motd — Debian 8 MOTD */
    snprintf(path, sizeof(path), "%s/etc/motd", base);
    fs_mkfile(path);
    fs_write(path,
        " Debian 8 (jessie)\n"
        " CodeOS Linux Terminal (LT)\n"
        " Kernel-based container with namespace isolation\n\n", 103);

    /* /etc/shells */
    snprintf(path, sizeof(path), "%s/etc/shells", base);
    fs_mkfile(path);
    fs_write(path, "/bin/sh\n/bin/bash\n/usr/bin/sh\n", 30);

    /* /etc/locale.gen */
    snprintf(path, sizeof(path), "%s/etc/locale.gen", base);
    fs_mkfile(path);
    fs_write(path, "en_US.UTF-8 UTF-8\n", 18);

    /* /etc/locale */
    snprintf(path, sizeof(path), "%s/etc/locale", base);
    fs_mkfile(path);
    fs_write(path, "LANG=en_US.UTF-8\n", 17);

    /* /etc/timezone */
    snprintf(path, sizeof(path), "%s/etc/timezone", base);
    fs_mkfile(path);
    fs_write(path, "UTC\n", 3);

    /* /etc/sysctl.conf */
    snprintf(path, sizeof(path), "%s/etc/sysctl.conf", base);
    fs_mkfile(path);
    fs_write(path, "# /etc/sysctl.conf - Debian 8\n", 29);

    /* /etc/init.d/ directory structure */
    snprintf(path, sizeof(path), "%s/etc/init.d/rcS", base);
    fs_mkfile(path);
    fs_write(path, "#!/bin/sh\n# System init script\n", 29);

    /* /root/.bashrc */
    snprintf(path, sizeof(path), "%s/root/.bashrc", base);
    fs_mkdir("/containers/images/debian-minimal/root");
    fs_mkfile(path);
    fs_write(path,
        "# ~/.bashrc: executed by bash for non-login shells\n"
        "export PS1='\\u@\\h:\\w\\$ '\n"
        "export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\n"
        "alias ll='ls -la'\n", 140);

    /* /proc/cpuinfo placeholder */
    snprintf(path, sizeof(path), "%s/proc/cpuinfo", base);
    fs_mkfile(path);
    fs_write(path, "processor : 0\nmodel name : CodeOS Virtual CPU\n", 46);

    /* /proc/meminfo placeholder */
    snprintf(path, sizeof(path), "%s/proc/meminfo", base);
    fs_mkfile(path);
    fs_write(path, "MemTotal: 1048576 kB\nMemFree: 524288 kB\n", 41);

    /* /proc/version */
    snprintf(path, sizeof(path), "%s/proc/version", base);
    fs_mkfile(path);
    fs_write(path, "CodeOS 1.0 (jessie) #1 SMP Mon Aug 17 2026 x86_64 GNU/Linux\n", 61);

    /* /proc/self/ directory */
    fs_mkdir("/containers/images/debian-minimal/proc/self");
    snprintf(path, sizeof(path), "%s/proc/self/status", base);
    fs_mkfile(path);
    fs_write(path, "Name:\tsh\nState:\tS (sleeping)\nPid:\t1\nPPid:\t0\n", 48);

    /* /etc/network/interfaces */
    snprintf(path, sizeof(path), "%s/etc/network/interfaces", base);
    fs_mkfile(path);
    fs_write(path,
        "# loopback\nauto lo\niface lo inet loopback\n"
        "# container eth0\nauto eth0\niface eth0 inet dhcp\n", 96);

    /* Mark rootfs as ready */
    rootfs_entry_t *e = rootfs_find("debian-minimal");
    if (e) {
        e->state = ROOTFS_STATE_READY;
        e->version_major = 8;
        e->version_minor = 11;
        e->version_patch = 1;
        e->installed_size = 256 * 1024;
        strlcpy(e->init_binary, "/sbin/init", sizeof(e->init_binary));
    }

    kprintf("[rootfs] debian 8 (jessie) minimal rootfs extracted to %s\n", base);
    return 0;
}

int rootfs_add_alpine_minimal(void) {
    return rootfs_register(
        "alpine-minimal", ROOTFS_LINUX_ALPINE,
        "https://dl-cdn.alpinelinux.org/alpine/v3.21/releases/x86_64/alpine-minirootfs-3.21.3-x86_64.tar.gz",
        "", "x86_64"
    );
}

int rootfs_add_android_stock(void) {
    return rootfs_register(
        "android-stock", ROOTFS_ANDROID_STOCK,
        "https://dl.google.com/android/repository/sys-img/google_apis/x86_64-36_r03.zip",
        "", "x86_64"
    );
}

/* ─── Seed the Android stock container image ───
 * Materializes a minimal container root at
 *   /containers/images/android-stock/{init,bin/sh,system/bin/run-as}
 * plus build.prop and the bundled apps at
 *   /containers/images/android-stock/system/app/<name>/<name>
 * by copying the boot-time /bin/run-as and /bin/android-* ELFs into place.
 * The root is used directly by container_create/start/exec (see container.c).
 * Fully idempotent: existing files are skipped. */
const char *const rootfs_android_apps[ROOTFS_ANDROID_APP_COUNT + 1] = {
    "android-launcher", "android-clock", "android-calculator",
    "android-settings", "android-dialer", "android-music",
    "android-browser",  "android-camera", "android-calendar",
    "android-keyboard", NULL
};

int rootfs_seed_android_stock(void) {
    const char *base = "/containers/images/android-stock";
    char boot_elf[FS_CONTENT_MAX];
    int is_dir;

    if (fs_resolve("/bin/run-as", &is_dir) < 0) {
        kprintf("[rootfs] android-stock: cannot find /bin/run-as to seed\n");
        return -1;
    }

    int n = fs_read("/bin/run-as", boot_elf, FS_CONTENT_MAX);
    if (n <= 0) {
        kprintf("[rootfs] android-stock: failed to read /bin/run-as (n=%d)\n", n);
        return -1;
    }

    kprintf("[rootfs] seeding android-stock container image (%d bytes)\n", n);

    fs_mkdir("/containers");
    fs_mkdir("/containers/images");
    fs_mkdir(base);
    fs_mkdir("/containers/images/android-stock/bin");
    fs_mkdir("/containers/images/android-stock/system");
    fs_mkdir("/containers/images/android-stock/system/bin");

    static const char *targets[] = {
        "/containers/images/android-stock/init",
        "/containers/images/android-stock/bin/sh",
        "/containers/images/android-stock/system/bin/run-as",
        NULL
    };

    for (int i = 0; targets[i]; i++) {
        if (fs_resolve(targets[i], &is_dir) >= 0) {
            kprintf("[rootfs] android-stock: %s already present\n", targets[i]);
            continue;
        }
        if (fs_mkfile(targets[i]) < 0) {
            kprintf("[rootfs] android-stock: cannot create %s\n", targets[i]);
            continue;
        }
        if (fs_write(targets[i], boot_elf, n) < 0) {
            kprintf("[rootfs] android-stock: cannot write %s\n", targets[i]);
            continue;
        }
        kprintf("[rootfs] android-stock: seeded %s\n", targets[i]);
    }

    /* ── Android metadata + bundled apps (idempotent) ──
     * The stock image ships complete (build.prop + all 10 apps under
     * /system/app/<name>/<name>) so plain appvm/container use of the
     * image works without a separate install step. */
    {
        char dir[FS_PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/system", base);
        fs_mkdir(dir);
        snprintf(dir, sizeof(dir), "%s/system/app", base);
        fs_mkdir(dir);

        char prop[FS_PATH_MAX];
        snprintf(prop, sizeof(prop), "%s/system/build.prop", base);
        if (fs_resolve(prop, &is_dir) < 0) {
            static const char prop_content[] =
                "# Waydroid build fingerprint (CodeOS)\n"
                "ro.build.fingerprint=CodeOS/waydroid/x86_64:12/SKQ1.211006.001/waydroid:user/release-keys\n"
                "ro.build.version.sdk=31\n"
                "ro.build.version.release=12\n"
                "ro.product.model=CodeOS Android\n"
                "ro.product.device=waydroid\n"
                "ro.hardware=codeos\n"
                "ro.secure=0\n"
                "ro.debuggable=1\n"
                "persist.sys.dalvik.vm.lib.2=libart.so\n";
            if (fs_mkfile(prop) == 0)
                fs_write(prop, prop_content, (int)sizeof(prop_content) - 1);
            kprintf("[rootfs] android-stock: wrote %s/system/build.prop\n", base);
        }
    }

    for (int i = 0; i < ROOTFS_ANDROID_APP_COUNT; i++) {
        const char *app = rootfs_android_apps[i];
        char src[FS_PATH_MAX], appdir[FS_PATH_MAX], dst[FS_PATH_MAX];
        snprintf(src, sizeof(src), "/bin/%s", app);
        snprintf(appdir, sizeof(appdir), "%s/system/app/%s", base, app);
        snprintf(dst, sizeof(dst), "%s/%s", appdir, app);
        if (fs_resolve(dst, &is_dir) >= 0) continue; /* already installed */
        int an = fs_read(src, boot_elf, sizeof(boot_elf));
        if (an <= 0) {
            kprintf("[rootfs] android-stock: skip %s (not present)\n", app);
            continue;
        }
        fs_mkdir(appdir);
        if (fs_mkfile(dst) < 0) {
            kprintf("[rootfs] android-stock: cannot create %s\n", dst);
            continue;
        }
        fs_write(dst, boot_elf, an);
        kprintf("[rootfs] android-stock: installed app %s\n", app);
    }

    return 0;
}

/* ─── Disk usage ─── */

uint64_t rootfs_get_disk_usage(int id) {
    rootfs_entry_t *e = rootfs_get(id);
    if (!e) return 0;
    return e->installed_size;
}

uint64_t rootfs_get_total_disk_usage(void) {
    uint64_t total = 0;
    for (int i = 0; i < ROOTFS_MAX; i++) {
        if (rootfs_table[i].id) total += rootfs_table[i].installed_size;
    }
    return total;
}
