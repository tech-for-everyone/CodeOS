#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "vmm.h"

/* PAGE_SIZE, PAGE_MASK, PAGE_SIZE_LOG2 are defined in vmm.h. */
#define PAGE_SHIFT PAGE_SIZE_LOG2

void pmm_init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(size_t n);
void  pmm_free_page(uint64_t vma);
void  pmm_free_pages(uint64_t vma, size_t n);
int pmm_is_page_allocated(uint64_t vma);
uint64_t pmm_total_pages(void);
uint64_t pmm_count_free(void);
uint64_t pmm_count_used(void);

#endif
