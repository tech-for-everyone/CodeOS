#include "android_prop.h"
#include "string.h"
#include "kernel/kprintf.h"

static android_prop_t props[PROP_MAX_ENTRIES];
static int prop_count = 0;
static android_prop_trigger_t triggers[PROP_MAX_TRIGGERS];
static int trigger_count = 0;

/* ─── CRC32 for fast property lookup ─── */
static uint32_t crc32_hash(const char *s) {
    uint32_t crc = 0xFFFFFFFF;
    while (*s) {
        crc ^= (uint8_t)*s++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

/* ─── Init ─── */

int android_prop_init(void) {
    memset(props, 0, sizeof(props));
    memset(triggers, 0, sizeof(triggers));
    prop_count = 0;
    trigger_count = 0;

    /* Load default CodeOS properties */
    android_prop_load_defaults();

    kprintf("[android-prop] initialized with %d properties\n", prop_count);
    return 0;
}

/* ─── Property get/set ─── */

int android_prop_set(const char *name, const char *value) {
    if (!name || !value) return -1;
    if (strlen(name) >= PROP_MAX_NAME) return -1;
    if (strlen(value) >= PROP_MAX_VALUE) return -1;

    uint32_t crc = crc32_hash(name);

    /* Find existing property */
    for (int i = 0; i < prop_count; i++) {
        if (props[i].crc32 == crc && strcmp(props[i].name, name) == 0) {
            if (props[i].readonly) return -1; /* can't change ro.* */

            strlcpy(props[i].value, value, PROP_MAX_VALUE);
            props[i].crc32 = crc32_hash(value);

            /* Fire triggers */
            android_prop_trigger_fire(name, value);
            return 0;
        }
    }

    /* New property */
    if (prop_count >= PROP_MAX_ENTRIES) return -1;

    android_prop_t *p = &props[prop_count];
    strlcpy(p->name, name, PROP_MAX_NAME);
    strlcpy(p->value, value, PROP_MAX_VALUE);
    p->crc32 = crc;
    p->readonly = (strncmp(name, "ro.", 3) == 0) ? 1 : 0;
    p->persist = (strncmp(name, "persist.", 8) == 0) ? 1 : 0;
    prop_count++;

    return 0;
}

int android_prop_get(const char *name, char *value, int max) {
    if (!name || !value || max <= 0) return -1;

    uint32_t crc = crc32_hash(name);
    for (int i = 0; i < prop_count; i++) {
        if (props[i].crc32 == crc && strcmp(props[i].name, name) == 0) {
            strlcpy(value, props[i].value, max);
            return 0;
        }
    }
    value[0] = '\0';
    return -1;
}

int android_prop_get_int(const char *name, int def) {
    char val[PROP_MAX_VALUE];
    if (android_prop_get(name, val, sizeof(val)) < 0) return def;
    int result = 0;
    int neg = 0;
    const char *p = val;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') result = result * 10 + (*p++ - '0');
    return neg ? -result : result;
}

int android_prop_get_bool(const char *name, int def) {
    char val[PROP_MAX_VALUE];
    if (android_prop_get(name, val, sizeof(val)) < 0) return def;
    if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0 ||
        strcmp(val, "yes") == 0 || strcmp(val, "on") == 0) return 1;
    return 0;
}

int android_prop_delete(const char *name) {
    if (!name) return -1;
    uint32_t crc = crc32_hash(name);
    for (int i = 0; i < prop_count; i++) {
        if (props[i].crc32 == crc && strcmp(props[i].name, name) == 0) {
            memmove(&props[i], &props[i + 1],
                    (prop_count - i - 1) * sizeof(android_prop_t));
            prop_count--;
            return 0;
        }
    }
    return -1;
}

/* ─── Property queries ─── */

int android_prop_get_prefix(const char *prefix, char out[][PROP_MAX_NAME], int max) {
    if (!prefix) return 0;
    int count = 0;
    size_t plen = strlen(prefix);
    for (int i = 0; i < prop_count && count < max; i++) {
        if (strncmp(props[i].name, prefix, plen) == 0) {
            strlcpy(out[count], props[i].name, PROP_MAX_NAME);
            count++;
        }
    }
    return count;
}

int android_prop_count(void) { return prop_count; }

int android_prop_list(char names[][PROP_MAX_NAME], int max) {
    int count = 0;
    for (int i = 0; i < prop_count && count < max; i++) {
        strlcpy(names[count], props[i].name, PROP_MAX_NAME);
        count++;
    }
    return count;
}

/* ─── Property triggers ─── */

int android_prop_trigger_add(const char *property, const char *value,
                             const char *action, int exact) {
    if (trigger_count >= PROP_MAX_TRIGGERS) return -1;

    android_prop_trigger_t *t = &triggers[trigger_count];
    strlcpy(t->property, property, PROP_MAX_NAME);
    strlcpy(t->value, value, PROP_MAX_VALUE);
    strlcpy(t->action, action, sizeof(t->action));
    t->exact = exact;
    trigger_count++;
    return trigger_count - 1;
}

int android_prop_trigger_remove(int index) {
    if (index < 0 || index >= trigger_count) return -1;
    memmove(&triggers[index], &triggers[index + 1],
            (trigger_count - index - 1) * sizeof(android_prop_trigger_t));
    trigger_count--;
    return 0;
}

int android_prop_trigger_fire(const char *property, const char *value) {
    int fired = 0;
    for (int i = 0; i < trigger_count; i++) {
        android_prop_trigger_t *t = &triggers[i];
        int match = 0;

        if (t->exact) {
            match = (strcmp(t->property, property) == 0 &&
                     strcmp(t->value, value) == 0);
        } else {
            /* Prefix match: trigger fires if property name starts with trigger's property */
            if (strncmp(t->property, property, strlen(t->property)) == 0) {
                /* Value match: wildcard "*" matches any, otherwise exact */
                if (strcmp(t->value, "*") == 0 || strcmp(t->value, value) == 0)
                    match = 1;
            }
        }

        if (match) {
            /* In production, this would queue the action for the init process */
            kprintf("[android-prop] trigger fired: %s=%s -> %s\n",
                    property, value, t->action);
            fired++;
        }
    }
    return fired;
}

int android_prop_trigger_count(void) { return trigger_count; }

/* ─── Build.prop loading ─── */

int android_prop_load_file(const char *path) {
    /* In production, this would read the file line by line */
    /* For now, we load from hardcoded defaults */
    (void)path;
    return 0;
}

int android_prop_parse_line(const char *line) {
    if (!line || line[0] == '#' || line[0] == '\0') return 0;

    /* Format: property=value */
    const char *eq = line;
    while (*eq && *eq != '=') eq++;
    if (*eq != '=') return -1;

    size_t nlen = eq - line;
    const char *val = eq + 1;

    if (nlen >= PROP_MAX_NAME || strlen(val) >= PROP_MAX_VALUE) return -1;

    char name[PROP_MAX_NAME];
    strlcpy(name, line, nlen + 1);
    android_prop_set(name, val);
    return 0;
}

int android_prop_load_defaults(void) {
    /* ─── Build identity ─── */
    android_prop_set("ro.build.id", "CODEOS.2026.07");
    android_prop_set("ro.build.display.id", "CodeOS 1.0");
    android_prop_set("ro.build.version.release", "16");
    android_prop_set("ro.build.version.sdk", "36");
    android_prop_set("ro.build.version.incremental", "1.0.0");
    android_prop_set("ro.build.type", "user");
    android_prop_set("ro.build.tags", "release-keys");
    android_prop_set("ro.build.date", "Tue Jul 22 2026");
    android_prop_set("ro.build.flavor", "codeos_x86_64-user");

    /* ─── Product identity ─── */
    android_prop_set("ro.product.brand", "CodeOS");
    android_prop_set("ro.product.device", "codeos");
    android_prop_set("ro.product.manufacturer", "CodeOS");
    android_prop_set("ro.product.model", "CodeOS Desktop");
    android_prop_set("ro.product.name", "codeos");
    android_prop_set("ro.product.board", "x86_64");

    /* ─── Hardware ─── */
    android_prop_set("ro.hardware", "codeos");
    android_prop_set("ro.board.platform", "x86_64");
    android_prop_set("ro.soc.model", "x86_64");
    android_prop_set("ro.product.first_api_level", "36");

    /* ─── Dalvik VM ─── */
    android_prop_set("dalvik.vm.heapstartsize", "8m");
    android_prop_set("dalvik.vm.heapgrowthlimit", "256m");
    android_prop_set("dalvik.vm.heapsize", "512m");
    android_prop_set("dalvik.vm.heaptargetutilization", "0.75");
    android_prop_set("dalvik.vm.heapminfree", "4m");
    android_prop_set("dalvik.vm.heapmaxfree", "16m");
    android_prop_set("dalvik.vm.usejit", "true");
    android_prop_set("dalvik.vm.dex2oat-Xms", "64m");
    android_prop_set("dalvik.vm.dex2oat-Xmx", "512m");
    android_prop_set("dalvik.vm.image-dex2oat-Xms", "64m");
    android_prop_set("dalvik.vm.image-dex2oat-Xmx", "64m");

    /* ─── Dexopt policy ─── */
    android_prop_set("pm.dexopt.post-boot", "verify");
    android_prop_set("pm.dexopt.first-boot", "verify");
    android_prop_set("pm.dexopt.install", "speed-profile");
    android_prop_set("pm.dexopt.bg-dexopt", "speed-profile");

    /* ─── Security ─── */
    android_prop_set("ro.treble.enabled", "true");
    android_prop_set("ro.secure", "1");
    android_prop_set("security.perf_harden", "1");
    android_prop_set("ro.debuggable", "0");
    android_prop_set("ro.adb.secure", "1");

    /* ─── CodeOS-specific ─── */
    android_prop_set("ro.codeos.version", "1.0.0");
    android_prop_set("ro.codeos.container.support", "true");
    android_prop_set("ro.codeos.vm.support", "true");
    android_prop_set("ro.codeos.display.bridge", "framebuffer");
    android_prop_set("ro.codeos.input.bridge", "virtio");

    /* ─── Virtual A/B OTA ─── */
    android_prop_set("ro.virtual_ab.enabled", "true");
    android_prop_set("ro.virtual_ab.compression.enabled", "true");

    /* ─── SELinux ─── */
    android_prop_set("ro.boot.selinux", "permissive");
    android_prop_set("selinux.reload_policy", "1");

    return prop_count;
}
