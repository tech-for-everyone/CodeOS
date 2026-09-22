#ifndef VMM_H
#define VMM_H

#include "types.h"

#define PAGE_PRESENT  (1 << 0)
#define PAGE_WRITE    (1 << 1)
#define PAGE_USER     (1 << 2)
#define PAGE_PWT      (1 << 3)
#define PAGE_PCD      (1 << 4)
#define PAGE_HUGE     (1 << 7)
#define PAGE_COW      (1 << 9)
#define PAGE_NX       (1ULL << 63)

/* Page sizes used by BRK / mmap / VMM helpers. */
#define PAGE_SIZE      0x1000UL
#define PAGE_SIZE_LOG2 12
#define PAGE_MASK      (~(PAGE_SIZE - 1))

/* Physical ↔ Virtual conversion.
 * The page tables have PML4[256] identity-mapped:
 *   VMA 0xFFFF800000000000 + P → physical P
 * boot.S exports phys_to_virt_base = 0xFFFF800000000000.
 */
extern uint64_t phys_to_virt_base;

static inline uint64_t phys_to_virt(uint64_t phys) {
    return phys_to_virt_base + phys;
}

uint64_t vmm_virt_to_phys(uint64_t virt);

static inline uint64_t virt_to_phys(uint64_t virt) {
    return vmm_virt_to_phys(virt);
}

void vmm_init(void);
int  vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int  vmm_unmap_page(uint64_t virt);
int  vmm_get_mapping(uint64_t virt, uint64_t *phys, uint64_t *flags);
int  vmm_set_flags(uint64_t virt, uint64_t flags);
void *vmm_alloc_page_at(uint64_t virt, uint64_t flags);
void vmm_switch_pml4(uint64_t *pml4);
uint64_t *vmm_current_pml4(void);
uint64_t *vmm_kernel_pml4(void);

/* CR3 helpers for context-switch. */
void vmm_set_cr3(uint64_t *pml4);
uint64_t *vmm_get_cr3(void);

#endif
