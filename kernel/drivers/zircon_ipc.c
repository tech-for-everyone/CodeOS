#include "zircon_ipc.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"
#include "../kernel/kprintf.h"
#include "string.h"

zircon_ipc_ring_t *zircon_ipc_ring;

void zircon_ipc_init(void) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        kprintf("zircon_ipc: failed to allocate shared page\n");
        return;
    }
    zircon_ipc_ring = (zircon_ipc_ring_t *)(uintptr_t)phys_to_virt(phys);
    memset(zircon_ipc_ring, 0, sizeof(zircon_ipc_ring_t));
    zircon_ipc_ring->magic = ZIRCON_IPC_MAGIC;
    zircon_ipc_ring->head = 0;
    zircon_ipc_ring->tail = 0;
    kprintf("zircon_ipc: shared page at phys=0x%lx virt=%p\n",
            phys, (void*)zircon_ipc_ring);
}

int zircon_ipc_send(const zircon_ipc_msg_t *msg) {
    if (!zircon_ipc_ring) return -1;
    uint32_t next = (zircon_ipc_ring->head + 1) % 16;
    if (next == zircon_ipc_ring->tail) return -1; /* full */
    zircon_ipc_ring->msgs[zircon_ipc_ring->head] = *msg;
    __atomic_store_n(&zircon_ipc_ring->head, next, __ATOMIC_RELEASE);
    return 0;
}

int zircon_ipc_recv(zircon_ipc_msg_t *msg) {
    if (!zircon_ipc_ring) return -1;
    if (zircon_ipc_ring->tail == zircon_ipc_ring->head) return 0;
    uint32_t t = zircon_ipc_ring->tail;
    *msg = zircon_ipc_ring->msgs[t];
    __atomic_store_n(&zircon_ipc_ring->tail, (t + 1) % 16, __ATOMIC_RELEASE);
    return 1;
}

int zircon_ipc_available(void) {
    if (!zircon_ipc_ring) return 0;
    return (zircon_ipc_ring->head != zircon_ipc_ring->tail);
}

void zircon_ipc_dump(void) {
    if (!zircon_ipc_ring) {
        kprintf("zircon_ipc: not initialized\n");
        return;
    }
    kprintf("  magic=0x%x head=%u tail=%u\n",
            zircon_ipc_ring->magic,
            zircon_ipc_ring->head,
            zircon_ipc_ring->tail);
    uint32_t count = 0;
    uint32_t i = zircon_ipc_ring->tail;
    while (i != zircon_ipc_ring->head) {
        zircon_ipc_msg_t *m = &zircon_ipc_ring->msgs[i];
        kprintf("  [%u] type=%d id=%d (%d,%d %dx%d) '%s'\n",
                i, m->type, m->id, m->x, m->y, m->w, m->h, m->text);
        i = (i + 1) % 16;
        count++;
    }
    kprintf("  %u messages in ring\n", count);
}
