#ifndef _ANDROID_H
#define _ANDROID_H

#define BINDER_SET_CONTEXT_MGR 1
#define BINDER_TRANSACTION 2
#define BINDER_REPLY 3
#define BINDER_VERSION 4
#define BINDER_GET_REF 5

#define ANDROID_ASHMEM_MAX_NAME 32

typedef struct {
    unsigned int cmd;
    unsigned int handle;
    unsigned int code;
    unsigned int flags;
    unsigned int sender_pid;
    unsigned int data_len;
    unsigned char data[256];
} binder_transaction_t;

/* Binder APIs */
int binder_set_context_mgr(void);
int binder_transaction(unsigned int handle, unsigned int code,
                       const void *data, unsigned int len);
int binder_get_ref(void);

/* Ashmem APIs */
int ashmem_create(const char *name, int size);
int ashmem_get_size(int fd);
int ashmem_set_name(int fd, const char *name);

#endif
