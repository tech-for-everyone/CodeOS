/* Reference VMM API for modular tree. Production code: kernel/vmm.c */

#include "vmm.h"
#include "kernel/kprintf.h"
#include "kernel/vmm.h"

void mm_vmm_init(void) {
    kprintf("mm/vmm: delegating to kernel/vmm\n");
    vmm_init();
}

void mm_vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    int ret = vmm_map_page(virt, phys, flags);
    if (ret < 0) {
        kprintf("mm/vmm: map_page(0x%lx -> 0x%lx, flags=0x%lx) FAILED\n",
                virt, phys, flags);
    }
}

void mm_vmm_unmap_page(uint64_t virt) {
    int ret = vmm_unmap_page(virt);
    if (ret < 0) {
        kprintf("mm/vmm: unmap_page(0x%lx) FAILED\n", virt);
    }
}
