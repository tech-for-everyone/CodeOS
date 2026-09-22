/* Reference heap API for modular tree. Production code: kernel/mm.c (malloc/free) */

#include "heap.h"
#include "kernel/kprintf.h"
#include "kernel/mm.h"

void *mm_heap_alloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        kprintf("mm/heap: alloc(%zu) FAILED (OOM)\n", size);
    }
    return ptr;
}

void mm_heap_free(void *ptr) {
    if (!ptr) {
        kprintf("mm/heap: free(NULL) ignored\n");
        return;
    }
    free(ptr);
}

size_t mm_heap_used(void) {
    extern size_t mm_heap_used(void);
    return mm_heap_used();
}
