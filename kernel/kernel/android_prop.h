#ifndef ANDROID_PROPERTY_H
#define ANDROID_PROPERTY_H

#include "types.h"

#define PROP_MAX_NAME     92
#define PROP_MAX_VALUE    92
#define PROP_MAX_ENTRIES  256
#define PROP_MAX_TRIGGERS 64

/* ─── Property entry ─── */
typedef struct {
    char name[PROP_MAX_NAME];
    char value[PROP_MAX_VALUE];
    int  readonly;          /* ro.* properties are immutable after set */
    int  persist;           /* persist.* properties survive reboot */
    uint32_t crc32;         /* hash for fast lookup */
} android_prop_t;

/* ─── Property trigger ─── */
typedef struct {
    char property[PROP_MAX_NAME];
    char value[PROP_MAX_VALUE];  /* wildcard "*" matches any value */
    char action[128];            /* action to execute */
    int  exact;                  /* exact match vs prefix match */
} android_prop_trigger_t;

/* ─── Init ─── */
int android_prop_init(void);

/* ─── Property get/set ─── */
int android_prop_set(const char *name, const char *value);
int android_prop_get(const char *name, char *value, int max);
int android_prop_get_int(const char *name, int def);
int android_prop_get_bool(const char *name, int def);
int android_prop_delete(const char *name);

/* ─── Property queries ─── */
int android_prop_get_prefix(const char *prefix, char out[][PROP_MAX_NAME], int max);
int android_prop_count(void);
int android_prop_list(char names[][PROP_MAX_NAME], int max);

/* ─── Property triggers ─── */
int android_prop_trigger_add(const char *property, const char *value,
                             const char *action, int exact);
int android_prop_trigger_remove(int index);
int android_prop_trigger_fire(const char *property, const char *value);
int android_prop_trigger_count(void);

/* ─── Build.prop loading ─── */
int android_prop_load_file(const char *path);
int android_prop_load_defaults(void);

/* ─── Property file parsing (for .prop format) ─── */
int android_prop_parse_line(const char *line);

#endif
