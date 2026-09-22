#ifndef AUDIT_H
#define AUDIT_H
#include "types.h"
#include "spinlock.h"
#define AUDIT_MAX_RULES 64
#define AUDIT_MAX_LOGS 1024
#define AUDIT_SYS_EXIT    1
#define AUDIT_SYS_CALL    2
#define AUDIT_FILE_OPEN   3
#define AUDIT_FILE_CLOSE  4
#define AUDIT_FILE_READ   5
#define AUDIT_FILE_WRITE  6
#define AUDIT_PROC_EXEC   7
#define AUDIT_PROC_EXIT   8
#define AUDIT_NET_CONNECT 9
#define AUDIT_NET_BIND    10
#define AUDIT_AUTH        11
#define AUDIT_MOUNT       12
#define AUDIT_TIME_CHANGE 13
#define AUDIT_MAX_ID 256
typedef enum { AUDIT_ALWAYS=0, AUDIT_FAILURE=1, AUDIT_SUCCESS=2 } audit_filter_t;
typedef struct {
    int syscall_num; audit_filter_t filter; int enabled;
    uint64_t key; char comm[16]; char exe[64];
    uint32_t uid, pid;
} audit_rule_t;
typedef struct {
    uint64_t id; uint64_t timestamp; uint32_t serial;
    int type; int success; int pid; uint32_t uid;
    char comm[16]; char exe[64]; char data[256];
} audit_record_t;
typedef struct {
    audit_rule_t rules[AUDIT_MAX_RULES];
    int rule_count;
    audit_record_t log[AUDIT_MAX_LOGS];
    int log_head, log_tail, log_count;
    uint64_t next_id;
    int enabled;
    spinlock_t lock;
} audit_context_t;
void audit_init(void);
int audit_set_enabled(int enabled);
int audit_add_rule(const audit_rule_t *rule);
int audit_del_rule(int index);
int audit_log(int type, int success, const char *comm, const char *exe, const char *data);
int audit_read(audit_record_t *rec);
int audit_get_count(void);
void audit_dump(void);
#endif
