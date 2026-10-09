#include "android.h"
#include "unistd.h"
#include "string.h"

int binder_set_context_mgr(void) {
    binder_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.cmd = BINDER_SET_CONTEXT_MGR;
    int ret = sys_binder(&t, sizeof(t));
    if (ret < 0) return -1;
    return (int)t.handle;
}

int binder_transaction(unsigned int handle, unsigned int code,
                       const void *data, unsigned int len) {
    binder_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.cmd = BINDER_TRANSACTION;
    t.handle = handle;
    t.code = code;
    t.data_len = len < sizeof(t.data) ? len : sizeof(t.data);
    if (data && t.data_len > 0)
        memcpy(t.data, data, t.data_len);
    return sys_binder(&t, sizeof(t));
}

int binder_get_ref(void) {
    binder_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.cmd = BINDER_GET_REF;
    int ret = sys_binder(&t, sizeof(t));
    if (ret < 0) return -1;
    return (int)t.handle;
}

int ashmem_create(const char *name, int size) {
    return sys_ashmem(0, (void *)name, size, 0);
}

int ashmem_get_size(int fd) {
    return sys_ashmem(1, 0, fd, 0);
}

int ashmem_set_name(int fd, const char *name) {
    return sys_ashmem(2, 0, fd, (int)(unsigned long)name);
}
