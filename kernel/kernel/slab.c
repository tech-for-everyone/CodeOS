#include "slab.h"
#include "mm.h"
#include "pmm.h"
#include "vmm.h"
#include "kprintf.h"
#include "string.h"

static slab_cache_t caches[SLAB_CACHES];
static int cache_count = 0;
static slab_cache_t *kmalloc_cache = 0;

void slab_init(void) {
    memset(caches, 0, sizeof(caches));
    cache_count = 0;
    kmalloc_cache = slab_create("kmalloc-heap", 256, 16);
    kprintf("slab: initialized %d cache slots\n", SLAB_CACHES);
}

static int align_up_val(int v, int a) { return (v + a - 1) & ~(a - 1); }

slab_cache_t *slab_create(const char *name, uint32_t obj_size, uint32_t alignment) {
    if (cache_count >= SLAB_CACHES) return 0;
    slab_cache_t *c = &caches[cache_count++];
    memset(c, 0, sizeof(slab_cache_t));
    if (name) strncpy_safe(c->name, name, sizeof(c->name));
    uint32_t sz = obj_size < sizeof(slab_obj_t) ? (uint32_t)sizeof(slab_obj_t) : obj_size;
    c->obj_size = (uint32_t)align_up_val((int)sz, 8);
    c->alignment = alignment > 0 ? alignment : 8;
    c->in_use = 1;
    return c;
}

void slab_destroy(slab_cache_t *cache) {
    if (!cache) return;
    slab_page_t *pg = cache->pages;
    while (pg) {
        slab_page_t *nxt = pg->next;
        if (pg->phys) pmm_free_page(pg->phys);
        free(pg);
        pg = nxt;
    }
    cache->pages = 0;
    cache->page_count = 0;
    cache->in_use = 0;
}

static slab_page_t *slab_new_page(slab_cache_t *cache) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    slab_page_t *pg = (slab_page_t *)phys_to_virt(phys);
    memset(pg, 0, 4096);
    pg->phys = phys;
    pg->next = cache->pages;
    cache->pages = pg;
    cache->page_count++;
    int per_page = (4096 - (int)sizeof(slab_page_t)) / (int)cache->obj_size;
    if (per_page < 1) per_page = 1;
    pg->total = per_page;
    pg->free_count = per_page;
    char *data = (char *)pg + sizeof(slab_page_t);
    slab_obj_t *prev = 0;
    for (int i = 0; i < per_page; i++) {
        slab_obj_t *obj = (slab_obj_t *)(data + i * cache->obj_size);
        obj->next = prev;
        prev = obj;
    }
    pg->free_list = prev;
    return pg;
}

void *slab_alloc(slab_cache_t *cache) {
    if (!cache) return 0;
    spin_lock(&cache->lock);
    slab_page_t *pg = cache->pages;
    while (pg) {
        if (pg->free_count > 0) {
            slab_obj_t *obj = pg->free_list;
            if (obj) {
                pg->free_list = obj->next;
                pg->free_count--;
                cache->alloc_count++;
                spin_unlock(&cache->lock);
                return (void *)obj;
            }
        }
        pg = pg->next;
    }
    pg = slab_new_page(cache);
    if (!pg) { spin_unlock(&cache->lock); return 0; }
    slab_obj_t *obj = pg->free_list;
    if (obj) {
        pg->free_list = obj->next;
        pg->free_count--;
        cache->alloc_count++;
    }
    spin_unlock(&cache->lock);
    return (void *)obj;
}

void slab_free(slab_cache_t *cache, void *ptr) {
    if (!cache || !ptr) return;
    spin_lock(&cache->lock);
    slab_page_t *pg = cache->pages;
    while (pg) {
        uint64_t phys = pg->phys;
        uint64_t base = phys_to_virt(phys);
        uint64_t p = (uint64_t)ptr;
        if (p >= base && p < base + 4096) {
            slab_obj_t *obj = (slab_obj_t *)ptr;
            obj->next = pg->free_list;
            pg->free_list = obj;
            pg->free_count++;
            cache->free_count++;
            spin_unlock(&cache->lock);
            return;
        }
        pg = pg->next;
    }
    spin_unlock(&cache->lock);
}

slab_cache_t *slab_find_cache(uint32_t size) {
    for (int i = 0; i < cache_count; i++) {
        if (caches[i].in_use && caches[i].obj_size >= size)
            return &caches[i];
    }
    return 0;
}

void *kmalloc(uint32_t size) {
    if (!kmalloc_cache) kmalloc_cache = slab_create("kmalloc", 256, 16);
    if (size <= kmalloc_cache->obj_size) return slab_alloc(kmalloc_cache);
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    return (void *)phys_to_virt(phys);
}

void *kcalloc(uint32_t nmemb, uint32_t size) {
    uint32_t total = nmemb * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *krealloc(void *ptr, uint32_t size) {
    if (!ptr) return kmalloc(size);
    void *newp = kmalloc(size);
    if (newp) { memcpy(newp, ptr, size); kfree(ptr); }
    return newp;
}

void kfree(void *ptr) {
    if (!ptr) return;
    if (kmalloc_cache) slab_free(kmalloc_cache, ptr);
}

void slab_dump_stats(void) {
    kprintf("slab: %d caches\n", cache_count);
    for (int i = 0; i < cache_count; i++) {
        if (!caches[i].in_use) continue;
        kprintf("  %s: obj=%u pages=%d alloc=%lu free=%lu\n",
                caches[i].name, caches[i].obj_size, caches[i].page_count,
                caches[i].alloc_count, caches[i].free_count);
    }
}
