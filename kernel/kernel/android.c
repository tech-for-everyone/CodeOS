#include "android.h"
#include "kprintf.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"
#include "process.h"

/* ── Binder implementation ── */

#define BINDER_MAX_REFS 32

static binder_ref_t binder_refs[BINDER_MAX_REFS];
static int binder_context_mgr;
static int binder_next_handle;

static int binder_alloc_ref(int pid) {
    for (int i = 0; i < BINDER_MAX_REFS; i++) {
        if (!binder_refs[i].active) {
            binder_refs[i].active = 1;
            binder_refs[i].pid = pid;
            binder_refs[i].handle = ++binder_next_handle;
            return binder_refs[i].handle;
        }
    }
    return -1;
}

static binder_ref_t *binder_get_ref(int handle) {
    for (int i = 0; i < BINDER_MAX_REFS; i++) {
        if (binder_refs[i].active && binder_refs[i].handle == (uint32_t)handle)
            return &binder_refs[i];
    }
    return 0;
}

int android_binder_cmd(binder_transaction_t *t) {
    if (!t) return -1;

    switch (t->cmd) {
    case BINDER_CMD_SET_CONTEXT_MGR: {
        int pid = proc_getpid();
        int handle = binder_alloc_ref(pid);
        if (handle < 0) return -1;
        binder_context_mgr = handle;
        t->handle = (uint32_t)handle;
        kprintf("android/binder: context manager registered (pid=%d handle=%d)\n", pid, handle);
        return 0;
    }

    case BINDER_CMD_TRANSACTION: {
        binder_ref_t *ref = binder_get_ref((int)t->handle);
        if (!ref) return -1;
        t->sender_pid = (uint32_t)proc_getpid();
        kprintf("android/binder: transaction handle=%d code=%d len=%d -> pid=%d\n",
                t->handle, t->code, t->data_len, ref->pid);
        return 0;
    }

    case BINDER_CMD_REPLY: {
        kprintf("android/binder: reply code=%d len=%d\n", t->code, t->data_len);
        return 0;
    }

    case BINDER_CMD_VERSION: {
        t->data_len = 0;
        return 0;
    }

    case BINDER_CMD_GET_REF: {
        int pid = proc_getpid();
        int handle = binder_alloc_ref(pid);
        if (handle < 0) return -1;
        t->handle = (uint32_t)handle;
        t->data_len = 0;
        return 0;
    }

    default:
        return -1;
    }
}

/* ── Ashmem implementation ── */

ashmem_region_t ashmem_regions[ANDROID_ASHMEM_MAX_REGIONS];

static int ashmem_find_free(void) {
    for (int i = 0; i < ANDROID_ASHMEM_MAX_REGIONS; i++) {
        if (!ashmem_regions[i].active) return i;
    }
    return -1;
}

static uint64_t ashmem_data_phys;
static int ashmem_initialized;

void android_init(void) {
    for (int i = 0; i < BINDER_MAX_REFS; i++)
        binder_refs[i].active = 0;
    binder_context_mgr = -1;
    binder_next_handle = 0;

    for (int i = 0; i < ANDROID_ASHMEM_MAX_REGIONS; i++)
        ashmem_regions[i].active = 0;

    ashmem_data_phys = pmm_alloc_page();
    if (ashmem_data_phys) {
        memset((void *)phys_to_virt(ashmem_data_phys), 0, 0x1000);
        ashmem_initialized = 1;
    }

    kprintf("android: compatibility layer ready (binder + ashmem)\n");
}

int android_ashmem_create(const char *name, int size, int prot) {
    if (!ashmem_initialized) return -1;
    if (size <= 0 || size > ANDROID_ASHMEM_MAX_SIZE) return -1;

    int id = ashmem_find_free();
    if (id < 0) return -1;

    int num_pages = (size + 0xFFF) / 0x1000;
    uint64_t phys = pmm_alloc_pages((size_t)num_pages);
    if (!phys) return -1;

    ashmem_regions[id].active = 1;
    ashmem_regions[id].size = size;
    ashmem_regions[id].prot = prot;
    ashmem_regions[id].phys_addr = phys;
    ashmem_regions[id].ref_count = 1;

    if (name) {
        int i;
        for (i = 0; name[i] && i < ANDROID_ASHMEM_NAME_MAX - 1; i++)
            ashmem_regions[id].name[i] = name[i];
        ashmem_regions[id].name[i] = 0;
    } else {
        ashmem_regions[id].name[0] = 0;
    }

    kprintf("android/ashmem: created id=%d size=%d phys=0x%lx name=%s\n",
            id, size, phys, name ? name : "(anon)");
    return id;
}

int android_ashmem_get_size(int id) {
    if (id < 0 || id >= ANDROID_ASHMEM_MAX_REGIONS || !ashmem_regions[id].active)
        return -1;
    return ashmem_regions[id].size;
}

int android_ashmem_set_name(int id, const char *name) {
    if (id < 0 || id >= ANDROID_ASHMEM_MAX_REGIONS || !ashmem_regions[id].active)
        return -1;
    if (!name) return -1;
    int i;
    for (i = 0; name[i] && i < ANDROID_ASHMEM_NAME_MAX - 1; i++)
        ashmem_regions[id].name[i] = name[i];
    ashmem_regions[id].name[i] = 0;
    return 0;
}

void *android_ashmem_mmap(int id) {
    if (id < 0 || id >= ANDROID_ASHMEM_MAX_REGIONS || !ashmem_regions[id].active)
        return 0;
    ashmem_regions[id].ref_count++;
    return (void *)phys_to_virt(ashmem_regions[id].phys_addr);
}
