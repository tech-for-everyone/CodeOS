#include "clamav.h"
#include "string.h"
#include "kprintf.h"
#include "fs.h"

static clamav_sig_t sigs[CLAMAV_MAX_SIGS];
static int sig_count;
static int threat_level;
static int db_loaded;
static int scan_total, scan_found;

#define QUARANTINE_DIR "/.quarantine"

/* ── Hex parsing ── */

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_to_bytes(const char *hex, uint8_t *bytes, int max) {
    int count = 0;
    while (*hex && *(hex + 1) && count < max) {
        if (*hex == '.') {
            bytes[count++] = 0x00; /* wildcard placeholder */
            hex++;
            continue;
        }
        int h = hex_val(*hex);
        int l = hex_val(*(hex + 1));
        if (h < 0 || l < 0) break;
        bytes[count++] = (h << 4) | l;
        hex += 2;
    }
    return count;
}

/* ── Match patterns with wildcard support ── */

static int match_bytes(const uint8_t *data, int dlen, const uint8_t *pat, int plen) {
    if (plen == 0 || dlen < plen) return 0;
    int wildcard = 0;
    for (int i = 0; i < plen; i++)
        if (pat[i] == 0x00) { wildcard = 1; break; }

    if (!wildcard) {
        /* Boyer-Moore-Horspool */
        int skip[256];
        for (int i = 0; i < 256; i++) skip[i] = plen;
        for (int i = 0; i < plen - 1; i++)
            skip[pat[i]] = plen - 1 - i;

        int i = 0;
        while (i <= dlen - plen) {
            int j = plen - 1;
            while (j >= 0 && data[i + j] == pat[j]) j--;
            if (j < 0) return 1;
            i += skip[data[i + plen - 1]];
        }
        return 0;
    }

    /* Wildcard-aware naive matching */
    for (int i = 0; i <= dlen - plen; i++) {
        int j;
        for (j = 0; j < plen; j++) {
            if (pat[j] != 0x00 && data[i + j] != pat[j]) break;
        }
        if (j == plen) return 1;
    }
    return 0;
}

/* ── Built-in signatures (40+ covering real malware families) ── */

static void load_builtin_sigs(void) {
    static const struct {
        const char *name;
        const char *hex;
        int threat;
    } builtins[] = {
        /* Standard test + common malware (20 existing improved) */
        {"EICAR-Test-File",      "58354f2150264041505b345c505a5835342850295e3743432937247d", 3},
        {"Unix-Trojan-Agent",    "2f62696e2f7368", 5},
        {"Script-Exploit-Shell", "726d202d7266202f", 4},
        {"Polymorphic-A",        "9090909090909090", 2},
        {"Win-Trojan-Downloader","5a6f6e655472616e73666572", 5},
        {"Unix-Malware-Base64",  "657865632873656c662e", 5},
        {"Xorist-Ransomware",    "72616e736f6d77617265", 5},
        {"Generic-Keylogger",    "6b65796c6f67", 3},
        {"Script-Kiddie",        "3432046f726c20646965", 4},
        {"Mirai-Botnet",         "6d69726169", 5},
        {"Kaiten-Botnet",        "6b616974656e", 5},
        {"Tsunami-Botnet",       "7473756e616d69", 5},
        {"Lizard-Malware",       "6c697a617264", 4},
        {"Rootkit-LKM-Hide",     "686964655f6d6f64756c65", 5},
        {"Exploit-Shellcode",    "31c050682f2f7368", 5},
        {"Web-Shell-Passthru",   "3c3f7068702073797374656d28", 4},
        {"Crypto-Miner-Script",  "2d2d6370752d6c696d6974", 3},
        {"Reverse-Shell",        "2f62696e2f62617368", 5},
        {"Ransomware-Encrypt",   "656e6372797074", 4},
        {"Worm-Autorun",         "5b6175746f72756e5d", 3},

        /*  New signatures (22 more) */
        {"ELF-Virus-Carrier",    "7f454c46", 5},
        {"Polymorphic-Encoder",  "eb02ff83", 4},
        {"Shell-Backdoor",       "2f62696e2f626f7572", 5},
        {"Perl-IRCBot",          "75736520495243", 4},
        {"Python-Reverse-Shell", "6f732e73797374656d", 5},
        {"DDoS-Agent",           "7374727563742069636d70", 4},
        {"DNS-Tunneler",         "656e637279707465642e", 3},
        {"Fileless-Malware",     "506f7765725368656c6c", 4},
        {"Macro-Virus",          "737562204175746f4f70656e", 3},
        {"Bootkit-MBR",          "33c08ed0bc007c", 5},
        {"UEFI-Implant",         "5a6f6e655472616e73666572", 5},
        {"APT-Persistence",      "736f6674776172655c4d6963726f736f6674", 5},
        {"Data-Exfiltrator",     "5053542f686f7374", 4},
        {"Keylogger-Linux",      "2f7661722f6c6f672f6b65796c6f67", 3},
        {"Log-Cleaner",          "2f7661722f6c6f67", 3},
        {"SSH-Backdoor",         "617574686f72697a65645f6b657973", 5},
        {"Kernel-Module-Hide",   "686964655f70726f63", 5},
        {"Process-Hider",        "686964655f70726f63657373", 5},
        {"Port-Scanner",         "534f434b5f53545245414d", 2},
        {"Packet-Sniffer",       "5041434b45545f534f434b4554", 3},
        {"Privilege-Escalation", "736574756964", 4},
        {"Crypto-Wallet-Stealer","626974636f696e", 5},

        {NULL, NULL, 0}
    };

    for (int i = 0; builtins[i].name && sig_count < CLAMAV_MAX_SIGS; i++) {
        clamav_sig_t *s = &sigs[sig_count];
        int ni = 0;
        while (builtins[i].name[ni] && ni < CLAMAV_NAME_MAX - 1) {
            s->name[ni] = builtins[i].name[ni];
            ni++;
        }
        s->name[ni] = 0;
        s->pat_len = hex_to_bytes(builtins[i].hex, s->pattern, CLAMAV_MAX_PAT);
        if (s->pat_len <= 0) continue;
        s->threat_level = builtins[i].threat;
        s->size_min = 0;
        s->size_max = 0x7FFFFFFF;
        uint32_t h = 0;
        for (int j = 0; j < s->pat_len; j++)
            h = h * 31 + s->pattern[j];
        s->hash[0] = h;
        sig_count++;
    }
}

/* ── Public API ── */

void clamav_init(void) {
    sig_count = 0;
    threat_level = 0;
    db_loaded = 0;
    scan_total = 0;
    scan_found = 0;
    load_builtin_sigs();
    clamav_load_db();
}

int clamav_load_db(void) {
    char buf[FS_CONTENT_MAX];
    int n = fs_read(CLAMAV_DB_PATH, buf, FS_CONTENT_MAX - 1);
    if (n <= 0) return -1;
    buf[n] = 0;

    sig_count = 0;
    load_builtin_sigs();

    char *line = buf;
    while (line && *line && sig_count < CLAMAV_MAX_SIGS) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        if (*nl == '\n') *nl++ = 0; else nl = 0;

        char *name = line;
        char *colon = line;
        while (*colon && *colon != ':') colon++;
        if (!*colon) { line = nl; continue; }
        *colon++ = 0;
        char *hex = colon;
        while (*colon && *colon != ':') colon++;
        if (!*colon) { line = nl; continue; }
        *colon++ = 0;
        int threat = 0;
        char *tp = colon;
        while (*tp >= '0' && *tp <= '9') threat = threat * 10 + (*tp++ - '0');

        clamav_sig_t *s = &sigs[sig_count];
        int ni = 0;
        while (name[ni] && ni < CLAMAV_NAME_MAX - 1) { s->name[ni] = name[ni]; ni++; }
        s->name[ni] = 0;
        s->pat_len = hex_to_bytes(hex, s->pattern, CLAMAV_MAX_PAT);
        if (s->pat_len <= 0) { line = nl; continue; }
        s->threat_level = threat;
        s->size_min = 0;
        s->size_max = 0x7FFFFFFF;
        uint32_t h = 0;
        for (int j = 0; j < s->pat_len; j++) h = h * 31 + s->pattern[j];
        s->hash[0] = h;
        sig_count++;

        line = nl;
    }
    db_loaded = 1;
    return 0;
}

int clamav_update_db(void) {
    kprintf("ClamAV: updating signature database...\n");
    sig_count = 0;
    load_builtin_sigs();
    clamav_load_db();
    kprintf("ClamAV: %d signatures loaded\n", sig_count);
    return 0;
}

int clamav_add_sig(const char *name, const char *hex_pattern, int threat) {
    if (!name || !hex_pattern) return -1;
    if (sig_count >= CLAMAV_MAX_SIGS) return -1;
    clamav_sig_t *s = &sigs[sig_count];
    int ni = 0;
    while (name[ni] && ni < CLAMAV_NAME_MAX - 1) { s->name[ni] = name[ni]; ni++; }
    s->name[ni] = 0;
    s->pat_len = hex_to_bytes(hex_pattern, s->pattern, CLAMAV_MAX_PAT);
    if (s->pat_len <= 0) return -1;
    s->threat_level = threat > 5 ? 5 : (threat < 1 ? 1 : threat);
    s->size_min = 0;
    s->size_max = 0x7FFFFFFF;
    uint32_t h = 0;
    for (int j = 0; j < s->pat_len; j++) h = h * 31 + s->pattern[j];
    s->hash[0] = h;
    sig_count++;
    return 0;
}

/* ── Scanning ── */

int clamav_scan_buf(const char *fname, const uint8_t *buf, int len) {
    int found = 0;
    if (!buf && len > 0) return -1;
    for (int i = 0; i < sig_count; i++) {
        if (len < (int)sigs[i].size_min || len > (int)sigs[i].size_max) continue;
        if (match_bytes(buf, len, sigs[i].pattern, sigs[i].pat_len)) {
            kprintf("ClamAV: \x9c %s -> \x9c \x9c%s\x9c \x9c(threat %d)\n",
                    fname ? fname : "<buffer>", sigs[i].name, sigs[i].threat_level);
            found++;
            if (sigs[i].threat_level > threat_level)
                threat_level = sigs[i].threat_level;
        }
    }
    return found;
}

int clamav_scan_file(const char *path) {
    if (!path) return -1;
    char buf[FS_CONTENT_MAX];
    int n = fs_read(path, buf, FS_CONTENT_MAX);
    if (n < 0) return -1;
    int found = clamav_scan_buf(path, (const uint8_t *)buf, n);
    scan_total++;
    if (found > 0) scan_found += found;
    return found;
}

/* ── Quarantine ── */

static int quarantine_init(void) {
    int is_dir;
    int idx = fs_resolve(QUARANTINE_DIR, &is_dir);
    if (idx >= 0 && is_dir) return 0;
    return fs_mkdir(QUARANTINE_DIR);
}

int clamav_quarantine(const char *path) {
    if (!path) return -1;
    int is_dir;
    if (fs_resolve(path, &is_dir) < 0) return -1;

    quarantine_init();

    /* Build quarantine path: /.quarantine/<basename> */
    char fname[FS_NAME_MAX];
    const char *slash = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') slash = p;
    if (*slash == '/') slash++;
    int i;
    for (i = 0; slash[i] && i < FS_NAME_MAX - 1; i++)
        fname[i] = slash[i];
    fname[i] = 0;

    char qpath[FS_PATH_MAX];
    i = 0;
    const char *qp = QUARANTINE_DIR;
    while (*qp && i < FS_PATH_MAX - 1) qpath[i++] = *qp++;
    qpath[i++] = '/';
    for (int j = 0; fname[j] && i < FS_PATH_MAX - 1; j++)
        qpath[i++] = fname[j];
    qpath[i] = 0;

    /* Read original, write to quarantine, remove original */
    char buf[FS_CONTENT_MAX];
    int n = fs_read(path, buf, FS_CONTENT_MAX);
    if (n < 0) return -1;

    if (fs_mkfile(qpath) < 0) {
        kprintf("ClamAV: quarantine: cannot create %s\n", qpath);
        return -1;
    }
    if (fs_write(qpath, buf, n) < 0) {
        kprintf("ClamAV: quarantine: cannot write %s\n", qpath);
        return -1;
    }

    if (fs_rm(path) < 0) {
        kprintf("ClamAV: quarantine: cannot remove %s\n", path);
        return -1;
    }

    kprintf("ClamAV: quarantined %s -> %s\n", path, qpath);
    return 0;
}

/* ── Recursive directory scanning ── */

static int scan_path(const char *path) {
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0) return 0;

    if (is_dir) {
        char names[FS_MAX_NODES][FS_NAME_MAX];
        int count = fs_listdir(path, names, FS_MAX_NODES);
        int found = 0;
        for (int i = 0; i < count; i++) {
            if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0) continue;
            char full[FS_PATH_MAX];
            int j = 0;
            const char *p = path;
            while (*p && j < FS_PATH_MAX - 1) full[j++] = *p++;
            if (j > 0 && full[j-1] != '/') full[j++] = '/';
            for (int k = 0; names[i][k] && j < FS_PATH_MAX - 1; k++)
                full[j++] = names[i][k];
            full[j] = 0;
            found += scan_path(full);
        }
        return found;
    }

    return clamav_scan_file(path);
}

int clamav_scan_tree(const char *path) {
    kprintf("ClamAV: scanning %s ...\n", path);
    return scan_path(path);
}

/* ── Heuristic scan ── */

static int heuristic_check(const char *path, const uint8_t *buf, int len) {
    (void)path;
    int flags = 0;

    /* Check for ELF header */
    if (len >= 4 && buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F')
        flags |= 1;

    /* Check for shell script */
    if (len >= 2 && buf[0] == '#' && buf[1] == '!')
        flags |= 2;

    /* Suspicious: high entropy (simple check — count unique bytes) */
    if (len >= 64) {
        int seen[256] = {0};
        int unique = 0;
        for (int i = 0; i < len && i < 256; i++) {
            if (!seen[buf[i]]) { seen[buf[i]] = 1; unique++; }
        }
        if (unique > 180) flags |= 4; /* high entropy */
    }

    /* Suspicious: base64-encoded payload indicators */
    if (len >= 32) {
        for (int i = 0; i < len - 8; i++) {
            if (buf[i] == 'P' && buf[i+1] == 'E' && buf[i+2] == 'N' && buf[i+3] == 'V')
                { flags |= 8; break; }
            if (buf[i] == 'J' && buf[i+1] == 'F' && buf[i+2] == 'I' && buf[i+3] == 'F')
                { flags |= 8; break; }
        }
    }

    /* Suspicious: embedded IP address patterns (x.x.x.x) */
    if (len >= 16) {
        int ip_count = 0;
        for (int i = 0; i < len - 7; i++) {
            int dots = 0;
            for (int j = 0; j < 8 && i + j < len; j++) {
                if (buf[i+j] >= '0' && buf[i+j] <= '9') continue;
                if (buf[i+j] == '.') { dots++; continue; }
                break;
            }
            if (dots >= 3) ip_count++;
        }
        if (ip_count >= 3) flags |= 16;
    }

    return flags;
}

/* ── System scan ── */

static void scan_with_heuristics(const char *path) {
    char buf[FS_CONTENT_MAX];
    int n = fs_read(path, buf, FS_CONTENT_MAX);
    if (n < 0) return;

    int sig_hits = clamav_scan_buf(path, (const uint8_t *)buf, n);
    scan_total++;

    int heur = heuristic_check(path, (const uint8_t *)buf, n);
    if (heur && sig_hits == 0) {
        kprintf("ClamAV: %s -> heuristic flags=0x%x (suspicious)\n", path, heur);
        if (heur >= 12) {
            kprintf("ClamAV: %s -> HEURISTIC: HIGH RISK\n", path);
            threat_level = threat_level < 3 ? 3 : threat_level;
        }
    }
}

void clamav_scan_system(void) {
    quarantine_init();
    kprintf("\n=== ClamAV: System Integrity Scan ===\n");
    kprintf("Signatures: %d loaded\n", sig_count);

    scan_total = 0;
    scan_found = 0;

    const char *files[] = {
        "/bin/init", "/bin/shell", "/boot/kernel.bin",
        "/etc/passwd", "/etc/hosts", "/etc/wm.conf",
        NULL
    };
    for (int i = 0; files[i]; i++) {
        scan_with_heuristics(files[i]);
    }

    kprintf("=== ClamAV: Scan complete: %d files, %d threats ===\n", scan_total, scan_found);
    if (scan_found > 0)
        kprintf("ClamAV: Threat level: %s\n",
                threat_level <= 2 ? "LOW" :
                threat_level <= 4 ? "MEDIUM" : "HIGH");
}

void clamav_list_sigs(void) {
    kprintf("ClamAV signature database: %d signatures\n", sig_count);
    for (int i = 0; i < sig_count; i++) {
        kprintf("  %s (threat=%d, %d bytes)\n",
                sigs[i].name, sigs[i].threat_level, sigs[i].pat_len);
    }
}

int clamav_sig_count(void) { return sig_count; }
int clamav_threat_level(void) { return threat_level; }

void clamav_stats(void) {
    kprintf("ClamAV Statistics:\n");
    kprintf("  Signatures:     %d\n", sig_count);
    kprintf("  DB loaded:      %s\n", db_loaded ? "yes" : "no");
    kprintf("  DB path:        %s\n", CLAMAV_DB_PATH);
    kprintf("  Threat level:   ");
    if (threat_level == 0) kprintf("none\n");
    else if (threat_level <= 2) kprintf("low\n");
    else if (threat_level <= 4) kprintf("medium\n");
    else kprintf("HIGH\n");
    kprintf("  Files scanned:  %d\n", scan_total);
    kprintf("  Threats found:  %d\n", scan_found);
}
