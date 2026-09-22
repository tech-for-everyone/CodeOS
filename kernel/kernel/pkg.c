#include "pkg.h"
#include "kprintf.h"
#include "string.h"
#include "fs.h"
#include "net.h"
#include "url.h"
#include "../drivers/timer.h"
#include "mbedtls/base64.h"

#define PKG_REPOS_FILE   PKG_DIR "/repos.index"
#define PKG_STONE_INDEX  PKG_CACHE_DIR "/stone.index"
#define PKG_STONE_EXT    ".stone"
#define PKG_FTECH_EXT    ".ftech"
#define PKG_PAYLOAD_DIR  PKG_CACHE_DIR "/payloads"

static pkg_info_t packages[PKG_MAX_PACKAGES];
static int pkg_count = 0;
static int pkg_cache_updated = 0;
static uint32_t pkg_metadata_ts = 0;

static char held_packages[32][PKG_NAME_MAX];
static int held_count = 0;

static repository_t repositories[PKG_MAX_REPOS];
static int repo_count = 0;

/* Transaction log */
static pkg_txn_t txn_log[PKG_MAX_TXN];
static int txn_count = 0;
static int txn_head = 0;

/* Package groups */
static pkg_group_t groups[PKG_MAX_GROUPS];
static int group_count = 0;

/* Forward declarations */
static void pkg_init_repos(void);
static void pkg_init_groups(void);
static void pkg_seed_payloads(void);
void pkg_txn_log(const char *name, const char *version, int action, const char *old_version);

/* Network-fetched packages (parsed from stone.index) live in a dynamic
   table appended to all_repos[] as the last repo (REMOTE_REPO_IDX). */
#define PKG_MAX_REMOTE 256
#define REMOTE_REPO_IDX 6

static pkg_repo_t remote_repo[PKG_MAX_REMOTE + 1];
static char remote_name[PKG_MAX_REMOTE][PKG_NAME_MAX];
static char remote_ver[PKG_MAX_REMOTE][PKG_VERSION_MAX];
static char remote_desc[PKG_MAX_REMOTE][PKG_DESC_MAX];
static char remote_src[PKG_MAX_REMOTE][PKG_REPO_NAME_MAX]; /* source repo */
static uint32_t remote_size[PKG_MAX_REMOTE];
static int remote_count = 0;

static uint32_t pkg_strtouint(const char *s);
static int pkg_find_installed(const char *name);

static void pkg_save_db(void) {
    char buf[FS_CONTENT_MAX];
    int n = 0;
    for (int i = 0; i < pkg_count; i++) {
        pkg_info_t *p = &packages[i];
        int written = snprintf(buf + n, sizeof(buf) - n,
                               "%s\t%s\t%u\t%s\n",
                               p->name, p->version, p->size, p->description);
        if (written < 0 || written >= (int)sizeof(buf) - n) break;
        n += written;
    }
    fs_mkdir(PKG_DIR);
    fs_mkfile(PKG_DB_FILE);
    if (n > 0) fs_write(PKG_DB_FILE, buf, n);
}

static void pkg_load_db(void) {
    char buf[FS_CONTENT_MAX];
    int len = fs_read(PKG_DB_FILE, buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = 0;

    char *line = buf;
    while (line && *line && pkg_count < PKG_MAX_PACKAGES) {
        char *next = strchr(line, '\n');
        if (next) *next = 0;
        char *name = line;
        char *version = strchr(name, '\t');
        if (version) {
            *version++ = 0;
            char *size_text = strchr(version, '\t');
            if (size_text) {
                *size_text++ = 0;
                char *description = strchr(size_text, '\t');
                if (description) {
                    *description++ = 0;
                    pkg_info_t *p = &packages[pkg_count];
                    if (name[0] && version[0] && pkg_find_installed(name) < 0 &&
                        strncpy_safe(p->name, name, PKG_NAME_MAX) == 0 &&
                        strncpy_safe(p->version, version, PKG_VERSION_MAX) == 0 &&
                        strncpy_safe(p->description, description, PKG_DESC_MAX) == 0) {
                        p->size = pkg_strtouint(size_text);
                        p->installed = 1;
                        p->priority = 0;
                        p->category = 0;
                        p->dep_count = 0;
                        p->conflict_count = 0;
                        p->provides_count = 0;
                        p->file_count = 0;
                        p->homepage[0] = 0;
                        p->license[0] = 0;
                        pkg_count++;
                    }
                }
            }
        }
        line = next ? next + 1 : NULL;
    }
}

static const pkg_repo_t *all_repos[] = {
    pkg_repo_core, pkg_repo_extra, pkg_repo_dev,
    pkg_repo_ccp, pkg_repo_aur, pkg_repo_android,
    remote_repo, NULL
};

static const char *cat_names[PKG_CATEGORY_COUNT] = {
    "Uncategorized",
    "System",
    "Development",
    "Network",
    "Multimedia",
    "Office",
    "Utilities",
    "Games",
    "Security",
    "Android"
};

const char *pkg_category_name(int cat) {
    if (cat < 0 || cat >= PKG_CATEGORY_COUNT) return "Unknown";
    return cat_names[cat];
}

/* ── Moss-style fetch/sync helpers ── */

static uint32_t pkg_now(void) {
    return (uint32_t)(timer_get_milliseconds() / 1000);
}

static uint32_t pkg_strtouint(const char *s) {
    uint32_t v = 0;
    while (s && *s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

/* Static repo tables in the order they appear in all_repos[] */
static const char *pkg_tab_names[] = {
    "core", "extra", "dev", "ccp", "aur", "android", NULL
};

static int pkg_repo_all_idx(const char *name) {
    for (int i = 0; pkg_tab_names[i]; i++)
        if (strcmp(pkg_tab_names[i], name) == 0) return i;
    return -1;
}

/* Add a remote package discovered via stone.index. Dedupes by name. */
static void pkg_remote_add(const char *name, const char *version,
                           uint32_t size, const char *src_repo) {
    if (remote_count >= PKG_MAX_REMOTE) return;
    for (int i = 0; i < remote_count; i++)
        if (strcmp(remote_name[i], name) == 0) return;
    if (strncpy_safe(remote_name[remote_count], name, PKG_NAME_MAX) < 0 ||
        strncpy_safe(remote_ver[remote_count], version, PKG_VERSION_MAX) < 0 ||
        strncpy_safe(remote_src[remote_count], src_repo, PKG_REPO_NAME_MAX) < 0)
        return;
    remote_size[remote_count] = size;
    remote_desc[remote_count][0] = 0;
    remote_repo[remote_count].name = remote_name[remote_count];
    remote_repo[remote_count].version = remote_ver[remote_count];
    remote_repo[remote_count].desc = remote_desc[remote_count];
    remote_repo[remote_count].homepage = "";
    remote_repo[remote_count].license = "";
    remote_repo[remote_count].size = size;
    remote_repo[remote_count].depends = NULL;
    remote_repo[remote_count].dep_count = 0;
    remote_repo[remote_count].priority = 0;
    remote_repo[remote_count].conflicts = NULL;
    remote_repo[remote_count].conflict_count = 0;
    remote_repo[remote_count].provides = NULL;
    remote_repo[remote_count].provides_count = 0;
    remote_repo[remote_count].category = 0;
    remote_count++;
    remote_repo[remote_count].name = NULL; /* keep table terminated */
}

/* Parse a stone.index payload: one "name version size repo" line each. */
static int pkg_parse_stone_index(const char *buf, int len, const char *src_repo) {
    int added = 0;
    const char *p = buf;
    while (p && p < buf + len) {
        const char *nl = strchr(p, '\n');
        int llen = nl ? (int)(nl - p) : (int)(buf + len - p);
        char line[FS_CONTENT_MAX];
        if (llen >= (int)sizeof(line)) llen = (int)sizeof(line) - 1;
        memcpy(line, p, llen);
        line[llen] = 0;
        if (line[0] && line[0] != '#') {
            char *sp1 = strchr(line, ' ');
            if (sp1) {
                *sp1 = 0;
                char *sp2 = strchr(sp1 + 1, ' ');
                if (sp2) {
                    *sp2 = 0;
                    char *sp3 = strchr(sp2 + 1, ' ');
                    if (sp3) *sp3 = 0;
                    pkg_remote_add(line, sp1 + 1, pkg_strtouint(sp2 + 1),
                                   sp3 && sp3[1] ? sp3 + 1 : src_repo);
                    added++;
                }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    return added;
}

/* Fetch <repo_url>/stone.index over http(s) and merge into the remote table.
 * Returns the number of packages added, or -1 on failure. */
static int pkg_fetch_repo_index(const repository_t *repo) {
    if (!repo || !repo->url[0]) return -1;
    int use_tls = (strncmp(repo->url, "https://", 8) == 0);
    url_t u;
    if (url_parse(repo->url, &u) < 0) return -1;
    if (use_tls && u.port == 80) u.port = 443;

    char path[576];
    int plen = (int)strlen(u.path);
    if (plen == 0 || strcmp(u.path, "/") == 0)
        sprintf(path, "/stone.index");
    else if (u.path[plen - 1] == '/')
        sprintf(path, "%sstone.index", u.path);
    else
        sprintf(path, "%s/stone.index", u.path);

    char buf[2048];
    int n = use_tls ? https_get(u.host, u.port, path, buf, sizeof(buf) - 1)
                    : http_get(u.host, u.port, path, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = 0;
    return pkg_parse_stone_index(buf, n, repo->name);
}

/* Write the available-package index (moss: stone.index) for enabled repos. */
static int pkg_write_stone_index(void) {
    char buf[FS_CONTENT_MAX];
    int n = 0, entries = 0;
    for (int i = 0; i < repo_count; i++) {
        if (!repositories[i].enabled) continue;
        int allidx = pkg_repo_all_idx(repositories[i].name);
        if (allidx < 0 || !all_repos[allidx]) continue;
        for (int j = 0; all_repos[allidx][j].name; j++) {
            const pkg_repo_t *p = &all_repos[allidx][j];
            int need = (int)strlen(p->name) + (int)strlen(p->version) + 32 +
                       (int)strlen(repositories[i].name);
            if (n + need >= (int)sizeof(buf)) break;
            n += sprintf(buf + n, "%s %s %u %s\n",
                         p->name, p->version, p->size, repositories[i].name);
            entries++;
        }
    }
    /* Include network-fetched packages so the cache survives reboot. */
    for (int j = 0; j < remote_count; j++) {
        int need = (int)strlen(remote_name[j]) + (int)strlen(remote_ver[j]) + 32 +
                   (int)strlen(remote_src[j]);
        if (n + need >= (int)sizeof(buf)) break;
        n += sprintf(buf + n, "%s %s %u %s\n",
                     remote_name[j], remote_ver[j], remote_size[j], remote_src[j]);
        entries++;
    }
    if (entries == 0) return 0;
    fs_mkdir(PKG_CACHE_DIR);
    fs_mkfile(PKG_STONE_INDEX);
    if (fs_write(PKG_STONE_INDEX, buf, n) < 0) return 0;
    return entries;
}

/* Persist repo config so it survives reboot (moss: config in /.moss). */
static void pkg_save_repo_config(void) {
    char buf[FS_CONTENT_MAX];
    int n = sprintf(buf, "# CodeOS CCP repo config\n# last-update: %u\n", pkg_metadata_ts);
    for (int i = 0; i < repo_count && n < (int)sizeof(buf) - 96; i++)
        n += sprintf(buf + n, "%s %s %d\n",
                     repositories[i].name, repositories[i].url, repositories[i].enabled);
    fs_mkdir(PKG_DIR);
    fs_mkfile(PKG_REPOS_FILE);
    fs_write(PKG_REPOS_FILE, buf, n);
}

static void pkg_load_repo_config(void) {
    char buf[FS_CONTENT_MAX];
    int len = fs_read(PKG_REPOS_FILE, buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = 0;
    repo_count = 0;
    pkg_metadata_ts = 0;
    char *line = buf;
    while (line && *line && repo_count < PKG_MAX_REPOS) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] == '#') {
            if (strncmp(line, "# last-update:", 14) == 0)
                pkg_metadata_ts = pkg_strtouint(line + 14);
        } else if (line[0]) {
            char *sp1 = strchr(line, ' ');
            if (sp1) {
                *sp1 = 0;
                char *sp2 = strchr(sp1 + 1, ' ');
                if (sp2) {
                    *sp2 = 0;
                    strncpy_safe(repositories[repo_count].name, line, PKG_REPO_NAME_MAX);
                    strncpy_safe(repositories[repo_count].url, sp1 + 1, 128);
                    repositories[repo_count].enabled = (sp2[1] == '1');
                    repositories[repo_count].mirror_count = 0;
                    repo_count++;
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
}

/* Load the cached stone.index written by a previous pkg_repo_update(), so
 * network-fetched metadata survives a reboot without needing to re-fetch. */
static void pkg_load_stone_index(void) {
    char buf[FS_CONTENT_MAX];
    int len = fs_read(PKG_STONE_INDEX, buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = 0;
    remote_count = 0;
    memset(remote_repo, 0, sizeof(remote_repo));
    pkg_parse_stone_index(buf, len, "remote");
}

/* ── Semantic version comparison ── */

int pkg_version_cmp(const char *a, const char *b) {
    while (*a || *b) {
        int na = 0, nb = 0;
        while (*a && *a >= '0' && *a <= '9') { na = na * 10 + (*a - '0'); a++; }
        while (*b && *b >= '0' && *b <= '9') { nb = nb * 10 + (*b - '0'); b++; }
        if (na != nb) return (na > nb) ? 1 : -1;
        if (*a == '.' || *a == '-') a++;
        if (*b == '.' || *b == '-') b++;
        while (*a && *a == *b && *a != '.' && *a != '-' &&
               !(*a >= '0' && *a <= '9')) { a++; b++; }
        if (*a == 0 && *b == 0) return 0;
    }
    return 0;
}

/* ── Dependency helpers ── */

static int pkg_find_installed(const char *name) {
    for (int i = 0; i < pkg_count; i++)
        if (strcmp(packages[i].name, name) == 0) return i;
    return -1;
}

static int dep_is_satisfied(const char *name) {
    if (pkg_find_installed(name) >= 0) return 1;
    for (int i = 0; i < pkg_count; i++)
        for (int j = 0; j < packages[i].provides_count; j++)
            if (strcmp(packages[i].provides[j], name) == 0) return 1;
    return 0;
}

int pkg_core_count, pkg_extra_count, pkg_dev_count, pkg_ccp_count, pkg_aur_count, pkg_android_count;

static void count_repo_pkgs(void) {
    pkg_core_count = 0;  pkg_extra_count = 0;
    pkg_dev_count = 0;   pkg_ccp_count = 0;   pkg_aur_count = 0;
    pkg_ccp_count = 0;
    for (int i = 0; pkg_repo_core[i].name; i++) pkg_core_count++;
    for (int i = 0; pkg_repo_extra[i].name; i++) pkg_extra_count++;
    for (int i = 0; pkg_repo_dev[i].name; i++) pkg_dev_count++;
    for (int i = 0; pkg_repo_aur[i].name; i++) pkg_aur_count++;
    for (int i = 0; pkg_repo_ccp[i].name; i++) pkg_ccp_count++;
    for (int i = 0; pkg_repo_android[i].name; i++) pkg_android_count++;
}

/* ── Helpers for repo data ── */

const pkg_repo_t pkg_repo_core[] = {
    {"busybox", "1.25.0", "Core utilities and shell", "", "GPLv2", 1048576, (const char*[]){0}, 0, 2, 0, 0, 0, 0, 1},
    {"bash", "5.1.16", "GNU Bourne Again SHell", "", "GPLv3", 1228800, (const char*[]){"readline", 0}, 1, 2, 0, 0, 0, 0, 1},
    {"coreutils", "9.1", "Core file, shell, text utilities", "", "GPLv3", 2097152, 0, 0, 2, 0, 0, 0, 0, 1},
    {"grep", "3.7", "Pattern-matching text search", "", "GPLv3", 327680, 0, 0, 1, 0, 0, 0, 0, 6},
    {"sed", "4.9", "Stream editor for filtering/transforming text", "", "GPLv3", 294912, 0, 0, 1, 0, 0, 0, 0, 6},
    {"awk", "5.1.0", "Text processing language", "", "GPLv2", 409600, 0, 0, 1, 0, 0, 0, 0, 6},
    {"readline", "8.2", "Line input library for interactive programs", "", "GPLv3", 524288, 0, 0, 1, 0, 0, 0, 0, 3},
    {"glibc", "2.37", "GNU C Library", "", "LGPLv2.1", 5242880, 0, 0, 2, 0, 0, 0, 0, 1},
    {"zlib", "1.2.13", "Compression library", "", "Zlib", 262144, 0, 0, 1, 0, 0, 0, 0, 3},
    {"pcre2", "10.42", "Perl-compatible regular expressions", "", "BSD", 393216, 0, 0, 0, 0, 0, 0, 0, 3},
    {"ncurses", "6.4", "Terminal handling library (curses)", "", "MIT", 1572864, 0, 0, 1, 0, 0, 0, 0, 3},
    {"systemd-libs", "252", "Systemd client libraries", "", "LGPLv2.1", 2097152, (const char*[]){"glibc", 0}, 1, 1, 0, 0, 0, 0, 1},
    {"dbus", "1.14.6", "Message bus system for IPC", "", "GPLv2", 1572864, (const char*[]){"glibc", 0}, 1, 1, 0, 0, 0, 0, 1},
    {"pam", "1.5.2", "Pluggable Authentication Modules", "", "GPLv2", 1048576, 0, 0, 1, 0, 0, 0, 0, 1},
    {"shadow", "4.13", "Password and user management utilities", "", "BSD", 1572864, (const char*[]){"pam", 0}, 1, 1, 0, 0, 0, 0, 1},
    {"sudo", "1.9.13", "Execute commands as superuser", "", "ISC", 786432, (const char*[]){"pam", 0}, 1, 1, 0, 0, 0, 0, 1},
    {"polkit", "122", "Authorization framework", "", "GPLv2", 1310720, (const char*[]){"glibc", "dbus", 0}, 2, 1, 0, 0, 0, 0, 1},
    {"e2fsprogs", "1.46.5", "Ext2/3/4 filesystem utilities", "", "GPLv2", 2097152, 0, 0, 1, 0, 0, 0, 0, 1},
    {"util-linux", "2.38.1", "Miscellaneous system utilities (mount, fdisk, kill)", "", "GPLv2", 3145728, 0, 0, 1, 0, 0, 0, 0, 1},
    {"procps-ng", "4.0.2", "Process monitoring tools (ps, top, free, uptime)", "", "GPLv2", 1310720, (const char*[]){"ncurses", 0}, 1, 1, 0, 0, 0, 0, 1},
    {"psmisc", "23.6", "Miscellaneous process tools (killall, pstree, fuser)", "", "GPLv2", 524288, 0, 0, 1, 0, 0, 0, 0, 1},
    {"iproute2", "6.1", "IP networking tools (ip, ss, tc, bridge)", "", "GPLv2", 2097152, 0, 0, 1, 0, 0, 0, 0, 3},
    {"net-tools", "2.10", "Legacy network tools (ifconfig, netstat, route)", "", "GPLv2", 786432, 0, 0, 1, 0, 0, 0, 0, 3},
    {"iptables", "1.8.8", "Packet filtering and NAT firewall", "", "GPLv2", 1572864, 0, 0, 1, 0, 0, 0, 0, 3},
    {"dhcpcd", "9.4.1", "DHCP client daemon", "", "BSD", 524288, 0, 0, 1, 0, 0, 0, 0, 3},
    {"dnsmasq", "2.89", "DNS forwarder and DHCP server", "", "GPLv2", 786432, 0, 0, 1, 0, 0, 0, 0, 3},
    {"cronie", "1.6.1", "Periodic command scheduler (cron)", "", "GPLv2", 393216, 0, 0, 1, 0, 0, 0, 0, 1},
    {"logrotate", "3.21.0", "Rotate and compress system log files", "", "GPLv2", 196608, 0, 0, 1, 0, 0, 0, 0, 1},
    {"man-db", "2.11.2", "Manual page browser and database", "", "GPLv2", 1310720, (const char*[]){"pcre2", 0}, 1, 1, 0, 0, 0, 0, 6},
    {"acpid", "2.0.34", "ACPI power management event daemon", "", "GPLv2", 131072, 0, 0, 1, 0, 0, 0, 0, 1},
    {"pciutils", "3.9.0", "PCI bus utilities (lspci, setpci)", "", "GPLv2", 262144, 0, 0, 1, 0, 0, 0, 0, 1},
    {"usbutils", "15.0", "USB device utilities (lsusb)", "", "GPLv2", 196608, 0, 0, 1, 0, 0, 0, 0, 1},
    {"ca-certificates", "20230311", "CA certificate bundle for TLS/SSL", "", "GPLv2", 524288, 0, 0, 1, 0, 0, 0, 0, 3},
    {"pacman", "6.0.2", "CodeOS package manager CLI (CCP client)", "", "GPLv2", 655360, 0, 0, 1, (const char*[]){"apt", 0}, 1, 0, 0, 1},
    {"grub", "2.06", "Grand Unified Bootloader", "", "GPLv3", 3145728, 0, 0, 2, 0, 0, 0, 0, 1},
    {"limine", "4.20230221", "Modern bootloader for PVH/bare-metal", "", "BSD", 2097152, 0, 0, 2, 0, 0, 0, 0, 1},
    {"kbd", "2.5.1", "Keyboard utilities (loadkeys, setfont)", "", "GPLv2", 524288, 0, 0, 1, 0, 0, 0, 0, 1},
    {"which", "2.21", "Locate a command in PATH", "", "GPLv3", 65536, 0, 0, 1, 0, 0, 0, 0, 6},
    {"time", "1.9", "Measure command execution time", "", "GPLv3", 98304, 0, 0, 1, 0, 0, 0, 0, 6},
    {"tree", "2.2.0", "Display directory trees in a compact format", "", "GPLv2", 98304, 0, 0, 0, 0, 0, 0, 0, 6},
    {"lsof", "4.95.0", "List open files and the processes that opened them", "", "ISC", 262144, 0, 0, 0, 0, 0, 0, 0, 6},
    {"strace", "6.0", "Trace system calls and signals of processes", "", "LGPLv2.1", 524288, 0, 0, 1, 0, 0, 0, 0, 2},
    {"netcat", "0.7.2", "TCP/UDP swiss-army network tool (nc)", "", "MIT", 98304, 0, 0, 0, 0, 0, 0, 0, 3},
    {"socat", "1.7.4.4", "Bidirectional data relay between two endpoints", "", "GPLv2", 524288, 0, 0, 0, 0, 0, 0, 0, 3},
    {"nmap", "7.94", "Network exploration and security scanner", "https://nmap.org", "GPLv2", 2097152, 0, 0, 0, 0, 0, 0, 0, 8},
    {"whois", "5.5.16", "Client for the whois domain registration service", "", "GPLv2", 131072, 0, 0, 0, 0, 0, 0, 0, 3},
    {"aria2", "1.36.0", "Multi-protocol download utility with resume support", "https://aria2.github.io", "GPLv2", 1966080, 0, 0, 0, 0, 0, 0, 0, 3},
    {"colordiff", "1.0.21", "Color-highlighted diff output wrapper", "", "GPLv2", 65536, 0, 0, 0, 0, 0, 0, 0, 6},
    {"xterm", "379", "X Window System terminal emulator", "", "MIT", 786432, 0, 0, 0, 0, 0, 0, 0, 6},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

const pkg_repo_t pkg_repo_extra[] = {
    {"nano", "2.9.8", "Tiny friendly text editor", "", "GPLv3", 248576, 0, 0, 0, (const char*[]){"vim", 0}, 1, 0, 0, 6},
    {"vim", "8.2", "Advanced text editor", "", "Vim", 2097152, 0, 0, 0, (const char*[]){"nano", 0}, 1, 0, 0, 6},
    {"curl", "7.85.0", "Data transfer tool (HTTP/FTP/etc)", "", "MIT", 786432, 0, 0, 0, 0, 0, 0, 0, 4},
    {"wget", "1.21.2", "File downloader", "", "GPLv3", 921600, 0, 0, 0, 0, 0, 0, 0, 4},
    {"less", "590", "Terminal file pager", "", "GPLv3", 147456, 0, 0, 0, 0, 0, 0, 0, 6},
    {"openssh", "9.0", "Secure shell client/server", "", "BSD", 1572864, (const char*[]){"zlib", 0}, 1, 0, 0, 0, 0, 0, 4},
    {"telnet", "0.17", "Telnet client", "", "BSD", 114688, 0, 0, 0, 0, 0, 0, 0, 4},
    {"rsync", "3.2.5", "File synchronization tool", "", "GPLv3", 819200, (const char*[]){"zlib", 0}, 1, 0, 0, 0, 0, 0, 6},
    {"screen", "4.9.0", "Terminal multiplexer", "", "GPLv2", 524288, 0, 0, 0, 0, 0, 0, 0, 6},
    {"htop", "3.2.1", "Interactive process viewer", "", "GPLv2", 262144, 0, 0, 0, 0, 0, 0, 0, 6},

    /* ── Gaming / STEAM ── */
    {"steam-runtime", "1.0.0", "STEAM gaming platform runtime for CodeOS", "http://steam.codeos", "Proprietary", 41943040, 0, 0, 0, 0, 0, 0, 0, 7},
    {"steam",         "1.0.0.77", "STEAM gaming client — browse and launch games", "https://steampowered.com", "Proprietary", 209715200, (const char*[]){"steam-runtime", "mesa", 0}, 2, 0, 0, 0, 0, 0, 7},
    {"opendoom", "1.0.0", "Open-source Doom engine port with vanilla + enhanced modes", "", "GPLv2", 6291456, 0, 0, 0, 0, 0, 0, 0, 7},
    {"quake", "1.0.0", "Classic Quake engine port (id Tech 2)", "", "GPLv2", 5242880, 0, 0, 0, 0, 0, 0, 0, 7},
    {"cave-story", "1.0.0", "Classic platformer Doukutsu Monogatari engine port", "", "Freeware", 20971520, 0, 0, 0, 0, 0, 0, 0, 7},
    {"sdl2-app", "2.26.0", "SDL2 runtime and example games platform", "", "Zlib", 5242880, 0, 0, 0, 0, 0, 0, 0, 7},
    {"zircon-pong", "0.1.0", "Classic Pong game for Zircon console", "", "MIT", 65536, 0, 0, 0, 0, 0, 0, 0, 7},
    {"zircon-snake", "0.1.0", "Snake game for Zircon console", "", "MIT", 65536, 0, 0, 0, 0, 0, 0, 0, 7},
    {"zircon-tetris", "0.1.0", "Tetris game for Zircon console", "", "MIT", 98304, 0, 0, 0, 0, 0, 0, 0, 7},
    {"zircon-minesweeper", "0.1.0", "Minesweeper game for Zircon console", "", "MIT", 98304, 0, 0, 0, 0, 0, 0, 0, 7},
    {"zircon-invaders", "0.1.0", "Space Invaders clone for Zircon console", "", "MIT", 114688, 0, 0, 0, 0, 0, 0, 0, 7},
    {"lgame-pong", "2.0.0", "Classic Pong built with LGame API v2 (2D game framework)", "https://codeos.dev/lgame", "MIT", 139264, 0, 0, 0, 0, 0, 0, 0, 7},
    {"lgame-snake", "2.0.0", "Snake built with LGame API v2 (2D game framework)", "https://codeos.dev/lgame", "MIT", 131072, 0, 0, 0, 0, 0, 0, 0, 7},
    {"freecode-games", "1.0.0", "Collection of FreeCode mini-games (tetris, breakout, minesweeper)", "", "MIT", 3145728, 0, 0, 0, 0, 0, 0, 0, 7},
    {"codeos-steam-link", "1.0.0", "Stream games from your STEAM desktop PC via network", "", "Proprietary", 2097152, (const char*[]){"steam-runtime", 0}, 1, 0, 0, 0, 0, 0, 7},

    /* ── Utils / network / multimedia ── */
    {"iftop", "1.0pre4", "Display bandwidth usage on an interface by host", "", "GPLv2", 131072, (const char*[]){"ncurses", 0}, 1, 0, 0, 0, 0, 0, 3},
    {"ncdu", "1.18", "NCurses disk usage analyzer", "", "MIT", 131072, (const char*[]){"ncurses", 0}, 1, 0, 0, 0, 0, 0, 6},
    {"ranger", "1.9.3", "File manager with Vim-like keybindings in the terminal", "", "GPLv3", 589824, (const char*[]){"python", 0}, 1, 0, 0, 0, 0, 0, 6},
    {"audacity", "3.2.4", "Free multi-track audio editor and recorder", "https://audacityteam.org", "GPLv3", 26214400, (const char*[]){"ffmpeg", 0}, 1, 0, 0, 0, 0, 0, 4},
    {"transmission", "4.0.0", "Lightweight BitTorrent client", "https://transmissionbt.com", "GPLv2", 5242880, 0, 0, 0, 0, 0, 0, 0, 3},
    {"feh", "3.9.1", "Lightweight image viewer and wallpaper setter", "", "MIT", 262144, 0, 0, 0, 0, 0, 0, 0, 4},
    {"cheese", "42.1", "Take photos and record videos with your webcam", "", "GPLv2", 5242880, (const char*[]){"gstreamer", 0}, 1, 0, 0, 0, 0, 0, 4},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

const pkg_repo_t pkg_repo_dev[] = {
    {"gcc", "12.2.0", "GNU C/C++ compiler", "", "GPLv3", 52428800, (const char*[]){"binutils", "glibc", 0}, 2, 0, 0, 0, 0, 0, 2},
    {"binutils", "2.39", "GNU binary utilities (ld, as, nm, objdump)", "", "GPLv3", 3932160, 0, 0, 1, 0, 0, 0, 0, 2},
    {"make", "4.3", "Build automation tool", "", "GPLv3", 524288, 0, 0, 1, 0, 0, 0, 0, 2},
    {"cmake", "3.24.0", "Cross-platform build system", "", "BSD", 8388608, (const char*[]){"make", 0}, 1, 0, 0, 0, 0, 0, 2},
    {"gdb", "12.1", "GNU debugger", "", "GPLv3", 3145728, 0, 0, 0, 0, 0, 0, 0, 2},
    {"git", "2.38.0", "Distributed version control system", "", "GPLv2", 5242880, (const char*[]){"curl", 0}, 1, 0, 0, 0, 0, 0, 2},
    {"python", "3.10.8", "Python 3 interpreter", "", "PSF", 10485760, 0, 0, 0, 0, 0, 0, 0, 2},
    {"ruby", "3.1.2", "Ruby programming language", "", "Ruby", 8126464, 0, 0, 0, 0, 0, 0, 0, 2},
    {"node", "18.7.0", "JavaScript runtime (Node.js)", "", "MIT", 35651584, 0, 0, 0, 0, 0, 0, 0, 2},
    {"golang", "1.19", "Go programming language compiler/tools", "", "BSD", 62914560, 0, 0, 0, 0, 0, 0, 0, 2},
    {"rustc", "1.70.0", "Rust compiler", "", "MIT/Apache2", 73400320, 0, 0, 0, 0, 0, 0, 0, 2},
    {"perl", "5.36.0", "Perl programming language", "", "GPLv2", 8388608, 0, 0, 0, 0, 0, 0, 0, 2},
    {"lua", "5.4.6", "Lua scripting language", "", "MIT", 524288, 0, 0, 0, 0, 0, 0, 0, 2},
    {"openssl", "3.0.5", "Cryptography and TLS toolkit", "", "Apache2.0", 2621440, (const char*[]){"zlib", 0}, 1, 0, 0, 0, 0, 0, 3},
    {"zip", "3.0", "ZIP compression utility", "", "BSD", 294912, (const char*[]){"zlib", 0}, 1, 0, 0, 0, 0, 0, 6},
    {"unzip", "6.0", "ZIP extraction utility", "", "BSD", 262144, 0, 0, 0, 0, 0, 0, 0, 6},
    {"tar", "1.24", "Tape archive utility", "", "GPLv3", 524288, 0, 0, 0, 0, 0, 0, 0, 6},
    {"gzip", "1.12", "GNU compression (gzip/gunzip)", "", "GPLv3", 128400, 0, 0, 0, 0, 0, 0, 0, 6},
    {"bzip2", "1.0.8", "Bzip2 compression", "", "BSD", 147456, 0, 0, 0, 0, 0, 0, 0, 6},
    {"xz", "5.2.6", "XZ compression (xz/unxz)", "", "GPLv2", 229376, 0, 0, 0, 0, 0, 0, 0, 6},
    {"file", "5.42", "File type identification utility", "", "BSD", 458752, 0, 0, 0, 0, 0, 0, 0, 6},
    {"diffutils", "3.8", "File comparison utilities (diff, cmp)", "", "GPLv3", 327680, 0, 0, 0, 0, 0, 0, 0, 6},
    {"patch", "2.7.6", "Apply diff patches", "", "GPLv2", 196608, 0, 0, 0, 0, 0, 0, 0, 6},
    {"findutils", "4.9.0", "File search utilities (find, xargs)", "", "GPLv3", 393216, 0, 0, 0, 0, 0, 0, 0, 6},
    {"ninja", "1.11.1", "Small build system with a focus on speed", "https://ninja-build.org", "Apache2", 393216, 0, 0, 0, 0, 0, 0, 0, 2},
    {"meson", "1.1.0", "High productivity build system for C/C++", "https://mesonbuild.com", "Apache2", 1572864, (const char*[]){"ninja", "python", 0}, 2, 0, 0, 0, 0, 0, 2},
    {"llvm-libs", "16.0.0", "LLVM runtime and support libraries", "https://llvm.org", "Apache2", 104857600, 0, 0, 0, 0, 0, 0, 0, 2},
    {"clang", "16.0.0", "LLVM-based C/C++ compiler", "https://llvm.org", "Apache2", 73400320, (const char*[]){"llvm-libs", 0}, 1, 0, 0, 0, 0, 0, 2},
    {"valgrind", "3.21.0", "Dynamic analysis: memory leaks, errors, profiling", "https://valgrind.org", "GPLv2", 52428800, 0, 0, 0, 0, 0, 0, 0, 2},
    {"pkg-config", "0.29.2", "Read installed package metadata for compiling", "", "GPLv2", 131072, 0, 0, 0, 0, 0, 0, 0, 2},
    {"doxygen", "1.9.7", "Generate documentation from annotated source code", "https://doxygen.org", "GPLv2", 10485760, 0, 0, 0, 0, 0, 0, 0, 2},
    {"nasm", "2.15.05", "The Netwide Assembler (x86/x86-64)", "https://nasm.us", "BSD", 1572864, 0, 0, 0, 0, 0, 0, 0, 2},
    {"ctags", "6.0.0", "Generates an index of source-code definitions", "", "GPLv2", 262144, 0, 0, 0, 0, 0, 0, 0, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

/* ── CCP — CodeOS Community Packages ── */

static const char *ccp_dep_ffmpeg[] = {"xz", "zlib", 0};
static const char *ccp_dep_imagemagick[] = {"zlib", 0};
static const char *ccp_dep_docker[] = {"openssl", 0};
static const char *ccp_dep_nginx[] = {"zlib", "openssl", 0};
static const char *ccp_dep_apache[] = {"openssl", 0};
static const char *ccp_dep_postgres[] = {"openssl", 0};
static const char *ccp_dep_gimp[] = {"python", 0};
static const char *ccp_dep_kitty[] = {"python", 0};
static const char *ccp_dep_neovim[] = {"python", 0};

const pkg_repo_t pkg_repo_ccp[] = {
    {"gimp",        "2.10.32",  "GNU Image Manipulation Program", "https://gimp.org", "GPLv3", 31457280, ccp_dep_gimp, 1, 0, 0, 0, 0, 0, 4},
    {"vlc",         "3.0.18",   "Versatile media player", "https://videolan.org", "GPLv2", 31457280,    0, 0, 0, 0, 0, (const char*[]){"mpv", "ffmpeg", 0}, 2, 4},
    {"mpv",         "0.35.0",   "Minimal video player", "https://mpv.io", "GPLv2", 10485760,            0, 0, 0, 0, 0, 0, 0, 4},
    {"ffmpeg",      "6.0",      "Multimedia framework (encode/decode/stream)", "https://ffmpeg.org", "GPLv2", 52428800, ccp_dep_ffmpeg, 2, 0, 0, 0, 0, 0, 4},
    {"imagemagick", "7.1.1",    "Image manipulation suite", "https://imagemagick.org", "Apache2", 10485760, ccp_dep_imagemagick, 1, 0, 0, 0, 0, 0, 4},
    {"neofetch",    "7.1.0",    "System info displayed in terminal (logos + stats)", "", "MIT", 196608,  0, 0, 0, 0, 0, 0, 0, 6},
    {"sysfetch",    "1.0.0",    "System info fetcher for CodeOS (fastfetch-style)", "", "MIT", 262144,  0, 0, 0, 0, 0, 0, 0, 6},
    {"tmux",        "3.3a",     "Terminal multiplexer (like screen)", "", "ISC", 524288,                0, 0, 0, 0, 0, (const char*[]){"screen", 0}, 1, 6},
    {"zsh",         "5.9",      "Z shell with advanced features", "https://zsh.org", "MIT", 1572864,    0, 0, 0, 0, 0, 0, 0, 1},
    {"fish",        "3.6.0",    "Friendly interactive shell", "https://fishshell.com", "GPLv2", 5242880, 0, 0, 0, 0, 0, 0, 0, 1},
    {"docker",      "24.0.2",   "Container runtime and image manager", "https://docker.com", "Apache2", 41943040, ccp_dep_docker, 1, 0, 0, 0, 0, 0, 2},
    {"podman",      "4.6.0",    "Daemonless container engine", "https://podman.io", "Apache2", 31457280, 0, 0, 0, 0, 0, (const char*[]){"docker", 0}, 1, 2},
    {"nginx",       "1.25.0",   "High-performance web server", "https://nginx.org", "BSD", 2097152,      ccp_dep_nginx, 2, 0, 0, 0, 0, 0, 4},
    {"apache",      "2.4.57",   "Apache HTTP Server", "https://httpd.apache.org", "Apache2", 5242880,   ccp_dep_apache, 1, 0, 0, 0, 0, 0, 4},
    {"mariadb",     "10.11.2",  "MySQL-compatible RDBMS", "https://mariadb.org", "GPLv2", 20971520,     0, 0, 0, (const char*[]){"postgresql", 0}, 1, 0, 0, 2},
    {"postgresql",  "15.3",     "Advanced relational database", "https://postgresql.org", "PostgreSQL", 31457280, ccp_dep_postgres, 1, 0, (const char*[]){"mariadb", 0}, 1, 0, 0, 2},
    {"redis",       "7.0.11",   "In-memory key-value data store", "https://redis.io", "BSD", 6291456,   0, 0, 0, 0, 0, 0, 0, 2},
    {"sqlite",      "3.42.0",   "Self-contained SQL database engine", "https://sqlite.org", "PublicDomain", 2097152, 0, 0, 0, 0, 0, 0, 0, 3},
    {"mesa",        "23.1.2",   "OpenGL/Vulkan graphics library", "https://mesa3d.org", "MIT", 41943040, 0, 0, 0, 0, 0, 0, 0, 3},
    {"neovim",      "0.9.1",    "Modern extensible Vim-based editor", "https://neovim.io", "Apache2", 14680064, ccp_dep_neovim, 1, 0, (const char*[]){"vim", "emacs", 0}, 2, 0, 0, 6},
    {"emacs",       "28.2",     "Extensible, customizable text editor", "https://gnu.org/emacs", "GPLv3", 41943040, 0, 0, 0, (const char*[]){"vim", "neovim", 0}, 2, 0, 0, 6},
    {"btop",        "1.2.13",   "Resource monitor with GPU and disk stats", "", "Apache2", 262144,       0, 0, 0, 0, 0, 0, 0, 6},
    {"alacritty",   "0.12.1",   "GPU-accelerated terminal emulator", "https://alacritty.org", "Apache2", 8388608, 0, 0, 0, 0, 0, 0, 0, 1},
    {"kitty",       "0.29.0",   "GPU-based terminal emulator", "https://sw.kovidgoyal.net/kitty", "GPLv3", 10485760, ccp_dep_kitty, 1, 0, 0, 0, 0, 0, 1},
    {"patchutils",  "0.4.2",    "Collection of patch manipulation tools", "", "GPLv2", 196608,          0, 0, 0, 0, 0, 0, 0, 2},
    {"tinycc",      "0.9.27",   "Fast self-contained C compiler (tcc)", "https://tinycc.org", "LGPLv2.1", 262144, 0, 0, 0, 0, 0, (const char*[]){"gcc", 0}, 1, 2},
    {"b3sum",       "1.3.3",    "BLAKE3 checksum utility", "", "CC0", 65536,                           0, 0, 0, 0, 0, 0, 0, 6},
    {"fd",          "8.7.0",    "Fast alternative to find (fd)", "", "Apache2", 131072,                 0, 0, 0, 0, 0, 0, 0, 6},
    {"ripgrep",     "13.0.0",   "Line-oriented search tool (rg)", "", "MIT", 196608,                    0, 0, 0, 0, 0, 0, 0, 6},
    {"bat",         "0.23.0",   "Cat clone with syntax highlighting + Git", "", "MIT", 262144,          0, 0, 0, 0, 0, 0, 0, 6},
    {"dust",        "0.8.6",    "More intuitive du (disk usage)", "", "Apache2", 98304,                 0, 0, 0, 0, 0, 0, 0, 6},
    {"duf",         "0.8.1",    "Disk usage/free utility with better output", "", "MIT", 131072,         0, 0, 0, 0, 0, 0, 0, 6},
    {"procs",       "0.14.0",   "Modern ps replacement (process viewer)", "", "MIT", 163840,            0, 0, 0, 0, 0, 0, 0, 6},
    {"bandwhich",   "0.21.0",   "Terminal bandwidth utilization tool", "", "MIT", 114688,               0, 0, 0, 0, 0, 0, 0, 4},
    {"grex",        "1.4.3",    "Generate regex from test cases", "", "MIT", 98304,                     0, 0, 0, 0, 0, 0, 0, 2},
    {"hyperfine",   "1.17.0",   "Command-line benchmarking tool", "", "MIT", 131072,                    0, 0, 0, 0, 0, 0, 0, 2},
    {"hexyl",       "0.13.1",   "Hex viewer with colored output", "", "MIT", 98304,                     0, 0, 0, 0, 0, 0, 0, 6},
    {"jq",          "1.6",      "Lightweight and flexible JSON processor", "https://jqlang.github.io/jq/", "MIT", 524288, 0, 0, 0, 0, 0, 0, 0, 6},
    {"fzf",         "0.44.1",   "General-purpose command-line fuzzy finder", "https://github.com/junegunn/fzf", "MIT", 393216, 0, 0, 0, 0, 0, 0, 0, 6},
    {"zoxide",      "0.9.2",    "Smart cd that learns your most-used directories", "https://github.com/ajeetdsouza/zoxide", "MIT", 262144, 0, 0, 0, 0, 0, 0, 0, 6},
    {"eza",         "0.16.1",   "A modern, maintained replacement for ls", "https://eza.rocks", "MIT", 393216, 0, 0, 0, 0, 0, 0, 0, 6},
    {"tldr",        "3.0.2",    "Collaborative cheatsheets for console commands", "https://tldr.sh", "MIT", 524288, (const char*[]){"curl", 0}, 1, 0, 0, 0, 0, 0, 6},
    {"glow",        "1.5.1",    "Render markdown on the command line", "https://github.com/charmbracelet/glow", "MIT", 786432, 0, 0, 0, 0, 0, 0, 0, 6},
    {"lazygit",     "0.40.2",   "Simple terminal UI for git commands", "https://github.com/jesseduffield/lazygit", "MIT", 1310720, (const char*[]){"git", 0}, 1, 0, 0, 0, 0, 0, 2},
    {"tig",         "2.5.8",    "Text-mode interface for git", "https://jonas.github.io/tig/", "GPLv2", 262144, (const char*[]){"git", "ncurses", 0}, 2, 0, 0, 0, 0, 0, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

/* ── AUR — CodeOS Arch User Repo (not recommended, unstable) ── */

static const char *aur_dep_ffox[] = {"openssl", 0};
static const char *aur_dep_steam[] = {"mesa", 0};
static const char *aur_dep_obsidian[] = {"openssl", 0};
static const char *aur_dep_discord[] = {"openssl", 0};
static const char *aur_dep_libre[] = {"openssl", "gimp", 0};

const pkg_repo_t pkg_repo_aur[] = {
    {"firefox",     "115.0",    "Full-featured web browser (Mozilla)", "https://firefox.com", "MPLv2", 52428800,       aur_dep_ffox, 1, 0, 0, 0, 0, 0, 4},
    {"chromium",    "115.0",    "Open-source web browser (Google)", "https://chromium.org", "BSD", 62914560,            0, 0, 0, 0, 0, 0, 0, 4},
    {"thunderbird", "102.0",    "Email, calendar and news client", "https://thunderbird.net", "MPLv2", 41943040,       0, 0, 0, 0, 0, 0, 0, 3},
    {"libreoffice", "7.5.0",    "Full office suite (writer, calc, impress)", "https://libreoffice.org", "MPLv2", 209715200, aur_dep_libre, 2, 0, 0, 0, 0, 0, 5},
    {"steam",       "1.0.0.77", "Gaming platform and store (Valve)", "https://steampowered.com", "Proprietary", 209715200, aur_dep_steam, 1, 0, 0, 0, 0, 0, 7},
    {"discord",     "0.0.31",   "Chat, voice, and social platform", "https://discord.com", "Proprietary", 83886080,    aur_dep_discord, 1, 0, 0, 0, 0, 0, 3},
    {"spotify",     "1.2.12",   "Music streaming desktop client", "https://spotify.com", "Proprietary", 31457280,      0, 0, 0, 0, 0, 0, 0, 4},
    {"obsidian",    "1.2.0",    "Knowledge base and note-taking app", "https://obsidian.md", "Proprietary", 26214400,  aur_dep_obsidian, 1, 0, 0, 0, 0, 0, 5},
    {"brave",       "1.51.0",   "Privacy-focused web browser", "https://brave.com", "MPLv2", 58880000,                  0, 0, 0, (const char*[]){"chromium", "firefox", 0}, 2, 0, 0, 3},
    {"signal",      "6.15.0",   "Encrypted messaging platform", "https://signal.org", "AGPLv3", 20971520,              0, 0, 0, 0, 0, 0, 0, 3},
    {"krita",       "5.1.5",    "Digital painting and illustration", "https://krita.org", "GPLv3", 52428800,          0, 0, 0, (const char*[]){"gimp", 0}, 1, 0, 0, 4},
    {"blender",     "3.6.0",    "3D creation suite (model, animate, render)", "https://blender.org", "GPLv2", 157286400, 0, 0, 0, 0, 0, 0, 0, 4},
    {"inkscape",    "1.2.2",    "Vector graphics editor (SVG)", "https://inkscape.org", "GPLv3", 62914560,             0, 0, 0, (const char*[]){"gimp", 0}, 1, 0, 0, 4},
    {"code-oss",    "1.80.0",   "Open-source VS Code editor (VSCodium)", "https://vscodium.com", "MIT", 83886080,     0, 0, 0, (const char*[]){"neovim", "emacs", 0}, 2, 0, 0, 2},
    {"minetest",    "5.6.1",    "Minecraft-inspired open-world sandbox game", "https://minetest.net", "LGPL2.1", 20971520, 0, 0, 0, 0, 0, 0, 0, 7},
    {"supertuxkart", "1.4.0",   "Kart racing game featuring Tux and friends", "https://supertuxkart.net", "GPLv3", 10485760, 0, 0, 0, 0, 0, 0, 0, 7},
    {"openttd",     "12.0",     "Open Transport Tycoon Deluxe — business simulation", "https://openttd.org", "GPLv2", 8388608, 0, 0, 0, 0, 0, 0, 0, 7},
    {"openra",      "20230218", "Open-source reimplementation of Westwood RTS engines", "https://openra.net", "GPL", 15728640, 0, 0, 0, 0, 0, 0, 0, 7},
    {"handbrake",   "1.6.1",    "Video transcoder (DVD ripper)", "https://handbrake.fr", "GPLv2", 20971520,           0, 0, 0, 0, 0, 0, 0, 4},
    {"tor",         "0.4.7.16", "Anonymizing overlay network for TCP (onion routing)", "https://torproject.org", "BSD", 26214400, (const char*[]){"openssl", 0}, 1, 0, 0, 0, 0, 0, 8},
    {"openshot",    "3.1.1",    "Award-winning free video editor (OpenShot)", "https://openshot.org", "GPLv3", 157286400, (const char*[]){"ffmpeg", 0}, 1, 0, 0, 0, 0, 0, 4},
    {"keepassxc",   "2.7.6",    "Cross-platform password manager", "https://keepassxc.org", "GPLv3", 62914560, (const char*[]){"openssl", 0}, 1, 0, 0, 0, 0, 0, 8},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

/* ── Android — AOSP open-source apps (category 9) ── */

const pkg_repo_t pkg_repo_android[] = {
    {"android-browser",   "15.0", "AOSP open-source web browser (Chromium-based)", "", "Apache2", 4194304, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-calendar",  "15.0", "AOSP Calendar app with event management", "", "Apache2", 2097152, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-camera",    "15.0", "AOSP Camera app with photo and video capture", "", "Apache2", 3145728, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-contacts",  "15.0", "AOSP Contacts app for managing people and accounts", "", "Apache2", 2097152, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-clock",     "15.0", "AOSP DeskClock — alarm, timer, stopwatch, world clock", "", "Apache2", 1048576, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-dialer",    "15.0", "AOSP Phone/Dialer app for voice calls", "", "Apache2", 2097152, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-gallery",   "15.0", "AOSP Gallery app for photos and videos", "", "Apache2", 3145728, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-keyboard",  "15.0", "AOSP LatinIME — Latin input method (on-screen keyboard)", "", "Apache2", 4194304, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-music",     "15.0", "AOSP Music app for local audio playback", "", "Apache2", 2097152, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-photos",    "15.0", "AOSP PhotoTable — photo viewer and wallpaper picker", "", "Apache2", 1048576, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-messaging", "15.0", "AOSP Messaging app for SMS/MMS", "", "Apache2", 1048576, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-search",    "15.0", "AOSP QuickSearchBox — on-device and web search", "", "Apache2", 2097152, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-settings",  "15.0", "AOSP Settings app — system configuration", "", "Apache2", 4194304, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-launcher",  "15.0", "AOSP Launcher3 — home screen and app drawer", "", "Apache2", 3145728, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-themes",    "15.0", "AOSP ThemePicker — wallpaper and theme selection", "", "Apache2", 1048576, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-storage",   "15.0", "AOSP StorageManager — storage settings and cleanup", "", "Apache2", 524288, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-emergency", "15.0", "AOSP EmergencyInfo — medical info and emergency contacts", "", "Apache2", 524288, 0, 0, 0, 0, 0, 0, 0, 9},
    {"android-wallpaper", "15.0", "AOSP WallpaperCropper — crop and set wallpapers", "", "Apache2", 524288, 0, 0, 0, 0, 0, 0, 0, 9},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

/* ── Init ── */

void pkg_init(void) {
    pkg_count = 0;
    held_count = 0;
    txn_count = 0;
    txn_head = 0;
    fs_mkdir(PKG_DIR);
    fs_mkdir(PKG_CACHE_DIR);
    count_repo_pkgs();
    pkg_init_repos();
    pkg_load_stone_index();
    pkg_load_db();
    pkg_init_groups();
    pkg_seed_payloads();
    kprintf("pkg: CCP ready — %d packages across %d categories\n",
            pkg_core_count + pkg_extra_count + pkg_dev_count + pkg_aur_count + pkg_ccp_count + pkg_android_count,
            PKG_CATEGORY_COUNT);
}

/* ── Install with dependency resolution ── */

static int install_transaction(const char *name, const char *version,
                                const char *desc, uint32_t size,
                                const char *homepage, const char *license, int category,
                                int depth) {
    if (pkg_count >= PKG_MAX_PACKAGES) {
        kprintf("pkg: database full\n");
        return -1;
    }
    int idx = pkg_find_installed(name);
    if (idx >= 0) {
        int cmp = pkg_version_cmp(version, packages[idx].version);
        if (cmp <= 0) return 0;
        pkg_txn_log(name, version, 2, packages[idx].version);
        kprintf("  %*supgrading %s %s -> %s\n", depth*2, "", name, packages[idx].version, version);
        strncpy_safe(packages[idx].version, version, PKG_VERSION_MAX);
        packages[idx].size = size;
        return 0;
    }
    if (strncpy_safe(packages[pkg_count].name, name, PKG_NAME_MAX) < 0 ||
        strncpy_safe(packages[pkg_count].version, version, PKG_VERSION_MAX) < 0 ||
        strncpy_safe(packages[pkg_count].description, desc, PKG_DESC_MAX) < 0) {
        kprintf("pkg: input too long\n");
        return -1;
    }
    packages[pkg_count].size = size;
    packages[pkg_count].installed = 1;
    packages[pkg_count].dep_count = 0;
    packages[pkg_count].conflict_count = 0;
    packages[pkg_count].provides_count = 0;
    packages[pkg_count].priority = 0;
    packages[pkg_count].category = category;
    strncpy_safe(packages[pkg_count].homepage, homepage ? homepage : "", PKG_URL_MAX);
    strncpy_safe(packages[pkg_count].license, license ? license : "", PKG_LICENSE_MAX);
    packages[pkg_count].file_count = 0;
    pkg_txn_log(name, version, 0, NULL);
    pkg_count++;
    pkg_save_db();
    return 0;
}

static int resolve_and_install(const char *name, int *seen, int seen_max, int depth) {
    if (dep_is_satisfied(name)) return 0;
    for (int i = 0; i < seen_max; i++)
        if (seen[i] && strcmp(packages[seen[i]-1].name, name) == 0) { return -1; }
    int ri, pi;
    if (pkg_find_in_repos(name, &ri, &pi) < 0) {
        kprintf("  %*s\u2717 %s (not found in any repo)\n", depth*2, "", name);
        return -1;
    }
    const pkg_repo_t *rp = all_repos[ri];
    if (!rp) return -1;
    const pkg_repo_t *p = &rp[pi];

    kprintf("  %*s\u21b3 %s %s\n", depth*2, "", p->name, p->version);

    for (int d = 0; d < p->dep_count; d++) {
        if (resolve_and_install(p->depends[d], seen, seen_max, depth + 1) < 0)
            return -1;
    }
    int rc = install_transaction(p->name, p->version, p->desc, p->size,
                                 p->homepage, p->license, p->category, depth);
    if (rc == 0 && ri == REMOTE_REPO_IDX) {
        /* Network-discovered packages: pull the .stone into the cache too,
         * so 'fetch fetch' output matches the install record. */
        kprintf("  %*sdownload %s-%s%s...\n", depth*2, "", p->name, p->version, PKG_STONE_EXT);
        pkg_fetch(p->name, PKG_CACHE_DIR);
    }
    return rc;
}

void pkg_install_with_deps(const char *name) {
    kprintf("Resolving dependencies for %s...\n", name);
    int seen[64] = {0};
    if (resolve_and_install(name, seen, 64, 0) < 0) {
        kprintf("pkg: failed to install %s (dependency error)\n", name);
        return;
    }
    kprintf("\u2713 %s installed successfully\n", name);
}

int pkg_install(const char *name, const char *version, const char *desc, uint32_t size) {
    return install_transaction(name, version, desc, size, 0, 0, 0, 0);
}

int pkg_installed_count(void) {
    return pkg_count;
}

/* ── Remove ── */

int pkg_remove(const char *name) {
    int idx = pkg_find_installed(name);
    if (idx < 0) { kprintf("pkg: %s not installed\n", name); return -1; }
    for (int i = 0; i < held_count; i++)
        if (strcmp(held_packages[i], name) == 0) { kprintf("pkg: %s is held — use 'fetch unhold' first\n", name); return -1; }

    /* Check reverse dependencies */
    int has_rdeps = 0;
    for (int i = 0; i < pkg_count; i++) {
        if (i == idx) continue;
        for (int j = 0; j < packages[i].dep_count; j++)
            if (strcmp(packages[i].depends[j], name) == 0) { has_rdeps = 1; break; }
        if (has_rdeps) break;
    }
    if (has_rdeps) {
        kprintf("pkg: %s is required by other packages — use 'fetch rdepends %s' to see them\n", name, name);
        return -1;
    }

    kprintf("Removing %s...\n", name);
    pkg_txn_log(name, packages[idx].version, 1, NULL);
    for (int j = idx; j < pkg_count - 1; j++) packages[j] = packages[j + 1];
    pkg_count--;
    pkg_save_db();
    kprintf("Removed successfully\n");
    return 0;
}

/* ── Search ── */

int pkg_search(const char *pattern) {
    int matches = 0;
    int total = pkg_core_count + pkg_extra_count + pkg_dev_count + pkg_aur_count +
                pkg_ccp_count + pkg_android_count + remote_count;
    kprintf("Searching %d packages for '%s'...\n", total, pattern);
    kprintf("%-22s %-12s %-6s %s\n", "NAME", "VERSION", "CATEGORY", "DESCRIPTION");
    kprintf("======================================================================\n");

    for (int r = 0; all_repos[r]; r++) {
        for (int i = 0; all_repos[r][i].name; i++) {
            const pkg_repo_t *p = &all_repos[r][i];
            int match = (strstr(p->name, pattern) != 0) ||
                        (strstr(p->desc, pattern) != 0);
            if (!match) {
                char low_name[64], low_pat[64];
                int j;
                for (j = 0; p->name[j] && j < 63; j++)
                    low_name[j] = (p->name[j] >= 'A' && p->name[j] <= 'Z') ? p->name[j] + 32 : p->name[j];
                low_name[j] = 0;
                for (j = 0; pattern[j] && j < 63; j++)
                    low_pat[j] = (pattern[j] >= 'A' && pattern[j] <= 'Z') ? pattern[j] + 32 : pattern[j];
                low_pat[j] = 0;
                match = (strstr(low_name, low_pat) != 0) || (strstr(p->desc, low_pat) != 0);
            }
            if (match) {
                matches++;
                if (r == REMOTE_REPO_IDX) {
                    const char *cn = pkg_category_name(p->category);
                    kprintf("%-22s %-12s %-6s %s\n", p->name, p->version, cn, p->desc);
                    kprintf("                         repo: %s (remote)\n", remote_src[i]);
                } else {
                    const char *cn = pkg_category_name(p->category);
                    kprintf("%-22s %-12s %-6s %s\n", p->name, p->version, cn, p->desc);
                }
            }
        }
    }
    if (matches == 0) { kprintf("No packages found\n"); return -1; }
    kprintf("\nFound %d package(s)\n", matches);
    return 0;
}

/* ── List installed ── */

void pkg_list(void) {
    if (pkg_count == 0) { kprintf("No packages installed\n"); return; }
    kprintf("Installed packages (%d):\n", pkg_count);
    kprintf("%-20s %-12s %-10s %-6s %s\n", "NAME", "VERSION", "SIZE", "CATEGORY", "DESCRIPTION");
    kprintf("=============================================================================\n");

    uint32_t total = 0;
    int by_cat[PKG_CATEGORY_COUNT] = {0};
    for (int i = 0; i < pkg_count; i++) {
        pkg_info_t *p = &packages[i];
        uint32_t sz = p->size;
        const char *cn = pkg_category_name(p->category);
        if (sz >= 1048576)
            kprintf("%-20s %-12s %-3u MB  %-6s %s\n", p->name, p->version, sz/1048576, cn, p->description);
        else if (sz >= 1024)
            kprintf("%-20s %-12s %-3u KB  %-6s %s\n", p->name, p->version, sz/1024, cn, p->description);
        else
            kprintf("%-20s %-12s %-3u B   %-6s %s\n", p->name, p->version, sz, cn, p->description);
        total += sz;
        int c = p->category;
        if (c >= 0 && c < PKG_CATEGORY_COUNT) by_cat[c]++;
    }
    kprintf("=============================================================================\n");
    kprintf("Total: %u bytes (%u MB)\n", total, total / 1048576);
}

/* ── Show package info ── */

int pkg_get_info(const char *name, pkg_info_t *info) {
    int idx = pkg_find_installed(name);
    if (idx < 0) return -1;
    *info = packages[idx];
    return 0;
}

void pkg_show(const char *name) {
    int ri, pi;
    pkg_info_t info;
    if (pkg_get_info(name, &info) == 0) {
        kprintf("         Name: %s\n", info.name);
        kprintf("      Version: %s\n", info.version);
        kprintf("         Size: %u bytes\n", info.size);
        kprintf("     Category: %s\n", pkg_category_name(info.category));
        kprintf("      License: %s\n", info.license[0] ? info.license : "(unknown)");
        kprintf("     Homepage: %s\n", info.homepage[0] ? info.homepage : "(none)");
        kprintf("       Status: Installed\n");
        kprintf("     Priority: ");
        switch (info.priority) {
            case 2: kprintf("Essential\n"); break;
            case 1: kprintf("Important\n"); break;
            case 3: kprintf("Recommended\n"); break;
            case 4: kprintf("Suggested\n"); break;
            default: kprintf("Optional\n"); break;
        }
        if (info.dep_count > 0) {
            kprintf("  Dependencies:");
            for (int i = 0; i < info.dep_count; i++) kprintf(" %s", info.depends[i]);
            kprintf("\n");
        }
        if (info.conflict_count > 0) {
            kprintf("     Conflicts:");
            for (int i = 0; i < info.conflict_count; i++) kprintf(" %s", info.conflicts[i]);
            kprintf("\n");
        }
        kprintf("  Description:\n    %s\n", info.description);
        return;
    }
    if (pkg_find_in_repos(name, &ri, &pi) == 0) {
        const pkg_repo_t *p = &all_repos[ri][pi];
        kprintf("         Name: %s\n", p->name);
        kprintf("      Version: %s\n", p->version);
        kprintf("         Size: %u bytes\n", p->size);
        kprintf("     Category: %s\n", pkg_category_name(p->category));
        kprintf("      License: %s\n", p->license ? p->license : "(unknown)");
        kprintf("     Homepage: %s\n", p->homepage ? p->homepage : "(none)");
        if (ri == REMOTE_REPO_IDX)
            kprintf("         Repo: %s (remote)\n", remote_src[pi]);
        else
            kprintf("         Repo: %s\n", pkg_tab_names[ri]);
        kprintf("       Status: Available in repo\n");
        if (p->dep_count > 0) {
            kprintf("  Dependencies:");
            for (int i = 0; i < p->dep_count; i++) kprintf(" %s", p->depends[i]);
            kprintf("\n");
        }
        if (p->conflict_count > 0) {
            kprintf("     Conflicts:");
            for (int i = 0; i < p->conflict_count; i++) kprintf(" %s", p->conflicts[i]);
            kprintf("\n");
        }
        if (p->provides_count > 0) {
            kprintf("      Provides:");
            for (int i = 0; i < p->provides_count; i++) kprintf(" %s", p->provides[i]);
            kprintf("\n");
        }
        kprintf("  Description:\n    %s\n", p->desc);
        return;
    }
    kprintf("pkg: '%s' not found\n", name);
}

/* ── Upgrade all ── */

void pkg_upgrade_all(void) {
    if (pkg_count == 0) { kprintf("No packages installed\n"); return; }
    kprintf("Checking for upgrades...\n");
    int upgraded = 0;
    for (int i = 0; i < pkg_count; i++) {
        int ri, pi;
        if (pkg_find_in_repos(packages[i].name, &ri, & pi) < 0) continue;
        const pkg_repo_t *p = &all_repos[ri][pi];
        int cmp = pkg_version_cmp(p->version, packages[i].version);
        if (cmp > 0) {
            pkg_txn_log(p->name, p->version, 2, packages[i].version);
            kprintf("  %s: %s -> %s\n", p->name, packages[i].version, p->version);
            strncpy_safe(packages[i].version, p->version, PKG_VERSION_MAX);
            packages[i].size = p->size;
            upgraded++;
        }
    }
    if (upgraded == 0) kprintf("All packages up to date\n");
    else {
        pkg_save_db();
        kprintf("Upgraded %d package(s)\n", upgraded);
    }
}

/* ── Stats ── */

void pkg_stats(void) {
    int total_repo = pkg_core_count + pkg_extra_count + pkg_dev_count + pkg_aur_count +
                     pkg_ccp_count + pkg_android_count + remote_count;
    kprintf("=== CodeOS Package Manager (CCP) ===\n");
    kprintf("  Installed: %d/%d\n", pkg_count, PKG_MAX_PACKAGES);
    uint32_t total_size = 0;
    int by_cat[PKG_CATEGORY_COUNT] = {0};
    for (int i = 0; i < pkg_count; i++) {
        total_size += packages[i].size;
        int c = packages[i].category;
        if (c >= 0 && c < PKG_CATEGORY_COUNT) by_cat[c]++;
    }
    kprintf("  Total size: %u MB (%u bytes)\n", total_size / 1048576, total_size);
    kprintf("  Held packages: %d\n", held_count);
    kprintf("\n  Repository: %d packages available\n", total_repo);
    kprintf("    core:  %d  (essential system)\n", pkg_core_count);
    kprintf("    extra: %d  (common desktop)\n", pkg_extra_count);
    kprintf("    dev:   %d  (development tools)\n", pkg_dev_count);
    kprintf("    aur:   %d  (AUR — not recommended, unstable)\n", pkg_aur_count);
    kprintf("    ccp:   %d  (community)\n", pkg_ccp_count);
    kprintf("    android: %d  (android apps)\n", pkg_android_count);
    if (remote_count > 0)
        kprintf("    remote: %d  (fetched over the network)\n", remote_count);
    kprintf("\n  By category:\n");
    for (int c = 0; c < PKG_CATEGORY_COUNT; c++) {
        if (by_cat[c] > 0)
            kprintf("    %s: %d installed\n", pkg_category_name(c), by_cat[c]);
    }
    kprintf("\n  Use 'fetch search <term>' to find packages\n");
    kprintf("  Use 'fetch install <pkg>' to install\n");
}

/* ── Dependency tree ── */

void pkg_dep_tree(const char *name, int depth) {
    int ri, pi;
    if (pkg_find_in_repos(name, &ri, &pi) < 0) {
        if (depth == 0) kprintf("pkg: '%s' not found\n", name);
        return;
    }
    const pkg_repo_t *p = &all_repos[ri][pi];
    int installed = (pkg_find_installed(name) >= 0);
    for (int i = 0; i < depth; i++) kprintf("  ");
    if (depth > 0) kprintf("\u2514\u2500");
    kprintf("%s %s%s\n", p->name, p->version, installed ? " [installed]" : "");
    for (int d = 0; d < p->dep_count; d++)
        pkg_dep_tree(p->depends[d], depth + 1);
}

/* ── Reverse dependencies ── */

void pkg_rdepends(const char *name) {
    int found = 0;
    for (int i = 0; i < pkg_count; i++) {
        for (int j = 0; j < packages[i].dep_count; j++) {
            if (strcmp(packages[i].depends[j], name) == 0) {
                if (!found) { kprintf("Packages depending on %s:\n", name); found = 1; }
                kprintf("  %s\n", packages[i].name);
                break;
            }
        }
    }
    /* Also check repo packages */
    for (int r = 0; all_repos[r]; r++) {
        for (int i = 0; all_repos[r][i].name; i++) {
            const pkg_repo_t *p = &all_repos[r][i];
            if (pkg_find_installed(p->name) >= 0) continue;
            for (int d = 0; d < p->dep_count; d++) {
                if (strcmp(p->depends[d], name) == 0) {
                    if (!found) { kprintf("Packages depending on %s:\n", name); found = 1; }
                    kprintf("  %s (available)\n", p->name);
                    break;
                }
            }
        }
    }
    if (!found) kprintf("No packages depend on %s\n", name);
}

/* ── Orphans ── */

void pkg_orphans(void) {
    int found = 0;
    kprintf("Checking for orphaned packages (no reverse dependencies)...\n");
    for (int i = 0; i < pkg_count; i++) {
        if (packages[i].priority >= 2) continue;
        int is_held = 0;
        for (int j = 0; j < held_count; j++)
            if (strcmp(held_packages[j], packages[i].name) == 0) { is_held = 1; break; }
        if (is_held) continue;
        int has_rdeps = 0;
        for (int j = 0; j < pkg_count; j++) {
            if (i == j) continue;
            for (int k = 0; k < packages[j].dep_count; k++)
                if (strcmp(packages[j].depends[k], packages[i].name) == 0) { has_rdeps = 1; break; }
            if (has_rdeps) break;
        }
        if (!has_rdeps) {
            if (!found) kprintf("Orphaned packages:\n");
            kprintf("  %s (%s)\n", packages[i].name, packages[i].version);
            found++;
        }
    }
    if (found == 0) kprintf("No orphaned packages found\n");
    else kprintf("\nUse 'fetch remove <pkg>' to remove orphans\n");
}

/* ── Autoremove ── */

void pkg_autoremove(void) {
    int removed = 0;
    kprintf("Removing orphaned packages...\n");
    int i = 0;
    while (i < pkg_count) {
        if (packages[i].priority >= 2) { i++; continue; }
        int is_held = 0;
        for (int j = 0; j < held_count; j++)
            if (strcmp(held_packages[j], packages[i].name) == 0) { is_held = 1; break; }
        if (is_held) { i++; continue; }
        int has_rdeps = 0;
        for (int j = 0; j < pkg_count; j++) {
            if (i == j) continue;
            for (int k = 0; k < packages[j].dep_count; k++)
                if (strcmp(packages[j].depends[k], packages[i].name) == 0) { has_rdeps = 1; break; }
            if (has_rdeps) break;
        }
        if (!has_rdeps) {
            kprintf("  Removed %s\n", packages[i].name);
            for (int j = i; j < pkg_count - 1; j++) packages[j] = packages[j + 1];
            pkg_count--;
            removed++;
        } else { i++; }
    }
    if (removed == 0) kprintf("No orphaned packages to remove\n");
    else {
        pkg_save_db();
        kprintf("Removed %d orphaned package(s)\n", removed);
    }
}

/* ── Hold / Unhold / Held ── */

int pkg_hold(const char *name) {
    if (!name) return -1;
    if (pkg_find_installed(name) < 0) { kprintf("pkg: %s not installed\n", name); return -1; }
    if (held_count >= 32) { kprintf("pkg: hold list full\n"); return -1; }
    for (int i = 0; i < held_count; i++)
        if (strcmp(held_packages[i], name) == 0) { kprintf("pkg: %s already held\n", name); return -1; }
    if (strncpy_safe(held_packages[held_count], name, PKG_NAME_MAX) < 0) return -1;
    held_count++;
    kprintf("Holding %s\n", name);
    return 0;
}

int pkg_unhold(const char *name) {
    for (int i = 0; i < held_count; i++) {
        if (strcmp(held_packages[i], name) == 0) {
            for (int j = i; j < held_count - 1; j++) strcpy(held_packages[j], held_packages[j + 1]);
            held_count--;
            kprintf("Unholding %s\n", name);
            return 0;
        }
    }
    kprintf("pkg: %s not held\n", name);
    return -1;
}

void pkg_held_list(void) {
    if (held_count == 0) { kprintf("No packages held\n"); return; }
    kprintf("Held packages:\n");
    for (int i = 0; i < held_count; i++) kprintf("  %s\n", held_packages[i]);
}

/* ── Update cache (moss: repo update) ── */

void pkg_repo_update(void) {
    int repos_active = 0;
    kprintf("Fetching repo metadata (stone.index)...\n");
    remote_count = 0;
    memset(remote_repo, 0, sizeof(remote_repo));
    for (int i = 0; i < repo_count; i++) {
        if (!repositories[i].enabled) continue;
        repos_active++;
        if (strncmp(repositories[i].url, "http://", 7) == 0 ||
            strncmp(repositories[i].url, "https://", 8) == 0) {
            int n = pkg_fetch_repo_index(&repositories[i]);
            kprintf("  [%d/%d] %s %s ... %s\n",
                    repos_active, repo_count,
                    repositories[i].name, repositories[i].url,
                    n > 0 ? "ok" : "FAILED");
        } else {
            /* file:// (or unknown scheme) repos use the bundled static
               tables: nothing to fetch. */
            kprintf("  [%d/%d] %s %s ... ok (static table)\n",
                    repos_active, repo_count,
                    repositories[i].name, repositories[i].url);
        }
    }
    pkg_metadata_ts = pkg_now();
    pkg_cache_updated = 1;
    int indexed = pkg_write_stone_index();
    pkg_save_repo_config();
    kprintf("Repo metadata refreshed — %d packages indexed from %d repo(s), ts=%u\n",
            indexed, repos_active, pkg_metadata_ts);
}

void pkg_update(void) {
    pkg_repo_update();
}

/* ── Sync (moss: sync [-u]) ── */

void pkg_sync(int update) {
    if (update) {
        pkg_repo_update();
    } else if (!pkg_cache_updated) {
        kprintf("pkg: repo metadata not refreshed\n");
        kprintf("  Run 'fetch sync -u' or 'fetch repo update' first\n");
        return;
    }
    kprintf("Syncing system state...\n");
    pkg_upgrade_all();
}

/* ── Fetch (moss: fetch <pkg> [--output-dir <dir>]) ── */

/* Join dir + "/" + rel-basename, tolerating a trailing '/' on dir. */
static void pkg_join_path(char *dst, int dst_max, const char *dir, const char *rel) {
    int dl = (int)strlen(dir);
    if (dl > 0 && dir[dl - 1] == '/') dl--;
    if (dl < 0) dl = 0;
    if (dl + 1 + (int)strlen(rel) >= dst_max) dl = dst_max - 2 - (int)strlen(rel);
    if (dl < 0) dl = 0;
    memcpy(dst, dir, (size_t)dl);
    int o = dl;
    if (dl <= 0 || dst[o - 1] != '/') dst[o++] = '/';
    strlcpy(dst + o, rel, (size_t)dst_max - (size_t)o);
}



void pkg_fetch(const char *name, const char *out_dir) {
    if (!name || !name[0]) return;
    int ri, pi;
    if (pkg_find_in_repos(name, &ri, &pi) < 0) {
        kprintf("fetch: '%s' not found in any repo\n", name);
        kprintf("  Run 'fetch repo update' to refresh metadata, then retry\n");
        return;
    }
    const pkg_repo_t *p = &all_repos[ri][pi];

    char dir[FS_PATH_MAX];
    if (out_dir && out_dir[0]) {
        strncpy_safe(dir, out_dir, sizeof(dir));
    } else {
        fs_getcwd(dir, sizeof(dir));
        if (dir[0] == 0) strncpy_safe(dir, PKG_CACHE_DIR, sizeof(dir));
    }
    fs_mkdir(dir);

    char path[FS_PATH_MAX];
    pkg_join_path(path, sizeof(path), dir, "");
    snprintf(path + (int)strlen(path), sizeof(path) - (int)strlen(path), "%s-%s%s", p->name, p->version, PKG_STONE_EXT);

    /* Packages discovered over the network (stone.index) are downloaded
     * from their source repo. Static-table packages get a synthetic stone
     * built from the bundled metadata. */
    if (ri == REMOTE_REPO_IDX) {
        const char *src_repo = remote_src[pi];
        const char *repo_url = NULL;
        for (int i = 0; i < repo_count; i++)
            if (strcmp(repositories[i].name, src_repo) == 0) { repo_url = repositories[i].url; break; }
        if (!repo_url || (strncmp(repo_url, "http://", 7) != 0 &&
                          strncmp(repo_url, "https://", 8) != 0)) {
            kprintf("fetch: '%s' source repo '%s' has no http(s) URL\n", name, src_repo);
            return;
        }
        int use_tls = (strncmp(repo_url, "https://", 8) == 0);
        url_t u;
        if (url_parse(repo_url, &u) < 0) {
            kprintf("fetch: bad repo URL '%s'\n", repo_url);
            return;
        }
        if (use_tls && u.port == 80) u.port = 443;

        char dl_path[576];
        int plen = (int)strlen(u.path);
        if (plen == 0 || strcmp(u.path, "/") == 0)
            snprintf(dl_path, sizeof(dl_path), "/%s-%s%s", p->name, p->version, PKG_STONE_EXT);
        else if (u.path[plen - 1] == '/')
            snprintf(dl_path, sizeof(dl_path), "%s%s-%s%s", u.path, p->name, p->version, PKG_STONE_EXT);
        else
            snprintf(dl_path, sizeof(dl_path), "%s/%s-%s%s", u.path, p->name, p->version, PKG_STONE_EXT);

        char stone[FS_CONTENT_MAX];
        int n = use_tls ? https_get(u.host, u.port, dl_path, stone, sizeof(stone) - 1)
                        : http_get(u.host, u.port, dl_path, stone, sizeof(stone) - 1);
        if (n <= 0) {
            kprintf("fetch: download of '%s' FAILED\n", dl_path);
            return;
        }
        if (fs_mkfile(path) < 0) { kprintf("fetch: cannot create '%s'\n", path); return; }
        int w = fs_write(path, stone, n);
        if (w < 0) { kprintf("fetch: failed to write '%s'\n", path); return; }
        kprintf("fetched %s-%s%s (%d bytes) -> %s\n",
                p->name, p->version, PKG_STONE_EXT, n, path);
        pkg_fetch_payload(name, dir);
        return;
    }

    /* Only fetch from enabled repos */
    const char *tabname = pkg_tab_names[ri];
    int enabled = 0;
    for (int i = 0; i < repo_count; i++) {
        if (strcmp(repositories[i].name, tabname) == 0) {
            enabled = repositories[i].enabled;
            break;
        }
    }
    if (!enabled) {
        kprintf("fetch: '%s' is in repo '%s' which is not enabled\n", name, tabname);
        kprintf("  Enable it with 'fetch repo enable %s'\n", tabname);
        return;
    }

    char stone[FS_CONTENT_MAX];
    int n = sprintf(stone,
        "stone-index: 1\n"
        "name: %s\n"
        "version: %s\n"
        "size: %u\n"
        "repo: %s\n"
        "license: %s\n"
        "homepage: %s\n"
        "description: %s\n"
        "fetched: %u\n",
        p->name, p->version, p->size,
        tabname,
        p->license ? p->license : "",
        p->homepage ? p->homepage : "",
        p->desc ? p->desc : "",
        pkg_now());
    if (n >= (int)sizeof(stone)) n = (int)sizeof(stone) - 1;

    if (fs_mkfile(path) < 0) { kprintf("fetch: cannot create '%s'\n", path); return; }
    int w = fs_write(path, stone, n);
    if (w < 0) { kprintf("fetch: failed to write '%s'\n", path); return; }

    kprintf("fetched %s-%s%s (%u bytes) -> %s\n",
            p->name, p->version, PKG_STONE_EXT, p->size, path);

    /* Also materialize the .ftech payload so download-only fetches give
     * an installable artifact (binary, base64 or synthetic). */
    pkg_fetch_payload(name, dir);
}

/* ── .ftech payload materialization ── */

/* Base64 encode a whole file into another file. Returns encoded byte count. */
int pkg_base64_encode_file(const char *in, const char *out) {
    static unsigned char enc[FS_CONTENT_MAX + 65536]; /* holds ~1.33x input */
    static char buf[FS_CONTENT_MAX];
    int n = fs_read(in, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    size_t olen = 0;
    if (mbedtls_base64_encode(enc, sizeof(enc), &olen,
                              (const unsigned char *)buf, (size_t)n) != 0)
        return -1;
    enc[olen] = 0;
    if (fs_mkfile(out) < 0) return -1;
    if (fs_write(out, (const char *)enc, (int)olen) < 0) return -1;
    return (int)olen;
}

/* Base64 decode a whole file (whitespace ignored) into another binary file.
 * Returns decoded byte count. */
int pkg_base64_decode_file(const char *b64file, const char *out) {
    static unsigned char dec[FS_CONTENT_MAX];
    static char txt[FS_CONTENT_MAX + 96];
    int n = fs_read(b64file, txt, sizeof(txt) - 1);
    if (n <= 0) return -1;
    txt[n] = 0;
    int w = 0;
    for (int i = 0; i < n; i++) {
        char c = txt[i];
        if (c != ' ' && c != '\r' && c != '\n' && c != '\t')
            txt[w++] = c;
    }
    txt[w] = 0;
    if (w == 0) return -1;
    size_t olen = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec), &olen,
                              (const unsigned char *)txt, (size_t)w) != 0)
        return -1;
    if (fs_mkfile(out) < 0) return -1;
    if (fs_write(out, (const char *)dec, (int)olen) < 0) return -1;
    return (int)olen;
}

/* Materialize <name>-<version>.ftech for a package in <dir>.
 * Order: network binary download (remote repos) → base64 payload shipped in
 * PKG_PAYLOAD_DIR (offline) → synthetic text manifest (installable via -If).
 * Returns payload bytes written, or 0/-1. */
int pkg_fetch_payload(const char *name, const char *out_dir) {
    if (!name || !name[0]) return -1;
    int ri = -1, pi = -1;
    if (pkg_find_in_repos(name, &ri, &pi) < 0) {
        kprintf("fetch: '%s' not found in any repo\n", name);
        return -1;
    }
    const pkg_repo_t *p = &all_repos[ri][pi];

    char dir[FS_PATH_MAX];
    if (out_dir && out_dir[0]) {
        strncpy_safe(dir, out_dir, sizeof(dir));
    } else {
        fs_getcwd(dir, sizeof(dir));
        if (dir[0] == 0) strncpy_safe(dir, PKG_CACHE_DIR, sizeof(dir));
    }
    fs_mkdir(dir);

    char path[FS_PATH_MAX];
    pkg_join_path(path, sizeof(path), dir, "");
    snprintf(path + (int)strlen(path), sizeof(path) - (int)strlen(path), "%s-%s%s", p->name, p->version, PKG_FTECH_EXT);

    /* Remote packages: download the binary .ftech next to the .stone. */
    if (ri == REMOTE_REPO_IDX) {
        const char *src_repo = remote_src[pi];
        if (!src_repo || !src_repo[0]) src_repo = "remote";
        const char *repo_url = NULL;
        for (int i = 0; i < repo_count; i++)
            if (strcmp(repositories[i].name, src_repo) == 0) { repo_url = repositories[i].url; break; }
        if (!repo_url || (strncmp(repo_url, "http://", 7) != 0 &&
                          strncmp(repo_url, "https://", 8) != 0)) {
            kprintf("fetch: '%s' source repo '%s' has no http(s) URL\n", name, src_repo);
            return -1;
        }
        int use_tls = (strncmp(repo_url, "https://", 8) == 0);
        url_t u;
        if (url_parse(repo_url, &u) < 0) {
            kprintf("fetch: bad repo URL '%s'\n", repo_url);
            return -1;
        }
        if (use_tls && u.port == 80) u.port = 443;

        char dl_path[576];
        int plen = (int)strlen(u.path);
        if (plen == 0 || strcmp(u.path, "/") == 0)
            snprintf(dl_path, sizeof(dl_path), "/%s-%s%s", p->name, p->version, PKG_FTECH_EXT);
        else if (u.path[plen - 1] == '/')
            snprintf(dl_path, sizeof(dl_path), "%s%s-%s%s", u.path, p->name, p->version, PKG_FTECH_EXT);
        else
            snprintf(dl_path, sizeof(dl_path), "%s/%s-%s%s", u.path, p->name, p->version, PKG_FTECH_EXT);

        char payload[FS_CONTENT_MAX];
        int n = use_tls ? https_get(u.host, u.port, dl_path, payload, sizeof(payload) - 1)
                        : http_get(u.host, u.port, dl_path, payload, sizeof(payload) - 1);
        if (n <= 0) {
            kprintf("fetch: remote '%s' payload not published (HTTP GET %s failed)\n", name, dl_path);
            return -1;
        }
        if (fs_mkfile(path) < 0 || fs_write(path, payload, n) < 0) {
            kprintf("fetch: cannot write '%s'\n", path);
            return -1;
        }
        kprintf("fetched %s-%s%s (%d bytes) -> %s\n",
                p->name, p->version, PKG_FTECH_EXT, n, path);
        return n;
    }

    /* Offline: base64 payload shipped in the payloads dir (or user-placed). */
    {
        char b64_path[FS_PATH_MAX];
        snprintf(b64_path, sizeof(b64_path), "%s/%s-%s%s.b64",
                 PKG_PAYLOAD_DIR, p->name, p->version, PKG_FTECH_EXT);
        int dn = pkg_base64_decode_file(b64_path, path);
        if (dn > 0) {
            kprintf("fetched %s-%s%s (%d bytes, base64) -> %s\n",
                    p->name, p->version, PKG_FTECH_EXT, dn, path);
            return dn;
        }
    }

    /* Synthetic manifest payload (NAME:/VERSION:/file: lines, -If compatible). */
    {
        char body[FS_CONTENT_MAX];
        int n = sprintf(body,
                        "NAME:%s\n"
                        "VERSION:%s\n"
                        "DESC:%s\n"
                        "SIZE:%u\n"
                        "repo:%s\n",
                        p->name, p->version,
                        p->desc ? p->desc : "",
                        p->size,
                        pkg_tab_names[ri] ? pkg_tab_names[ri] : "repo");
        if (n >= (int)sizeof(body)) n = (int)sizeof(body) - 1;
        if (fs_mkfile(path) < 0 || fs_write(path, body, n) < 0) {
            kprintf("fetch: cannot write '%s'\n", path);
            return -1;
        }
        kprintf("fetched %s-%s%s (%d bytes, synthetic) -> %s\n",
                p->name, p->version, PKG_FTECH_EXT, n, path);
        return n;
    }
}

/* Seed one base64-encoded .ftech payload so offline installs demo instantly. */
static void pkg_seed_payloads(void) {
    fs_mkdir(PKG_PAYLOAD_DIR);
    int isdir = 0;
    if (fs_resolve(PKG_PAYLOAD_DIR, &isdir) < 0) return;

    const char *raw =
        "NAME:zircon-pong\n"
        "VERSION:0.1.0\n"
        "DESC:Classic Pong game for Zircon console\n"
        "SIZE:89\n"
        "file:/usr/share/zircon-pong-README:Zircon Pong — official CodeOS CCP payload demo.\n";
    int raw_len = (int)strlen(raw);
    static unsigned char enc[FS_CONTENT_MAX + 16384];
    size_t olen = 0;
    if (mbedtls_base64_encode(enc, sizeof(enc), &olen,
                              (const unsigned char *)raw, (size_t)raw_len) != 0)
        return;
    enc[olen] = 0;

    char out[FS_PATH_MAX];
    snprintf(out, sizeof(out), "%s/zircon-pong-0.1.0.ftech.b64", PKG_PAYLOAD_DIR);
    if (fs_mkfile(out) < 0) return; /* already seeded */
    if (fs_write(out, (const char *)enc, (int)olen) < 0)
        kprintf("pkg: failed to seed base64 payload\n");
    kprintf("pkg: seeded base64 .ftech payload for zircon-pong (%d bytes b64)\n", (int)olen);
}

/* ── Conflicts ── */

int pkg_check_conflicts(const char *name) {
    int ri, pi;
    if (pkg_find_in_repos(name, &ri, &pi) < 0) return 0;
    const pkg_repo_t *p = &all_repos[ri][pi];
    for (int i = 0; i < p->conflict_count; i++) {
        if (pkg_find_installed(p->conflicts[i]) >= 0) {
            kprintf("pkg: %s conflicts with installed %s\n", name, p->conflicts[i]);
            return -1;
        }
    }
    for (int i = 0; i < pkg_count; i++) {
        for (int j = 0; j < packages[i].conflict_count; j++) {
            if (strcmp(packages[i].conflicts[j], name) == 0) {
                kprintf("pkg: %s conflicts with installed %s\n", name, packages[i].name);
                return -1;
            }
        }
    }
    return 0;
}

int pkg_check_deps(const char *name) {
    int ri, pi;
    if (pkg_find_in_repos(name, &ri, &pi) < 0) return 0;
    const pkg_repo_t *p = &all_repos[ri][pi];
    for (int i = 0; i < p->dep_count; i++) {
        if (!dep_is_satisfied(p->depends[i])) {
            kprintf("pkg: missing dependency %s for %s\n", p->depends[i], name);
            return -1;
        }
    }
    return 0;
}

/* ── Clean cache ── */

void pkg_clean(void) {
    kprintf("Cleaning package cache at %s...\n", PKG_CACHE_DIR);
    char names[PKG_MAX_FILES][FS_NAME_MAX];
    int count = fs_listdir(PKG_CACHE_DIR, names, PKG_MAX_FILES);
    int removed = 0;
    for (int i = 0; i < count; i++) {
        char path[FS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", PKG_CACHE_DIR, names[i]);
        if (fs_rm(path) == 0) removed++;
    }
    if (removed == 0) kprintf("  Cache is already empty\n");
    else kprintf("  Removed %d cached file(s)\n", removed);
    pkg_cache_updated = 0;
    pkg_metadata_ts = 0;
    kprintf("Cache cleaned\n");
}

/* ── Repository management ── */

void pkg_init_repos(void) {
    repo_count = 0;
    strcpy(repositories[repo_count].name, "core");
    strcpy(repositories[repo_count].url, "file:///.pkg/core");
    repositories[repo_count].enabled = 1;
    repo_count++;

    strcpy(repositories[repo_count].name, "extra");
    strcpy(repositories[repo_count].url, "file:///.pkg/extra");
    repositories[repo_count].enabled = 1;
    repo_count++;

    strcpy(repositories[repo_count].name, "dev");
    strcpy(repositories[repo_count].url, "file:///.pkg/dev");
    repositories[repo_count].enabled = 1;
    repo_count++;

    strcpy(repositories[repo_count].name, "aur");
    strcpy(repositories[repo_count].url, "https://aur.codeos.dev/rpc");
    repositories[repo_count].enabled = 1;
    repo_count++;

    strcpy(repositories[repo_count].name, "ccp");
    strcpy(repositories[repo_count].url, "https://ccp.codeos.dev/rpc");
    repositories[repo_count].enabled = 1;
    repo_count++;

    /* Restore persisted repo config from a previous session, if any */
    pkg_load_repo_config();
}

void pkg_list_repos(void) {
    kprintf("Configured repositories:\n");
    kprintf("%-15s %-30s %-10s\n", "NAME", "URL", "STATUS");
    kprintf("========================================================\n");
    for (int i = 0; i < repo_count; i++)
        kprintf("%-15s %-30s %-10s\n", repositories[i].name, repositories[i].url,
                repositories[i].enabled ? "enabled" : "disabled");
    int total = pkg_core_count + pkg_extra_count + pkg_dev_count + pkg_aur_count +
                pkg_ccp_count + pkg_android_count + remote_count;
    kprintf("\nTotal: %d repos, %d packages (%d remote)\n", repo_count, total, remote_count);
}

void pkg_add_repo(const char *name, const char *url) {
    if (!name || !url) return;
    if (repo_count >= PKG_MAX_REPOS) { kprintf("pkg: repo list full\n"); return; }
    for (int i = 0; i < repo_count; i++)
        if (strcmp(repositories[i].name, name) == 0) { kprintf("pkg: repo '%s' exists\n", name); return; }
    if (strncpy_safe(repositories[repo_count].name, name, PKG_REPO_NAME_MAX) < 0 ||
        strncpy_safe(repositories[repo_count].url, url, 128) < 0) { kprintf("pkg: input too long\n"); return; }
    repositories[repo_count].enabled = 1;
    repo_count++;
    pkg_save_repo_config();
    kprintf("Added repo '%s'\n", name);
}

void pkg_remove_repo(const char *name) {
    for (int i = 0; i < repo_count; i++) {
        if (strcmp(repositories[i].name, name) == 0) {
            for (int j = i; j < repo_count - 1; j++) repositories[j] = repositories[j + 1];
            repo_count--;
            pkg_save_repo_config();
            kprintf("Removed repo '%s'\n", name);
            return;
        }
    }
    kprintf("pkg: repo '%s' not found\n", name);
}

void pkg_enable_repo(const char *name) {
    for (int i = 0; i < repo_count; i++)
        if (strcmp(repositories[i].name, name) == 0) {
            repositories[i].enabled = 1;
            pkg_save_repo_config();
            kprintf("Enabled '%s'\n", name);
            return;
        }
    kprintf("pkg: '%s' not found\n", name);
}

void pkg_disable_repo(const char *name) {
    for (int i = 0; i < repo_count; i++)
        if (strcmp(repositories[i].name, name) == 0) {
            repositories[i].enabled = 0;
            pkg_save_repo_config();
            kprintf("Disabled '%s'\n", name);
            return;
        }
    kprintf("pkg: '%s' not found\n", name);
}

int pkg_repo_count(void) { return repo_count; }

const char *pkg_repo_url(int idx) {
    if (idx < 0 || idx >= repo_count) return NULL;
    return repositories[idx].url;
}

const char *pkg_repo_name(int idx) {
    if (idx < 0 || idx >= repo_count) return NULL;
    return repositories[idx].name;
}

int pkg_find_in_repos(const char *name, int *repo_idx, int *pkg_idx) {
    if (!name) return -1;
    for (int r = 0; all_repos[r]; r++) {
        for (int i = 0; all_repos[r][i].name; i++) {
            if (strcmp(all_repos[r][i].name, name) == 0) {
                if (repo_idx) *repo_idx = r;
                if (pkg_idx)  *pkg_idx = i;
                return 0;
            }
        }
    }
    return -1;
}

/* ── Transaction log ── */

void pkg_txn_log(const char *name, const char *version, int action, const char *old_version) {
    if (txn_count >= PKG_MAX_TXN) {
        /* Ring buffer: overwrite oldest */
        txn_head = (txn_head + 1) % PKG_MAX_TXN;
    } else {
        txn_count++;
    }
    int idx = (txn_head + txn_count - 1) % PKG_MAX_TXN;
    txn_log[idx].timestamp = 0; /* would use timer if available */
    txn_log[idx].action = action;
    strncpy_safe(txn_log[idx].name, name, PKG_NAME_MAX);
    strncpy_safe(txn_log[idx].version, version, PKG_VERSION_MAX);
    txn_log[idx].old_version[0] = 0;
    if (old_version) strncpy_safe(txn_log[idx].old_version, old_version, PKG_VERSION_MAX);
}

void pkg_txn_show(void) {
    if (txn_count == 0) { kprintf("No transactions recorded\n"); return; }
    kprintf("Transaction log (%d entries):\n", txn_count);
    kprintf("%-6s %-24s %-10s %s\n", "IDX", "PACKAGE", "ACTION", "VERSION");
    kprintf("====================================================\n");
    for (int i = 0; i < txn_count; i++) {
        int idx = (txn_head + i) % PKG_MAX_TXN;
        const char *act = txn_log[idx].action == 0 ? "install" :
                          txn_log[idx].action == 1 ? "remove" : "upgrade";
        if (txn_log[idx].action == 2)
            kprintf("%-6d %-24s %-10s %s -> %s\n", i, txn_log[idx].name, act,
                    txn_log[idx].old_version, txn_log[idx].version);
        else
            kprintf("%-6d %-24s %-10s %s\n", i, txn_log[idx].name, act, txn_log[idx].version);
    }
}

int pkg_rollback(int steps) {
    if (steps <= 0 || steps > txn_count) {
        kprintf("pkg: can rollback 1..%d transactions\n", txn_count);
        return -1;
    }
    kprintf("Rolling back %d transaction(s)...\n", steps);
    int undone = 0;
    for (int s = steps - 1; s >= 0; s--) {
        int idx = (txn_head + txn_count - 1 - s) % PKG_MAX_TXN;
        pkg_txn_t *t = &txn_log[idx];
        if (t->action == 0) {
            /* Undo install → remove */
            int pi = pkg_find_installed(t->name);
            if (pi >= 0) {
                for (int j = pi; j < pkg_count - 1; j++) packages[j] = packages[j + 1];
                pkg_count--;
                kprintf("  Removed %s\n", t->name);
                undone++;
            }
        } else if (t->action == 1) {
            /* Undo remove → reinstall */
            int ri, pi;
            if (pkg_find_in_repos(t->name, &ri, &pi) == 0) {
                const pkg_repo_t *rp = all_repos[ri];
                const pkg_repo_t *p = &rp[pi];
                install_transaction(p->name, p->version, p->desc, p->size,
                                    p->homepage, p->license, p->category, 0);
                kprintf("  Reinstalled %s %s\n", p->name, p->version);
                undone++;
            }
        } else if (t->action == 2) {
            /* Undo upgrade → downgrade */
            int pi = pkg_find_installed(t->name);
            if (pi >= 0) {
                strncpy_safe(packages[pi].version, t->old_version, PKG_VERSION_MAX);
                kprintf("  Downgraded %s to %s\n", t->name, t->old_version);
                undone++;
            }
        }
    }
    /* Remove rolled-back transactions */
    txn_count -= steps;
    if (txn_count < 0) txn_count = 0;
    kprintf("Rolled back %d transaction(s)\n", undone);
    return 0;
}

/* ── pkg_why: show why a package is installed ── */

void pkg_why(const char *name) {
    int idx = pkg_find_installed(name);
    if (idx < 0) { kprintf("pkg: %s is not installed\n", name); return; }

    /* Check if it was explicitly installed (not pulled as a dependency) */
    int is_dep = 0;
    int parent_idx = -1;
    for (int i = 0; i < pkg_count; i++) {
        if (i == idx) continue;
        for (int j = 0; j < packages[i].dep_count; j++) {
            if (strcmp(packages[i].depends[j], name) == 0) {
                is_dep = 1;
                parent_idx = i;
                break;
            }
        }
        if (is_dep) break;
    }

    /* Also check provides */
    if (!is_dep) {
        for (int i = 0; i < pkg_count; i++) {
            if (i == idx) continue;
            for (int j = 0; j < packages[i].dep_count; j++) {
                for (int k = 0; k < packages[i].provides_count; k++) {
                    if (strcmp(packages[i].depends[j], packages[i].provides[k]) == 0 &&
                        strcmp(name, packages[i].provides[k]) == 0) {
                        is_dep = 1;
                        parent_idx = i;
                        break;
                    }
                }
                if (is_dep) break;
            }
            if (is_dep) break;
        }
    }

    if (is_dep && parent_idx >= 0) {
        kprintf("%s was installed as a dependency of %s\n", name, packages[parent_idx].name);
        kprintf("  Dependency chain:\n");
        /* Walk up the chain */
        const char *cur = packages[parent_idx].name;
        int depth = 1;
        while (cur && depth < 10) {
            for (int i = 0; i < depth; i++) kprintf("    ");
            kprintf("-> %s\n", cur);
            int found_parent = 0;
            for (int i = 0; i < pkg_count; i++) {
                if (strcmp(packages[i].name, cur) == 0) continue;
                for (int j = 0; j < packages[i].dep_count; j++) {
                    if (strcmp(packages[i].depends[j], cur) == 0) {
                        cur = packages[i].name;
                        found_parent = 1;
                        break;
                    }
                }
                if (found_parent) break;
            }
            if (!found_parent) break;
            depth++;
        }
    } else {
        kprintf("%s was explicitly installed\n", name);
    }
}

/* ── pkg_outdated: list packages with newer versions ── */

void pkg_outdated(void) {
    if (pkg_count == 0) { kprintf("No packages installed\n"); return; }
    int outdated = 0;
    kprintf("%-22s %-12s %-12s %s\n", "NAME", "INSTALLED", "AVAILABLE", "DESCRIPTION");
    kprintf("======================================================================\n");
    for (int i = 0; i < pkg_count; i++) {
        int ri, pi;
        if (pkg_find_in_repos(packages[i].name, &ri, &pi) < 0) continue;
        const pkg_repo_t *p = &all_repos[ri][pi];
        int cmp = pkg_version_cmp(p->version, packages[i].version);
        if (cmp > 0) {
            kprintf("%-22s %-12s %-12s %s\n", packages[i].name, packages[i].version, p->version, p->desc);
            outdated++;
        }
    }
    if (outdated == 0) kprintf("All packages up to date\n");
    else kprintf("\n%d package(s) outdated\n", outdated);
}

/* ── pkg_check: integrity verification ── */

int pkg_check_integrity(const char *name) {
    int idx = pkg_find_installed(name);
    if (idx < 0) { kprintf("pkg: %s not installed\n", name); return -1; }

    /* Simulate integrity check — in a real system this would verify checksums */
    /* For now, check that the package has a name, version, and size > 0 */
    int ok = 1;
    if (packages[idx].name[0] == 0) { kprintf("  %s: missing name\n", name); ok = 0; }
    if (packages[idx].version[0] == 0) { kprintf("  %s: missing version\n", name); ok = 0; }
    if (packages[idx].size == 0) { kprintf("  %s: zero size\n", name); ok = 0; }

    /* Check file count consistency */
    if (packages[idx].file_count < 0 || packages[idx].file_count > PKG_MAX_FILES) {
        kprintf("  %s: invalid file count (%d)\n", name, packages[idx].file_count);
        ok = 0;
    }

    /* Check dependency references are valid */
    for (int d = 0; d < packages[idx].dep_count; d++) {
        int ri2, pi2;
        if (pkg_find_in_repos(packages[idx].depends[d], &ri2, &pi2) < 0) {
            kprintf("  %s: dependency %s not found in any repo\n", name, packages[idx].depends[d]);
            ok = 0;
        }
    }

    if (ok) kprintf("  %s: OK\n", name);
    return ok ? 0 : -1;
}

void pkg_check_all(void) {
    if (pkg_count == 0) { kprintf("No packages installed\n"); return; }
    kprintf("Checking integrity of %d installed packages...\n", pkg_count);
    int failed = 0;
    for (int i = 0; i < pkg_count; i++) {
        if (pkg_check_integrity(packages[i].name) < 0) failed++;
    }
    if (failed == 0) kprintf("All packages OK\n");
    else kprintf("%d package(s) failed integrity check\n", failed);
}

/* ── Package groups ── */

void pkg_group_add(const char *group_name, const char **members, int count) {
    if (!group_name || count <= 0) return;
    if (group_count >= PKG_MAX_GROUPS) { kprintf("pkg: group list full\n"); return; }
    for (int i = 0; i < group_count; i++)
        if (strcmp(groups[i].name, group_name) == 0) { kprintf("pkg: group '%s' exists\n", group_name); return; }
    strncpy_safe(groups[group_count].name, group_name, PKG_GROUP_NAME_MAX);
    groups[group_count].member_count = count;
    if (count > PKG_GROUP_MEMBERS) count = PKG_GROUP_MEMBERS;
    for (int i = 0; i < count; i++)
        strncpy_safe(groups[group_count].members[i], members[i], PKG_NAME_MAX);
    group_count++;
    kprintf("Added group '%s' with %d member(s)\n", group_name, count);
}

int pkg_group_exists(const char *group_name) {
    for (int i = 0; i < group_count; i++)
        if (strcmp(groups[i].name, group_name) == 0) return 1;
    return 0;
}

void pkg_group_install(const char *group_name) {
    int idx = -1;
    for (int i = 0; i < group_count; i++)
        if (strcmp(groups[i].name, group_name) == 0) { idx = i; break; }
    if (idx < 0) { kprintf("pkg: group '%s' not found\n", group_name); return; }

    kprintf("Installing group '%s' (%d packages)...\n", group_name, groups[idx].member_count);
    int installed = 0;
    for (int i = 0; i < groups[idx].member_count; i++) {
        const char *name = groups[idx].members[i];
        if (pkg_find_installed(name) >= 0) {
            kprintf("  %s: already installed\n", name);
            continue;
        }
        int ri, pi;
        if (pkg_find_in_repos(name, &ri, &pi) < 0) {
            kprintf("  %s: not found in any repo\n", name);
            continue;
        }
        const pkg_repo_t *rp = all_repos[ri];
        const pkg_repo_t *p = &rp[pi];
        install_transaction(p->name, p->version, p->desc, p->size,
                                p->homepage, p->license, p->category, 0);
        pkg_txn_log(p->name, p->version, 0, NULL);
        installed++;
    }
    kprintf("Installed %d package(s) from group '%s'\n", installed, group_name);
}

void pkg_group_remove(const char *group_name) {
    int idx = -1;
    for (int i = 0; i < group_count; i++)
        if (strcmp(groups[i].name, group_name) == 0) { idx = i; break; }
    if (idx < 0) { kprintf("pkg: group '%s' not found\n", group_name); return; }

    kprintf("Removing group '%s'...\n", group_name);
    int removed = 0;
    for (int i = 0; i < groups[idx].member_count; i++) {
        const char *name = groups[idx].members[i];
        int pi2 = pkg_find_installed(name);
        if (pi2 < 0) continue;
        /* Check reverse deps from non-group packages */
        int has_rdeps = 0;
        for (int j = 0; j < pkg_count; j++) {
            if (j == pi2) continue;
            /* Skip if the dependent is also in this group */
            int in_group = 0;
            for (int k = 0; k < groups[idx].member_count; k++)
                if (strcmp(packages[j].name, groups[idx].members[k]) == 0) { in_group = 1; break; }
            if (in_group) continue;
            for (int d = 0; d < packages[j].dep_count; d++)
                if (strcmp(packages[j].depends[d], name) == 0) { has_rdeps = 1; break; }
            if (has_rdeps) break;
        }
        if (has_rdeps) {
            kprintf("  %s: required by other packages, skipping\n", name);
            continue;
        }
        pkg_txn_log(name, packages[pi2].version, 1, NULL);
        for (int j = pi2; j < pkg_count - 1; j++) packages[j] = packages[j + 1];
        pkg_count--;
        kprintf("  Removed %s\n", name);
        removed++;
    }
    kprintf("Removed %d package(s) from group '%s'\n", removed, group_name);
}

void pkg_group_list(void) {
    if (group_count == 0) { kprintf("No package groups defined\n"); return; }
    kprintf("Package groups (%d):\n", group_count);
    for (int i = 0; i < group_count; i++) {
        int installed = 0;
        for (int j = 0; j < groups[i].member_count; j++)
            if (pkg_find_installed(groups[i].members[j]) >= 0) installed++;
        kprintf("  %s (%d/%d installed)\n", groups[i].name, installed, groups[i].member_count);
        for (int j = 0; j < groups[i].member_count; j++) {
            int is_inst = pkg_find_installed(groups[i].members[j]) >= 0;
            kprintf("    %s %s\n", groups[i].members[j], is_inst ? "[installed]" : "");
        }
    }
}

/* ── Mirror management ── */

void pkg_add_mirror(const char *repo_name, const char *mirror_url) {
    for (int i = 0; i < repo_count; i++) {
        if (strcmp(repositories[i].name, repo_name) == 0) {
            if (repositories[i].mirror_count >= PKG_MAX_MIRRORS) {
                kprintf("pkg: mirror list full for '%s'\n", repo_name);
                return;
            }
            strncpy_safe(repositories[i].mirrors[repositories[i].mirror_count], mirror_url, 128);
            repositories[i].mirror_count++;
            kprintf("Added mirror for '%s': %s\n", repo_name, mirror_url);
            return;
        }
    }
    kprintf("pkg: repo '%s' not found\n", repo_name);
}

void pkg_list_mirrors(const char *repo_name) {
    for (int i = 0; i < repo_count; i++) {
        if (strcmp(repositories[i].name, repo_name) == 0) {
            kprintf("Mirrors for '%s':\n", repo_name);
            kprintf("  Primary: %s\n", repositories[i].url);
            if (repositories[i].mirror_count == 0) {
                kprintf("  No mirrors configured\n");
            } else {
                for (int j = 0; j < repositories[i].mirror_count; j++)
                    kprintf("  Mirror %d: %s\n", j + 1, repositories[i].mirrors[j]);
            }
            return;
        }
    }
    kprintf("pkg: repo '%s' not found\n", repo_name);
}

/* ── Init groups with common presets ── */

static void pkg_init_groups(void) {
    group_count = 0;

    {
        const char *base[] = {"coreutils", "bash", "grep", "sed", "awk", "glibc", "zlib", 0};
        pkg_group_add("base", base, 7);
    }
    {
        const char *base_devel[] = {"gcc", "binutils", "make", "cmake", "gdb", "git", 0};
        pkg_group_add("base-devel", base_devel, 6);
    }
    {
        const char *networking[] = {"iproute2", "net-tools", "iptables", "dhcpcd", "dnsmasq", "curl", "wget", 0};
        pkg_group_add("networking", networking, 7);
    }
    {
        const char *desktop[] = {"mesa", "alacritty", "htop", "neofetch", 0};
        pkg_group_add("desktop", desktop, 4);
    }
    {
        const char *multimedia[] = {"ffmpeg", "mpv", "vlc", "gimp", 0};
        pkg_group_add("multimedia", multimedia, 4);
    }
    {
        const char *gaming[] = {"steam-runtime", "sdl2-app", 0};
        pkg_group_add("gaming", gaming, 2);
    }
    {
        const char *android_apps[] = {
            "android-browser", "android-calendar", "android-camera",
            "android-contacts", "android-clock", "android-dialer",
            "android-gallery", "android-keyboard", "android-music",
            "android-photos", "android-messaging", "android-search",
            "android-settings", "android-launcher", 0};
        pkg_group_add("android", android_apps, 14);
    }
    {
        const char *security[] = {"openssl", "ca-certificates", "openssh", "polkit", 0};
        pkg_group_add("security", security, 4);
    }
    {
        const char *sys_utils[] = {"htop", "btop", "fd", "ripgrep", "bat", "dust", "duf", "procs", 0};
        pkg_group_add("sysutils", sys_utils, 8);
    }
}

/* ── Pkg network self-test ──
   Runs after pkg_init() (needs fs + network). Uses a temporary repo pointing
   at the host test server (httptest.py on 10.0.2.2:9001). */
void pkg_net_selftest(void) {
    kprintf("PKGTEST: begin\n");
    int repo_was_added = 0;
    for (int i = 0; i < repo_count; i++)
        if (strcmp(repositories[i].name, "test") == 0) repo_was_added = 1;
    if (!repo_was_added) {
        if (repo_count >= PKG_MAX_REPOS) { kprintf("PKGTEST: repo list full\n"); return; }
        strcpy(repositories[repo_count].name, "test");
        strcpy(repositories[repo_count].url, "http://10.0.2.2:9001");
        repositories[repo_count].enabled = 1;
        repositories[repo_count].mirror_count = 0;
        repo_count++;
    }

    pkg_repo_update();

    int found = 0;
    {
        int ri, pi;
        if (pkg_find_in_repos("netfetch-tool", &ri, &pi) == 0) found = 1;
    }
    kprintf("PKGTEST: remote find netfetch-tool=%d (remote_count=%d)\n",
            found, remote_count);

    pkg_fetch("netfetch-tool", PKG_CACHE_DIR);

    /* Verify the downloaded stone is on disk */
    {
        char path[FS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/netfetch-tool-1.2.3.stone", PKG_CACHE_DIR);
        char probe[FS_CONTENT_MAX];
        int len = fs_read(path, probe, sizeof(probe) - 1);
        if (len > 0) {
            probe[len] = 0;
            kprintf("PKGTEST: downloaded stone (%d bytes): %s\n", len, probe);
        } else {
            kprintf("PKGTEST: downloaded stone MISSING (%s)\n", path);
        }
    }

    /* Search should now surface the remote package. */
    {
        int ri, pi;
        int s = pkg_search("netfetch");
        kprintf("PKGTEST: search 'netfetch' rc=%d\n", s);
        if (pkg_find_in_repos("netfetch-tool", &ri, &pi) == 0)
            kprintf("PKGTEST: search-hit netfetch-tool in repo idx %d (remote=%d)\n",
                    ri, ri == REMOTE_REPO_IDX);
    }

    /* Installing a remote package should download its .stone into the cache. */
    {
        int pre = pkg_find_installed("netfetch-lib");
        pkg_install_with_deps("netfetch-lib");
        if (pkg_find_installed("netfetch-lib") >= 0 && pre < 0)
            kprintf("PKGTEST: installed netfetch-lib from remote\n");
        char path[FS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/netfetch-lib-0.9.1.stone", PKG_CACHE_DIR);
        char chk[1];
        if (fs_read(path, chk, 1) > 0)
            kprintf("PKGTEST: netfetch-lib stone cached\n");
        else
            kprintf("PKGTEST: netfetch-lib stone NOT cached\n");
    }
    kprintf("PKGTEST: done\n");
}

/* ── Offline .ftech / base64 payload self-test ──
   Verifies the encode→decode round trip and that pkg_fetch_payload() turns a
   seeded base64 blob into an installable .ftech — all offline. */
void pkg_b64_selftest(void) {
    kprintf("B64TEST: begin\n");

    /* Round trip: write a sample, encode, decode, compare. */
    const char *sample = "CodeOS .ftech base64 payload self-test 1234567890!@#\n";
    int slen = (int)strlen(sample);
    fs_mkfile(PKG_CACHE_DIR "/b64-src");
    fs_write(PKG_CACHE_DIR "/b64-src", sample, slen);
    int enc = pkg_base64_encode_file(PKG_CACHE_DIR "/b64-src", PKG_CACHE_DIR "/b64-enc");
    int dec = pkg_base64_decode_file(PKG_CACHE_DIR "/b64-enc", PKG_CACHE_DIR "/b64-dec");
    char back[256];
    int blen = fs_read(PKG_CACHE_DIR "/b64-dec", back, sizeof(back) - 1);
    int ok = (enc > 0) && (dec == slen) && (blen == slen) &&
             memcmp(sample, back, (size_t)slen) == 0;
    kprintf("B64TEST: round trip enc=%d dec=%d cmp=%d %s\n",
            enc, dec, (blen == slen && memcmp(sample, back, (size_t)slen) == 0) ? 1 : 0,
            ok ? "PASS" : "FAIL");

    /* The seeded zircon-pong payload should decode to the .ftech in cache. */
    pkg_fetch_payload("zircon-pong", PKG_CACHE_DIR);
    {
        char path[FS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/zircon-pong-0.1.0%s", PKG_CACHE_DIR, PKG_FTECH_EXT);
        char probe[FS_CONTENT_MAX];
        int len = fs_read(path, probe, sizeof(probe) - 1);
        if (len > 0) {
            probe[len] = 0;
            kprintf("B64TEST: .ftech decoded (%d bytes):\n%s", len, probe);
            if (strncmp(probe, "NAME:zircon-pong", 16) == 0)
                kprintf("B64TEST: seeded payload content matches PASS\n");
            else
                kprintf("B64TEST: seeded payload content MISMATCH FAIL\n");
        } else {
            kprintf("B64TEST: .ftech MISSING FAIL\n");
        }
    }
    kprintf("B64TEST: done\n");
}

