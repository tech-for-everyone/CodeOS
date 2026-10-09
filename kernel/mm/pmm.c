/* Reference PMM API for modular tree. Production code: kernel/pmm.c */

#include "pmm.h"
#include "kernel/kprintf.h"
#include "kernel/pmm.h"

void mm_pmm_init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end) {
    kprintf("mm/pmm: delegating to kernel/pmm\n");
    pmm_init(mem_size, kernel_start, kernel_end);
}

void *mm_pmm_alloc_page(void) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        kprintf("mm/pmm: alloc_page FAILED (OOM)\n");
        return 0;
    }
    return (void*)phys;
}

void mm_pmm_free_page(void *addr) {
    if (!addr) {
        kprintf("mm/pmm: free_page(NULL) ignored\n");
        return;
    }
    pmm_free_page((uint64_t)addr);
}

uint64_t mm_pmm_free_count(void) {
    return pmm_count_free();
}
