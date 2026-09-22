#ifndef CLAMAV_H
#define CLAMAV_H

#include <stdint.h>

#define CLAMAV_MAX_SIGS   256
#define CLAMAV_NAME_MAX   48
#define CLAMAV_SIG_HASH   32
#define CLAMAV_MAX_PAT    64
#define CLAMAV_DB_PATH    "/.clamav/main.cvd"

typedef struct {
    char name[CLAMAV_NAME_MAX];
    uint32_t hash[8];
    uint32_t size_min;
    uint32_t size_max;
    uint8_t  pattern[CLAMAV_MAX_PAT];
    int      pat_len;
    int      threat_level;
} clamav_sig_t;

void     clamav_init(void);
int      clamav_load_db(void);
int      clamav_update_db(void);
int      clamav_scan_file(const char *path);
int      clamav_scan_buf(const char *name, const uint8_t *buf, int len);
void     clamav_list_sigs(void);
int      clamav_sig_count(void);
int      clamav_add_sig(const char *name, const char *hex_pattern, int threat);
void     clamav_scan_system(void);
int      clamav_scan_tree(const char *path);
int      clamav_quarantine(const char *path);
void     clamav_stats(void);
int      clamav_threat_level(void);

#endif
