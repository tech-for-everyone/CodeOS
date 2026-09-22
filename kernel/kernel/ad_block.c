#include "ad_block.h"
#include "string.h"
#include "kprintf.h"
#include "fs.h"

static char domains[ADBLOCK_MAX_DOMAINS][ADBLOCK_DOMAIN_MAX];
static int domain_count;
static int enabled;
static uint32_t stat_dns_blocked;
static uint32_t stat_http_blocked;

static const char *builtin_domains[] = {
    "doubleclick.net",
    "googlesyndication.com",
    "googleadservices.com",
    "adservice.google.com",
    "pagead2.googlesyndication.com",
    "ads.yahoo.com",
    "advertising.com",
    "adnxs.com",
    "adsrvr.org",
    "taboola.com",
    "outbrain.com",
    "scorecardresearch.com",
    "moatads.com",
    "amazon-adsystem.com",
    "facebook.net",
    "connect.facebook.net",
    "analytics.google.com",
    "hotjar.com",
    "quantserve.com",
    "zedo.com",
    "popads.net",
    "popcash.net",
    "propellerads.com",
    "mgid.com",
    "revcontent.com",
    "criteo.com",
    "rubiconproject.com",
    "openx.net",
    "pubmatic.com",
    "adsafeprotected.com",
    "adroll.com",
    "optimizely.com",
    "crazyegg.com",
    "clickfunnels.com",
    "hubspot.com",
    "marketo.com",
    "pinterest.com",
    "sharethis.com",
    "addthis.com",
    "tribalfusion.com",
    "bluekai.com",
    "exelator.com",
    "lotame.com",
    "demdex.net",
    "krxd.net",
    "casalemedia.com",
    "1rx.io",
    "adform.net",
    "adition.com",
    "adzerk.net",
    "bidswitch.net",
    "contextweb.com",
    "districtm.io",
    "indexww.com",
    "media.net",
    "omnitagjs.com",
    "openbidder.net",
    "pubnative.net",
    "rlcdn.com",
    "sharethrough.com",
    "smaato.net",
    "smartadserver.com",
    "spotx.tv",
    "teads.tv",
    "tremorhub.com",
    "triplelift.com",
    "yieldmo.com",
    0
};

static void domain_to_lower(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        dst[i] = c;
        i++;
    }
    dst[i] = 0;
    while (i > 0 && dst[i - 1] == '.') {
        dst[i - 1] = 0;
        i--;
    }
}

static int domain_exists(const char *name) {
    for (int i = 0; i < domain_count; i++) {
        if (strcmp(domains[i], name) == 0)
            return 1;
    }
    return 0;
}

static int domain_add(const char *name) {
    if (!name || !*name || domain_count >= ADBLOCK_MAX_DOMAINS)
        return -1;

    char norm[ADBLOCK_DOMAIN_MAX];
    domain_to_lower(norm, name, ADBLOCK_DOMAIN_MAX);
    if (!norm[0] || norm[0] == '#')
        return -1;
    if (domain_exists(norm))
        return 0;

    int i = 0;
    while (norm[i] && i < ADBLOCK_DOMAIN_MAX - 1) {
        domains[domain_count][i] = norm[i];
        i++;
    }
    domains[domain_count][i] = 0;
    domain_count++;
    return 0;
}

static void load_builtins(void) {
    for (int i = 0; builtin_domains[i]; i++)
        domain_add(builtin_domains[i]);
}

static void load_file_list(void) {
    char buf[FS_CONTENT_MAX];
    int n = fs_read(ADBLOCK_LIST_PATH, buf, FS_CONTENT_MAX - 1);
    if (n <= 0)
        return;
    buf[n] = 0;

    char line[ADBLOCK_DOMAIN_MAX];
    int li = 0;
    for (int i = 0; i <= n; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r' || c == 0) {
            line[li] = 0;
            if (li > 0 && line[0] != '#')
                domain_add(line);
            li = 0;
            if (c == 0)
                break;
        } else if (li < ADBLOCK_DOMAIN_MAX - 1) {
            line[li++] = c;
        }
    }
}

void adblock_init(void) {
    domain_count = 0;
    enabled = 1;
    stat_dns_blocked = 0;
    stat_http_blocked = 0;
    load_builtins();
    load_file_list();
}

int adblock_enable(void) {
    enabled = 1;
    return 0;
}

int adblock_disable(void) {
    enabled = 0;
    return 0;
}

int adblock_is_enabled(void) {
    return enabled;
}

static int wildcard_match(const char *host, const char *pattern) {
    for (; *pattern; pattern++, host++) {
        if (*pattern == '*') {
            if (*(pattern + 1) == 0)
                return 1;
            while (*host) {
                if (wildcard_match(host, pattern + 1))
                    return 1;
                host++;
            }
            return 0;
        }
        if (*host != *pattern && *pattern != '?')
            return 0;
        if (*host == 0)
            return 0;
    }
    return *host == 0;
}

int adblock_check_host(const char *host) {
    if (!enabled || !host || !*host)
        return 0;

    char norm[ADBLOCK_DOMAIN_MAX];
    domain_to_lower(norm, host, ADBLOCK_DOMAIN_MAX);
    if (!norm[0])
        return 0;

    for (int i = 0; i < domain_count; i++) {
        const char *rule = domains[i];

        if (strchr(rule, '*') || strchr(rule, '?')) {
            if (wildcard_match(norm, rule))
                return 1;
            continue;
        }

        int rlen = (int)strlen(rule);
        int hlen = (int)strlen(norm);

        if (strcmp(norm, rule) == 0)
            return 1;

        if (hlen > rlen && norm[hlen - rlen - 1] == '.' &&
            strcmp(norm + hlen - rlen, rule) == 0)
            return 1;
    }

    return 0;
}

int adblock_reload(void) {
    domain_count = 0;
    load_builtins();
    load_file_list();
    return domain_count;
}

void adblock_record_dns_block(void) {
    stat_dns_blocked++;
}

void adblock_record_http_block(void) {
    stat_http_blocked++;
}

int adblock_domain_count(void) {
    return domain_count;
}

void adblock_stats(void) {
    kprintf("AdBlock Statistics:\n");
    kprintf("  Enabled:        %s\n", enabled ? "yes" : "no");
    kprintf("  Blocklist size: %d domains\n", domain_count);
    kprintf("  DNS blocked:    %u\n", stat_dns_blocked);
    kprintf("  HTTP blocked:   %u\n", stat_http_blocked);
    kprintf("  List path:      %s\n", ADBLOCK_LIST_PATH);
}

void adblock_list(void) {
    kprintf("AdBlock domains (%d):\n", domain_count);
    for (int i = 0; i < domain_count; i++)
        kprintf("  %s\n", domains[i]);
}

void adblock_status(void) {
    kprintf("AdBlock: %s (%d domains, %u DNS + %u HTTP blocked)\n",
            enabled ? "ON" : "OFF", domain_count,
            stat_dns_blocked, stat_http_blocked);
}

int adblock_add_domain(const char *domain) {
    return domain_add(domain);
}

int adblock_remove_domain(const char *domain) {
    if (!domain || !*domain)
        return -1;
    char norm[ADBLOCK_DOMAIN_MAX];
    domain_to_lower(norm, domain, ADBLOCK_DOMAIN_MAX);
    for (int i = 0; i < domain_count; i++) {
        if (strcmp(domains[i], norm) == 0) {
            for (int j = i; j < domain_count - 1; j++)
                strlcpy(domains[j], domains[j + 1], sizeof(domains[j]));
            domain_count--;
            return 0;
        }
    }
    return -1;
}
