#ifndef MM_PMM_H
#define MM_PMM_H

#include "types.h"

#define MM_PAGE_SIZE  4096
#define MM_PAGE_SHIFT 12

void mm_pmm_init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end);
void *mm_pmm_alloc_page(void);
void  mm_pmm_free_page(void *addr);
uint64_t mm_pmm_free_count(void);

#endif
