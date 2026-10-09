#ifndef SLAB_H
#define SLAB_H
#include "types.h"
#include "spinlock.h"

#define SLAB_MIN_SIZE   16
#define SLAB_MAX_SIZE   4096
#define SLAB_CACHES     64

typedef struct slab_obj { struct slab_obj *next; } slab_obj_t;

typedef struct slab_page {
    struct slab_page *next;
    uint64_t phys;
    int total, free_count;
    slab_obj_t *free_list;
} slab_page_t;

typedef struct slab_cache {
    char name[32];
    uint32_t obj_size;
    uint32_t alignment;
    slab_page_t *pages;
    int page_count;
    spinlock_t lock;
    int in_use;
    uint64_t alloc_count;
    uint64_t free_count;
} slab_cache_t;

void slab_init(void);
slab_cache_t *slab_create(const char *name, uint32_t obj_size, uint32_t alignment);
void slab_destroy(slab_cache_t *cache);
void *slab_alloc(slab_cache_t *cache);
void slab_free(slab_cache_t *cache, void *ptr);
slab_cache_t *slab_find_cache(uint32_t size);
void *kmalloc(uint32_t size);
void *kcalloc(uint32_t nmemb, uint32_t size);
void *krealloc(void *ptr, uint32_t size);
void kfree(void *ptr);
void slab_dump_stats(void);
#endif
