#include "security.h"
#include "security_advanced.h"
#include "ad_block.h"
#include "string.h"
#include "kprintf.h"
#include "fs.h"

static sec_audit_t audit_log[SEC_AUDIT_MAX];
static int audit_count;
static sec_integrity_t integrity[SEC_INTEGRITY_MAX];
static int integrity_count;
static int firewall_on;
static char firewall_rules[8][64];
static int firewall_rule_count;
static char perm_paths[16][128];
static int perm_table[16];
static int perm_count;

void sec_init(void) {
    audit_count = 0;
    integrity_count = 0;
    firewall_on = 0;
    firewall_rule_count = 0;
    perm_count = 0;
    sec_advanced_init();
}

void sec_log(const char *msg) {
    if (!msg) return;
    if (audit_count >= SEC_AUDIT_MAX) {
        for (int i = 0; i < SEC_AUDIT_MAX - 1; i++)
            audit_log[i] = audit_log[i + 1];
        audit_count--;
    }
    sec_audit_t *e = &audit_log[audit_count];
    int i = 0;
    while (msg[i] && i < 255) { e->msg[i] = msg[i]; i++; }
    e->msg[i] = 0;
    e->pid = 0;
    e->uid = 0;
    e->time = 0;
    audit_count++;
}

/* FNV-1a hash — proper non-cryptographic hash for file integrity */
static uint32_t hash_file(const char *path) {
    char buf[FS_CONTENT_MAX];
    int n = fs_read(path, buf, FS_CONTENT_MAX);
    if (n <= 0) return 0;
    uint32_t h = 0x811c9dc5u;  /* FNV offset basis */
    for (int i = 0; i < n; i++) {
        h ^= (uint8_t)buf[i];
        h *= 0x01000193u;       /* FNV prime */
    }
    return h;
}

int sec_integrity_add(const char *path) {
    if (!path) return -1;
    if (integrity_count >= SEC_INTEGRITY_MAX) return -1;
    sec_integrity_t *e = &integrity[integrity_count];
    int i = 0;
    while (path[i] && i < 127) { e->path[i] = path[i]; i++; }
    e->path[i] = 0;
    e->hash = hash_file(path);
    e->size = 0;
    e->monitored = 1;
    integrity_count++;
    sec_log("integrity: added monitoring for ");
    sec_log(path);
    return 0;
}

int sec_integrity_check(const char *path) {
    if (!path) return -1;
    for (int i = 0; i < integrity_count; i++) {
        if (strcmp(integrity[i].path, path) == 0) {
            uint32_t cur = hash_file(path);
            if (cur != integrity[i].hash) {
                kprintf("SECURITY: File '%s' has been MODIFIED!\n", path);
                kprintf("  Original hash: 0x%08x, Current: 0x%08x\n",
                        integrity[i].hash, cur);
                sec_log("integrity: file modified ");
                sec_log(path);
                return 1;
            }
            kprintf("SECURITY: '%s' integrity OK (hash 0x%08x)\n", path, cur);
            return 0;
        }
    }
    kprintf("SECURITY: '%s' is not monitored\n", path);
    return -1;
}

void sec_integrity_list(void) {
    kprintf("Monitored files:\n");
    for (int i = 0; i < integrity_count; i++) {
        kprintf("  %s (hash=0x%08x, monitored=%d)\n",
                integrity[i].path, integrity[i].hash, integrity[i].monitored);
    }
    if (integrity_count == 0) kprintf("  (none)\n");
}

void sec_audit_dump(void) {
    kprintf("Security Audit Log (%d entries):\n", audit_count);
    for (int i = 0; i < audit_count; i++) {
        kprintf("  %s\n", audit_log[i].msg);
    }
    if (audit_count == 0) kprintf("  (empty)\n");
}

void sec_set_permissions(const char *path, int mode) {
    if (!path) return;
    if (perm_count >= 16) return;
    int i = 0;
    while (path[i] && i < 127) {
        perm_paths[perm_count][i] = path[i];
        i++;
    }
    perm_paths[perm_count][i] = 0;
    perm_table[perm_count] = mode;
    perm_count++;
    char buf[128];
    int n = 0;
    const char *m = "permissions set on ";
    while (*m && n < 60) buf[n++] = *m++;
    m = path;
    while (*m && n < 126) buf[n++] = *m++;
    buf[n] = 0;
    sec_log(buf);
}

int sec_get_permissions(const char *path) {
    if (!path) return -1;
    for (int i = 0; i < perm_count; i++) {
        if (strcmp(perm_paths[i], path) == 0)
            return perm_table[i];
    }
    return -1;
}

int sec_enforce_firewall(int enable) {
    firewall_on = enable;
    sec_log(enable ? "firewall enabled" : "firewall disabled");
    return 0;
}

int sec_firewall_enabled(void) { return firewall_on; }

void sec_firewall_rule_add(const char *rule) {
    if (!rule) return;
    if (firewall_rule_count >= 8) return;
    int i = 0;
    while (rule[i] && i < 63) { firewall_rules[firewall_rule_count][i] = rule[i]; i++; }
    firewall_rules[firewall_rule_count][i] = 0;
    firewall_rule_count++;
    sec_log("firewall: added rule");
}

int sec_firewall_rule_count(void) { return firewall_rule_count; }

void sec_firewall_rule_get(int index, char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return;
    buf[0] = 0;
    if (index < 0 || index >= firewall_rule_count) return;
    int i = 0;
    while (firewall_rules[index][i] && i < bufsize - 1) {
        buf[i] = firewall_rules[index][i];
        i++;
    }
    buf[i] = 0;
}

void sec_firewall_rule_remove(int index) {
    if (index < 0 || index >= firewall_rule_count) return;
    for (int i = index; i < firewall_rule_count - 1; i++) {
        int j = 0;
        while (firewall_rules[i + 1][j] && j < 63) {
            firewall_rules[i][j] = firewall_rules[i + 1][j];
            j++;
        }
        firewall_rules[i][j] = 0;
    }
    firewall_rule_count--;
    firewall_rules[firewall_rule_count][0] = 0;
    sec_log("firewall: removed rule");
}

void sec_firewall_rule_clear(void) {
    firewall_rule_count = 0;
    for (int i = 0; i < 8; i++) firewall_rules[i][0] = 0;
    sec_log("firewall: cleared all rules");
}

static int parse_ip(const char *s, uint32_t *ip) {
    if (!s || !ip) return -1;
    *ip = 0;
    uint32_t val = 0;
    int octet = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); }
        else if (*s == '.') { *ip = (*ip << 8) | val; val = 0; octet++; }
        else return -1;
        s++;
    }
    *ip = (*ip << 8) | val;
    *ip = __builtin_bswap32(*ip);
    return octet == 3 ? 0 : -1;
}

int sec_firewall_check(uint32_t src_ip, uint32_t dst_ip, uint8_t proto, uint16_t src_port, uint16_t dst_port) {
    (void)src_port;
    if (!firewall_on) return 1;

    for (int i = 0; i < firewall_rule_count; i++) {
        const char *r = firewall_rules[i];

        if (strcmp(r, "deny all") == 0) return 0;
        if (strcmp(r, "allow all") == 0) return 1;

        if (strcmp(r, "deny tcp") == 0 && proto == 6) return 0;
        if (strcmp(r, "allow tcp") == 0 && proto == 6) return 1;
        if (strcmp(r, "deny udp") == 0 && proto == 17) return 0;
        if (strcmp(r, "allow udp") == 0 && proto == 17) return 1;
        if (strcmp(r, "deny icmp") == 0 && proto == 1) return 0;
        if (strcmp(r, "allow icmp") == 0 && proto == 1) return 1;

        /* Per-IP rules: "deny from 10.0.2.15" or "deny to 10.0.2.1 port 80" */
        uint32_t rule_ip = 0;
        uint16_t rule_port = 0;
        if (strncmp(r, "deny from ", 10) == 0 && parse_ip(r + 10, &rule_ip) == 0) {
            if (src_ip == rule_ip) return 0;
        } else if (strncmp(r, "deny to ", 8) == 0 && parse_ip(r + 8, &rule_ip) == 0) {
            if (dst_ip == rule_ip) return 0;
        } else if (strncmp(r, "deny port ", 10) == 0) {
            rule_port = 0;
            const char *pr = r + 10;
            while (*pr >= '0' && *pr <= '9') { rule_port = rule_port * 10 + (*pr - '0'); pr++; }
            if (dst_port == rule_port) return 0;
        } else if (strncmp(r, "allow from ", 11) == 0 && parse_ip(r + 11, &rule_ip) == 0) {
            if (src_ip == rule_ip) return 1;
        }
    }
    return 1;
}

void sec_status(void) {
    kprintf("System Security Status:\n");
    kprintf("  Firewall:       %s\n", firewall_on ? "ENABLED" : "disabled");
    kprintf("  Rules:          %d\n", firewall_rule_count);
    for (int i = 0; i < firewall_rule_count; i++)
        kprintf("    %s\n", firewall_rules[i]);
    kprintf("  Integrity:      %d files monitored\n", integrity_count);
    kprintf("  Audit entries:  %d\n", audit_count);
    kprintf("  Permissions:    %d entries\n", perm_count);
    kprintf("  AdBlock:        %s (%d domains)\n",
            adblock_is_enabled() ? "ON" : "OFF", adblock_domain_count());
}
