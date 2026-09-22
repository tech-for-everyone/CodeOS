#ifndef ANDROID_H
#define ANDROID_H

#include "types.h"

#define ANDROID_BINDER_MAX_TRANSACTIONS 32
#define ANDROID_BINDER_MAX_DATA 256
#define ANDROID_ASHMEM_MAX_REGIONS 16
#define ANDROID_ASHMEM_NAME_MAX 32
#define ANDROID_ASHMEM_MAX_SIZE (1024 * 1024)

typedef enum {
    BINDER_CMD_NONE = 0,
    BINDER_CMD_SET_CONTEXT_MGR,
    BINDER_CMD_TRANSACTION,
    BINDER_CMD_REPLY,
    BINDER_CMD_VERSION,
    BINDER_CMD_GET_REF,
} binder_cmd_t;

typedef struct {
    uint32_t cmd;
    uint32_t handle;
    uint32_t code;
    uint32_t flags;
    uint32_t sender_pid;
    uint32_t data_len;
    uint8_t data[ANDROID_BINDER_MAX_DATA];
} binder_transaction_t;

typedef struct {
    uint32_t handle;
    uint32_t pid;
    int active;
} binder_ref_t;

typedef struct {
    int active;
    char name[ANDROID_ASHMEM_NAME_MAX];
    int size;
    int prot;
    uint64_t phys_addr;
    int ref_count;
} ashmem_region_t;

void android_init(void);

int android_binder_cmd(binder_transaction_t *t);
int android_ashmem_create(const char *name, int size, int prot);
int android_ashmem_get_size(int id);
int android_ashmem_set_name(int id, const char *name);
void *android_ashmem_mmap(int id);

extern ashmem_region_t ashmem_regions[ANDROID_ASHMEM_MAX_REGIONS];

#endif
