#ifndef MM_HEAP_H
#define MM_HEAP_H


void *mm_heap_alloc(size_t size);
void  mm_heap_free(void *ptr);
size_t mm_heap_used(void);

#endif
