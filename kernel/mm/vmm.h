#ifndef MM_VMM_H
#define MM_VMM_H

#include "types.h"

void mm_vmm_init(void);
void mm_vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void mm_vmm_unmap_page(uint64_t virt);

#endif
