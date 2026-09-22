#ifndef MM_H
#define MM_H

#include "types.h"

void mm_init(uint32_t mem_lower, uint32_t mem_upper, uint32_t mmap_addr, uint32_t mmap_length);
uint64_t mm_total(void);
uint64_t mm_free(void);
void mm_print_regions(void);
uint64_t mm_used(void);

size_t mm_heap_used(void);
size_t mm_heap_peak(void);
size_t mm_heap_total(void);
int mm_defrag(void);

void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);

void heap_debug_print(void);
void heap_chain_dump(void);

/* Pre-allocate heap pages for early large allocations (e.g. LVGL/Qt init).
 * Call after vmm_init(). Returns number of pages successfully allocated. */
int mm_heap_preseed(int pages);

#endif
