#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"

#define SEC_AUDIT_MAX     256
#define SEC_INTEGRITY_MAX 64
#define SEC_LOG_MAX       1024

typedef struct {
    char path[128];
    uint32_t hash;
    int      size;
    int      monitored;
} sec_integrity_t;

typedef struct {
    char msg[256];
    int  pid;
    int  uid;
    uint32_t time;
} sec_audit_t;

void     sec_init(void);
void     sec_log(const char *msg);
void     sec_audit_dump(void);
int      sec_integrity_add(const char *path);
int      sec_integrity_check(const char *path);
void     sec_integrity_list(void);
void     sec_set_permissions(const char *path, int mode);
int      sec_get_permissions(const char *path);
int      sec_enforce_firewall(int enable);
int      sec_firewall_enabled(void);
void     sec_firewall_rule_add(const char *rule);
int      sec_firewall_check(uint32_t src_ip, uint32_t dst_ip, uint8_t proto, uint16_t src_port, uint16_t dst_port);
int      sec_firewall_rule_count(void);
void     sec_firewall_rule_get(int index, char *buf, int bufsize);
void     sec_firewall_rule_remove(int index);
void     sec_firewall_rule_clear(void);
void     sec_status(void);

#endif
