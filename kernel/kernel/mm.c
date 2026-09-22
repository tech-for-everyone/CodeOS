#include "mm.h"
#include "kprintf.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"
#include "spinlock.h"

#define HEAP_CHUNK_MAGIC 0xDEADBEEFCAFEBABEULL

void *codeos_track_ptr;
void *codeos_guard_addr;

void codeos_guard_theme(void *addr) {
    uint64_t page = (uint64_t)addr & ~0xFFFULL;
    uint64_t phys, fl;
    int r = vmm_get_mapping(page, &phys, &fl);
    if (r == 0)
        vmm_map_page(page, phys, fl & ~PAGE_WRITE);
    codeos_guard_addr = (void *)page;
    kprintf("GUARD: theme page=%p r=%d phys=%p flags=%lx\n",
            (void *)page, r, (void *)phys, (unsigned long)fl);
}

extern int pmm_is_page_allocated(uint64_t vma);

void codeos_dump_theme_mem(void *addr);


/* ── Size class freelists for common small allocations ── */
/* Classes: 16, 32, 64, 128, 256, 512 bytes.
 * Anything larger goes to the general heap. */
#define NUM_SIZE_CLASSES 6
static const size_t size_class_sizes[NUM_SIZE_CLASSES] = { 16, 32, 64, 128, 256, 512 };
#define SIZE_CLASS_MAX_PAGES 64

/* Each size class has its own freelists for O(1) alloc/free of common sizes */
typedef struct sized_block {
    struct sized_block *next;
} sized_block_t;

static sized_block_t *size_class_free[NUM_SIZE_CLASSES];
static int size_class_count[NUM_SIZE_CLASSES];

/* Track pages allocated to each size class so we can identify free() targets.
   Each page carries a per-block bitmap (bit = 1 means the block is free/on the
   freelist, 0 = allocated). This lets free() reject double-frees instead of
   pushing a duplicate onto the freelist: a block freed twice stayed on the
   list twice, and a later malloc handed the same 16-byte block to both the
   QPlatformTheme and QPlatformScreenPrivate, corrupting the live theme. */
typedef struct {
    uint64_t phys;
    uint32_t used_bits[8];   /* 256 bits max (16-byte class) */
} size_class_page_t;

static size_class_page_t size_class_pages[NUM_SIZE_CLASSES][SIZE_CLASS_MAX_PAGES];
static int size_class_page_count[NUM_SIZE_CLASSES];

static int bit_get(const uint32_t *bits, int i) { return (bits[i >> 5] >> (i & 31)) & 1; }
static void bit_set(uint32_t *bits, int i) { bits[i >> 5] |= (1u << (i & 31)); }
static void bit_clr(uint32_t *bits, int i) { bits[i >> 5] &= ~(1u << (i & 31)); }

/* ── General heap (linked list allocator) ── */

typedef struct heap_chunk {
    uint64_t magic;
    size_t size;
    struct heap_chunk *next;
    struct heap_chunk *prev;
    int free;
    uint64_t _pad; /* sizeof must be multiple of 16 so malloc returns 16-byte-aligned pointers */
} heap_chunk_t;

static heap_chunk_t *heap_first;
static spinlock_t heap_lock = SPINLOCK_INIT;
static int heap_inited;
static size_t heap_total_size;
static size_t heap_used_size;
static size_t heap_peak_used;

/* Allocation history ring for diagnosing heap corruption */
#define MALLOC_RING 2048
struct malloc_rec {
    uint64_t data;
    uint64_t size;
    uint64_t caller;
    uint32_t kind; /* 1=alloc 2=free */
};
static struct malloc_rec malloc_ring[MALLOC_RING];
static unsigned malloc_ring_idx;

static void ring_add(uint64_t data, uint64_t size, uint64_t caller, int kind) {
    malloc_ring[malloc_ring_idx].data = data;
    malloc_ring[malloc_ring_idx].size = size;
    malloc_ring[malloc_ring_idx].caller = caller;
    malloc_ring[malloc_ring_idx].kind = (uint32_t)kind;
    malloc_ring_idx = (malloc_ring_idx + 1) % MALLOC_RING;
}

static void ring_dump(uint64_t lo, uint64_t hi) {
    kprintf("HEAP-RING (data in [%p,%p]):\n", (void *)lo, (void *)hi);
    int printed = 0;
    for (int i = 0; i < MALLOC_RING; i++) {
        struct malloc_rec *r = &malloc_ring[i];
        if (r->data && r->data >= lo && r->data < hi) {
            kprintf("  [%5d] %s data=%p size=%lu caller=%p\n",
                    i, r->kind == 2 ? "FREE" : "ALLOC",
                    (void *)r->data, (unsigned long)r->size, (void *)r->caller);
            if (++printed >= 32) break;
        }
    }
    if (!printed) kprintf("  (none)\n");
}

/* ── Size class helpers ── */

static int size_class_index(size_t size) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= size_class_sizes[i]) return i;
    }
    return -1;
}

static int size_class_find_block(void *ptr, int *cls_out, int *slot_out, int *idx_out);

static void *size_class_alloc(int idx) {
    if (size_class_free[idx]) {
        sized_block_t *block = size_class_free[idx];
        size_class_free[idx] = block->next;
        size_class_count[idx]--;
        int cls, slot, bi;
        if (size_class_find_block(block, &cls, &slot, &bi) == 0 && cls == idx) {
            if (bit_get(size_class_pages[cls][slot].used_bits, bi) != 1) {
                kprintf("!! SIZE-CLASS LIST-CORRUPT pop=%p cls=%d\n", (void *)block, cls);
            }
            bit_clr(size_class_pages[cls][slot].used_bits, bi);
        }
        return (void *)block;
    }
    /* Allocate a new page and carve it into blocks of this class size */
    if (size_class_page_count[idx] >= SIZE_CLASS_MAX_PAGES) return 0;
    uint64_t phys = (uint64_t)pmm_alloc_page();
    if (!phys) return 0;
#ifdef CODEOS_DEBUG
    if (phys >= 0x200000 && phys < 0x250000)
        kprintf("!! SC-NEWPAGE cls=%d phys=%p\n", idx, (void *)phys);
#endif
    int slot = size_class_page_count[idx];
    size_class_pages[idx][slot].phys = phys;
    sized_block_t *page = (sized_block_t *)phys_to_virt(phys);
    size_t blk_size = size_class_sizes[idx];
    /* Carve into blocks, link them into the freelist (skip first — we return it) */
    size_t total = PAGE_SIZE / blk_size;
    for (size_t i = 0; i < total; i++)
        bit_set(size_class_pages[idx][slot].used_bits, (int)i);
    size_class_page_count[idx]++;
    bit_clr(size_class_pages[idx][slot].used_bits, 0);
    for (size_t i = 1; i < total; i++) {
        sized_block_t *b = (sized_block_t *)((char *)page + i * blk_size);
        b->next = size_class_free[idx];
        size_class_free[idx] = b;
        size_class_count[idx]++;
    }
    return (void *)page;
}

/* Check if a pointer belongs to a size-class page; if so, return its class index (0-based) or -1 */
static int size_class_find_page(void *ptr) {
    uint64_t addr = (uint64_t)ptr;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        size_t blk = size_class_sizes[i];
        for (int j = 0; j < size_class_page_count[i]; j++) {
            uint64_t page_virt = (uint64_t)phys_to_virt(size_class_pages[i][j].phys);
            /* Check if ptr falls within this page's block region */
            if (addr >= page_virt && addr < page_virt + PAGE_SIZE) {
                /* Verify it's actually a block of this class size (alignment check) */
                if ((addr - page_virt) % blk == 0)
                    return i;
            }
        }
    }
    return -1;
}

/* Locate a block in a size-class page. Returns 0 and fills cls/slot/idx on
   success, 1 if the address is inside a page but misaligned, -1 if not found. */
static int size_class_find_block(void *ptr, int *cls_out, int *slot_out, int *idx_out) {
    uint64_t addr = (uint64_t)ptr;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        size_t blk = size_class_sizes[i];
        for (int j = 0; j < size_class_page_count[i]; j++) {
            uint64_t page_virt = (uint64_t)phys_to_virt(size_class_pages[i][j].phys);
            if (addr >= page_virt && addr < page_virt + PAGE_SIZE) {
                size_t off = addr - page_virt;
                if (off % blk == 0) {
                    *cls_out = i;
                    *slot_out = j;
                    *idx_out = (int)(off / blk);
                    return 0;
                }
                return 1;
            }
        }
    }
    return -1;
}

void codeos_dump_theme_mem(void *addr) {
    int cls = size_class_find_page(addr);
    kprintf("THEMEMEM: theme=%p size_class=%d pmm_alloc=%d\n",
            addr, cls, pmm_is_page_allocated(virt_to_phys((uint64_t)addr)));
    kprintf("THEMEMEM: heap_first=%p\n", (void *)heap_first);
    heap_chunk_t *cur = heap_first;
    int n = 0;
    while (cur && n < 64) {
        uint64_t d = (uint64_t)cur + sizeof(heap_chunk_t);
        if (cur->magic == HEAP_CHUNK_MAGIC &&
            d >= 0xffff800000200000ULL && d < 0xffff800000210000ULL)
            kprintf("THEMEMEM: chunk=%p size=%lu free=%d\n",
                    (void *)cur, (unsigned long)cur->size, cur->free);
        uint64_t a = (uint64_t)addr;
        if (cur->magic == HEAP_CHUNK_MAGIC && a >= d && a < d + cur->size) {
            kprintf("THEMEMEM: CONTAINS chunk=%p size=%lu free=%d\n",
                    (void *)cur, (unsigned long)cur->size, cur->free);
            break;
        }
        if (cur->magic != HEAP_CHUNK_MAGIC) {
            kprintf("THEMEMEM: BADMAGIC chunk=%p next=%p\n", (void *)cur, (void *)cur->next);
            break;
        }
        cur = cur->next;
        n++;
    }
}

void codeos_dump_heap_corrupt(void) {
    kprintf("HEAP: total=%lu used=%lu peak=%lu\n",
            (unsigned long)heap_total_size, (unsigned long)heap_used_size,
            (unsigned long)heap_peak_used);
    heap_chunk_t *cur = heap_first;
    unsigned long n = 0, bad = 0, nfree = 0;
    size_t max_free = 0;
    while (cur) {
        n++;
        if (cur->magic != HEAP_CHUNK_MAGIC) {
            kprintf("HEAP-CORRUPT chunk=%p magic=%016lx size=%lu free=%d prev=%p next=%p idx=%lu\n",
                    (void *)cur, cur->magic, (unsigned long)cur->size, cur->free,
                    (void *)cur->prev, (void *)cur->next, n);
            bad++;
            if (bad >= 8) break;
        } else {
            if (cur->free) {
                nfree++;
                if (cur->size > max_free) max_free = cur->size;
            }
            if (cur->next) {
                char *cend = (char *)cur + sizeof(heap_chunk_t) + cur->size;
                char *nstart = (char *)cur->next;
                if (cend > nstart) {
                    kprintf("HEAP-OVERLAP cur=%p size=%lu end=%p next=%p idx=%lu\n",
                            (void *)cur, (unsigned long)cur->size, (void *)cend,
                            (void *)nstart, n);
                }
            }
        }
        cur = cur->next;
    }
    kprintf("HEAP: chunks=%lu bad=%lu free_chunks=%lu max_free=%lu first=%p\n",
            n, bad, nfree, (unsigned long)max_free, (void *)heap_first);
}

/* ── General heap helpers ── */

static heap_chunk_t *find_free_chunk(size_t size);

struct pageadd_rec {
    uint64_t phys;
    uint64_t last_addr;
    uint64_t last_size;
    uint32_t last_free;
    uint32_t merged;
};
#define PAGEADD_RING 32
static struct pageadd_rec pageadd_ring[PAGEADD_RING];
static unsigned pageadd_idx;

static void pageadd_note(uint64_t phys, heap_chunk_t *last, heap_chunk_t *chunk, int merged) {
    (void)chunk;
    struct pageadd_rec *r = &pageadd_ring[pageadd_idx];
    pageadd_idx = (pageadd_idx + 1) % PAGEADD_RING;
    r->phys = phys;
    r->last_addr = last ? (uint64_t)last : 0;
    r->last_size = last ? last->size : 0;
    r->last_free = last ? (unsigned)last->free : 0;
    r->merged = (unsigned)merged;
}

static void pageadd_dump(void) {
    for (unsigned i = 0; i < PAGEADD_RING; i++) {
        unsigned idx = (pageadd_idx + i) % PAGEADD_RING;
        struct pageadd_rec *r = &pageadd_ring[idx];
        if (!r->phys) continue;
        kprintf("PAGEADD[%2u] phys=%p last=%p lastsize=%lu lastfree=%d %s\n",
                i, (void *)r->phys, (void *)r->last_addr,
                r->last_size, r->last_free,
                r->merged ? "MERGED" : "APPEND");
    }
}

static void check_new_page_overlap(uint64_t phys) {
    heap_chunk_t *cur = heap_first;
    char *p = (char *)phys_to_virt(phys);
    char *pend = p + PAGE_SIZE;
    while (cur) {
        if (cur->magic != HEAP_CHUNK_MAGIC) return;
        char *cstart = (char *)cur;
        char *cend = cstart + sizeof(heap_chunk_t) + cur->size;
        if ((p >= cstart && p < cend) || (pend > cstart && pend <= cend)) {
            kprintf("!! DOUBLE-ALLOC page=%p phys=%p overlaps chunk=%p size=%lu free=%d\n",
                    (void *)p, (void *)phys, (void *)cur,
                    (unsigned long)cur->size, cur->free);
            return;
        }
        cur = cur->next;
    }
}

static void heap_add_page(void) {
    uint64_t phys = (uint64_t)pmm_alloc_page();
    if (!phys) {
        kprintf("!! heap_add_page: pmm_alloc_page failed\n");
        return;
    }
    check_new_page_overlap(phys);
    heap_chunk_t *chunk = (heap_chunk_t *)phys_to_virt(phys);
    chunk->magic = HEAP_CHUNK_MAGIC;
    chunk->size = PAGE_SIZE - sizeof(heap_chunk_t);
    chunk->free = 1;
    chunk->next = 0;
    chunk->prev = 0;
    if (!heap_first) {
        heap_first = chunk;
        pageadd_note(phys, 0, chunk, 0);
        return;
    }
    heap_chunk_t *last = heap_first;
    while (last->next) last = last->next;
    if (last->free &&
        (char *)last + sizeof(heap_chunk_t) + last->size == (char *)chunk) {
        last->size += sizeof(heap_chunk_t) + chunk->size;
        pageadd_note(phys, last, chunk, 1);
    } else {
        last->next = chunk;
        chunk->prev = last;
        pageadd_note(phys, last, chunk, 0);
    }
    heap_total_size += PAGE_SIZE;
}

static heap_chunk_t *find_free_chunk(size_t size) {
    heap_chunk_t *cur = heap_first;
    while (cur) {
        if (cur->magic != HEAP_CHUNK_MAGIC) return 0;
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return 0;
}

static void heap_add_large_chunk(size_t min_size) {
    int needed_pages = (min_size + sizeof(heap_chunk_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    if (needed_pages < 1) needed_pages = 1;
    uint64_t phys = pmm_alloc_pages((size_t)needed_pages);
    if (!phys) {
        kprintf("!! heap_add_large_chunk: pmm_alloc_pages(%d) FAILED\n", needed_pages);
        return;
    }
    check_new_page_overlap(phys);
    heap_chunk_t *chunk = (heap_chunk_t *)phys_to_virt(phys);
    chunk->magic = HEAP_CHUNK_MAGIC;
    chunk->size = (size_t)needed_pages * PAGE_SIZE - sizeof(heap_chunk_t);
    chunk->free = 1;
    chunk->next = 0;
    chunk->prev = 0;
    if (!heap_first) {
        heap_first = chunk;
    } else {
        heap_chunk_t *last = heap_first;
        while (last->next) last = last->next;
        last->next = chunk;
        chunk->prev = last;
    }
    heap_total_size += (size_t)needed_pages * PAGE_SIZE;
}

int mm_heap_preseed(int pages) {
    if (pages <= 0) return 0;
    int allocated = 0;
    for (int i = 0; i < pages; i++) {
        /* Use the existing heap_add_page logic for proper initialization */
        uint64_t phys = (uint64_t)pmm_alloc_page();
        if (!phys) break;
        check_new_page_overlap(phys);
        heap_chunk_t *chunk = (heap_chunk_t *)phys_to_virt(phys);
        chunk->magic = HEAP_CHUNK_MAGIC;
        chunk->size = PAGE_SIZE - sizeof(heap_chunk_t);
        chunk->free = 1;
        chunk->next = 0;
        chunk->prev = 0;
        if (!heap_first) {
            heap_first = chunk;
        } else {
            heap_chunk_t *last = heap_first;
            while (last->next) last = last->next;
            if (last->free &&
                (char *)last + sizeof(heap_chunk_t) + last->size == (char *)chunk) {
                last->size += sizeof(heap_chunk_t) + chunk->size;
            } else {
                last->next = chunk;
                chunk->prev = last;
            }
        }
        heap_total_size += PAGE_SIZE;
        allocated++;
    }
    kprintf("mm: pre-seeded heap with %d pages (%lu KB)\n", allocated, (allocated * 4));
    return allocated;
}

static void split_chunk(heap_chunk_t *chunk, size_t size) {
    size_t remaining = chunk->size - size;
    if (remaining < sizeof(heap_chunk_t) + 32) return;
    heap_chunk_t *new_chunk = (heap_chunk_t *)((char *)chunk + sizeof(heap_chunk_t) + size);
    new_chunk->magic = HEAP_CHUNK_MAGIC;
    new_chunk->size = remaining - sizeof(heap_chunk_t);
    new_chunk->free = 1;
    new_chunk->next = chunk->next;
    new_chunk->prev = chunk;
    if (chunk->next) chunk->next->prev = new_chunk;
    chunk->next = new_chunk;
    chunk->size = size;
}

static void coalesce(heap_chunk_t *chunk) {
    if (chunk->next && chunk->next->free) {
        heap_chunk_t *next = chunk->next;
        if ((char *)chunk + sizeof(heap_chunk_t) + chunk->size == (char *)next) {
            if (next->magic != HEAP_CHUNK_MAGIC) return;
            chunk->size += sizeof(heap_chunk_t) + next->size;
            chunk->next = next->next;
            if (next->next) next->next->prev = chunk;
        }
    }
    if (chunk->prev && chunk->prev->free) {
        heap_chunk_t *prev = chunk->prev;
        if ((char *)prev + sizeof(heap_chunk_t) + prev->size == (char *)chunk) {
            if (prev->magic != HEAP_CHUNK_MAGIC) return;
            prev->size += sizeof(heap_chunk_t) + chunk->size;
            prev->next = chunk->next;
            if (chunk->next) chunk->next->prev = prev;
            chunk = prev;
        }
    }
    /* NOTE: pages are deliberately NOT returned to PMM here. Returning
       trailing pages while a neighboring kept partial page still holds
       live allocations lets the same physical page end up owned by both
       the general heap and the size-class allocator (double-allocation
       that corrupted the QPlatformTheme). The general heap grows
       monotonically instead; with 4G of RAM this is acceptable. */
}

/* ── Public API ── */

void *malloc(size_t size) {
    if (size == 0) return 0;

    /* Try size class first (fast path) */
    int cls = size_class_index(size);
    if (cls >= 0) {
        spin_lock(&heap_lock);
        if (!heap_inited) {
            heap_first = 0;
            heap_total_size = 0;
            heap_used_size = 0;
            heap_peak_used = 0;
            for (int i = 0; i < NUM_SIZE_CLASSES; i++)
                size_class_free[i] = 0;
            heap_inited = 1;
        }
        void *ptr = size_class_alloc(cls);
        if (ptr) {
            if (codeos_track_ptr && ptr == codeos_track_ptr) {
                kprintf("!! MALLOC-REUSED theme=%p size=%lu caller=%p\n",
                        ptr, size, __builtin_return_address(0));
            }
            heap_used_size += size_class_sizes[cls];
            if (heap_used_size > heap_peak_used) heap_peak_used = heap_used_size;
            spin_unlock(&heap_lock);
            return ptr;
        }
        /* Size class exhausted: fall back to the general heap instead of
           failing. Otherwise a hot class (e.g. 256B during Qt repaints)
           hitting SIZE_CLASS_MAX_PAGES makes `operator new` land in the
           ud2 bad_alloc trap and crash the whole kernel. */
        spin_unlock(&heap_lock);
    }

    /* General path for larger allocations (also catches exhausted classes) */
    spin_lock(&heap_lock);
    if (!heap_inited) {
        heap_first = 0;
        heap_total_size = 0;
        heap_used_size = 0;
        heap_peak_used = 0;
        for (int i = 0; i < NUM_SIZE_CLASSES; i++)
            size_class_free[i] = 0;
        heap_inited = 1;
    }
    size = (size + 15) & ~15; /* 16-byte alignment so data ptr matches Qt's MaxPrimitiveAlignment */
    heap_chunk_t *chunk = find_free_chunk(size);
    if (!chunk) {
        if (size > PAGE_SIZE - sizeof(heap_chunk_t)) {
            heap_add_large_chunk(size);
        } else {
            heap_add_page();
            chunk = find_free_chunk(size);
            if (!chunk) {
                heap_add_page();
                chunk = find_free_chunk(size);
            }
        }
        chunk = find_free_chunk(size);
    }
    if (!chunk) {
        heap_chunk_t *tl = heap_first;
        while (tl && tl->next) tl = tl->next;
        kprintf("!! MALLOC-PERMFAIL size=%lu tail=%p tailsize=%lu tailfree=%d\n",
                (unsigned long)size, (void *)tl,
                tl ? (unsigned long)tl->size : 0,
                tl ? tl->free : 0);
        pageadd_dump();
        spin_unlock(&heap_lock);
        return 0;
    }
    split_chunk(chunk, size);
    chunk->free = 0;
    if (codeos_track_ptr && (void *)((char *)chunk + sizeof(heap_chunk_t)) == codeos_track_ptr) {
        kprintf("!! MALLOC-GEN theme=%p size=%lu caller=%p\n",
                (char *)chunk + sizeof(heap_chunk_t), size,
                __builtin_return_address(0));
    }
    ring_add((uint64_t)((char *)chunk + sizeof(heap_chunk_t)), size,
             (uint64_t)__builtin_return_address(0), 1);
    heap_used_size += chunk->size;
    if (heap_used_size > heap_peak_used) heap_peak_used = heap_used_size;
    spin_unlock(&heap_lock);
    return (void *)((char *)chunk + sizeof(heap_chunk_t));
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    /* Determine old allocation size */
    size_t old_size = 0;
    spin_lock(&heap_lock);
    if (heap_inited) {
        int cls = size_class_find_page(ptr);
        if (cls >= 0) {
            old_size = size_class_sizes[cls];
        }
    }
    if (old_size == 0) {
        heap_chunk_t *chunk = (heap_chunk_t *)((char *)ptr - sizeof(heap_chunk_t));
        if (chunk->magic == HEAP_CHUNK_MAGIC)
            old_size = chunk->size;
    }
    spin_unlock(&heap_lock);

    void *new_ptr = malloc(size);
    if (!new_ptr) return 0;
    ring_add((uint64_t)new_ptr, size, (uint64_t)__builtin_return_address(0), 3);
    size_t copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return malloc(0);
    if (nmemb > (size_t)-1 / size) return 0;
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void free(void *ptr) {
    if (!ptr) return;

    spin_lock(&heap_lock);

#ifdef CODEOS_DEBUG
    uint64_t p = (uint64_t)ptr;
    if (p >= 0xffff800000200000ULL && p < 0xffff800000220000ULL)
        kprintf("!! FREE-REGION ptr=%p caller=%p\n", ptr, __builtin_return_address(0));
#endif

    /* Check if this is a size-class block first (O(1) per class × pages) */
    if (heap_inited) {
        int cls, slot, bi;
        int r = size_class_find_block(ptr, &cls, &slot, &bi);
        if (r == 0) {
            if (codeos_track_ptr && ptr == codeos_track_ptr) {
                kprintf("!! FREE-HEAP theme=%p caller=%p data[0]=%p data[8]=%p\n",
                        ptr, __builtin_return_address(0),
                        ((uint64_t *)ptr)[0], ((uint64_t *)ptr)[1]);
            }
            if (bit_get(size_class_pages[cls][slot].used_bits, bi) == 1) {
                kprintf("!! SIZE-CLASS DOUBLE-FREE ptr=%p cls=%d caller=%p\n",
                        ptr, cls, __builtin_return_address(0));
                spin_unlock(&heap_lock);
                return;
            }
            bit_set(size_class_pages[cls][slot].used_bits, bi);
            sized_block_t *block = (sized_block_t *)ptr;
            block->next = size_class_free[cls];
            size_class_free[cls] = block;
            size_class_count[cls]++;
            heap_used_size -= size_class_sizes[cls];
            spin_unlock(&heap_lock);
            return;
        }
        if (r == 1) {
            kprintf("!! SIZE-CLASS MISALIGNED-FREE ptr=%p caller=%p\n",
                    ptr, __builtin_return_address(0));
            spin_unlock(&heap_lock);
            return;
        }
    }

    /* General heap chunk */
    if (codeos_track_ptr && ptr == codeos_track_ptr) {
        kprintf("!! FREE-GEN theme=%p caller=%p data[0]=%p data[8]=%p\n",
                ptr, __builtin_return_address(0),
                ((uint64_t *)ptr)[0], ((uint64_t *)ptr)[1]);
    }
    heap_chunk_t *chunk = (heap_chunk_t *)((char *)ptr - sizeof(heap_chunk_t));
    if (chunk->magic != HEAP_CHUNK_MAGIC) {
        /* Dangling or corrupted — nothing we can do */
        spin_unlock(&heap_lock);
        return;
    }
    if (codeos_track_ptr && ptr != codeos_track_ptr) {
        uint64_t lo = (uint64_t)ptr;
        uint64_t hi = lo + chunk->size;
        uint64_t t = (uint64_t)codeos_track_ptr;
        if (t >= lo && t < hi)
            kprintf("!! FREE-GEN-CONTAINS theme=%p freed=%p chunk_size=%lu caller=%p\n",
                    codeos_track_ptr, ptr, (unsigned long)chunk->size,
                    __builtin_return_address(0));
    }
    if (heap_used_size >= chunk->size)
        heap_used_size -= chunk->size;
    ring_add((uint64_t)ptr, (uint64_t)chunk->size,
             (uint64_t)__builtin_return_address(0), 2);
    chunk->free = 1;
    coalesce(chunk);
    spin_unlock(&heap_lock);
}

size_t mm_heap_used(void) { return heap_used_size; }
size_t mm_heap_total(void) { return heap_total_size; }
size_t mm_heap_peak(void) { return heap_peak_used; }

int mm_defrag(void) {
    spin_lock(&heap_lock);
    int count = 0;
    heap_chunk_t *cur = heap_first;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            coalesce(cur);
            count++;
        }
        cur = cur->next;
    }
    spin_unlock(&heap_lock);
    return count;
}

/* Diagnostic: walk the general heap chain and report the first corrupt link. */
void heap_chain_dump(void) {
    heap_chunk_t *cur = heap_first;
    heap_chunk_t *prev_chunk = 0;
    int n = 0;
    int bad = 0;
    while (cur && n < 64) {
        int sc = size_class_find_page(cur);
        bad = 0;
        if ((uint64_t)cur < 0xffff800000200000ULL || (uint64_t)cur > 0xffffffff82000000ULL)
            bad = 1;
        if (cur->magic != HEAP_CHUNK_MAGIC)
            bad = 2;
        if (cur->next && (uint64_t)cur->next < 0xffff800000200000ULL)
            bad = 3;
        kprintf("HEAP chunk#%d=%p magic=%016lx size=%lu free=%d next=%p prev=%p scpage=%d %s\n",
                n, (void *)cur, cur->magic, (unsigned long)cur->size, cur->free,
                cur->next, cur->prev, sc,
                bad ? (bad == 1 ? "BAD-ADDR" : bad == 2 ? "BAD-MAGIC" : "BAD-NEXT") : "");
        if (bad == 3 || !cur->next)
            break;
        prev_chunk = cur;
        cur = cur->next;
        n++;
    }
    if (cur && bad == 3) {
        if (prev_chunk) {
            uint64_t plo = (uint64_t)prev_chunk + sizeof(heap_chunk_t);
            uint64_t phi = plo + prev_chunk->size;
            ring_dump(plo, (uint64_t)cur);
            kprintf("HEAP prev_chunk=%p size=%lu data=[%p,%p)\n",
                    (void *)prev_chunk, (unsigned long)prev_chunk->size,
                    (void *)plo, (void *)phi);
        }
        uint8_t *r = (uint8_t *)((uint64_t)cur & ~0xfULL);
        uint8_t *pr = (uint8_t *)(((uint64_t)prev_chunk + sizeof(heap_chunk_t)) & ~0xfULL);
        kprintf("HEAP-PREV-XDMP @%p (tail of overflowing chunk):\n", (void *)pr);
        for (int i = 0; i < 8; i++) {
            kprintf("  %p:", (void *)(pr + i * 16));
            for (int j = 0; j < 16; j++)
                kprintf(" %02x", pr[i * 16 + j]);
            kprintf("\n");
        }
        kprintf("HEAP-XDMP @%p:\n", (void *)r);
        for (int i = 0; i < 16; i++) {
            kprintf("  %p:", (void *)(r + i * 16));
            for (int j = 0; j < 16; j++)
                kprintf(" %02x", r[i * 16 + j]);
            kprintf("\n");
        }
    }
    if (n >= 64)
        kprintf("HEAP-CHAIN-DUMP: walked 64 chunks without end\n");
    else
        kprintf("HEAP-CHAIN-DUMP: walked %d chunks, last=%p\n", n, (void *)cur);
    if (cur && bad == 3) {
        /* Dump string payloads referenced by the first records after the clobber */
        {
            uint64_t refs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            int nref = 0;
            uint8_t *base = (uint8_t *)cur;
            for (int rec = 0; rec < 8 && nref < 8; rec++) {
                uint64_t *r = (uint64_t *)(base + rec * 24);
                uint64_t a = r[0];
                if (a >= 0xffff800000200000ULL && a < 0xffff800000250000ULL) {
                    int dup = 0;
                    for (int k = 0; k < nref; k++)
                        if (refs[k] == a) dup = 1;
                    if (!dup) refs[nref++] = a;
                }
            }
            for (int k = 0; k < nref; k++) {
                uint8_t *p = (uint8_t *)refs[k];
                kprintf("HEAP-STR @%p:", (void *)refs[k]);
                for (int j = 0; j < 24; j++) {
                    uint8_t c = p[j];
                    kprintf(" %02x", c);
                }
                kprintf(" | ");
                for (int j = 0; j < 24; j++) {
                    uint8_t c = p[j];
                    kprintf("%c", (c >= 32 && c < 127) ? c : '.');
                }
                kprintf("\n");
            }
        }
    }
}

void heap_debug_print(void) {
    spin_lock(&heap_lock);
    kprintf("Heap: %luB total, %luB used, %luB peak\n",
            heap_total_size, heap_used_size, heap_peak_used);
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        kprintf("  size_class[%d] (%luB): %d free blocks\n",
                i, size_class_sizes[i], size_class_count[i]);
    }
    heap_chunk_t *cur = heap_first;
    while (cur) {
        kprintf("  chunk %p: size=%lu %s next=%p prev=%p\n",
                cur, cur->size, cur->free ? "FREE" : "USED",
                cur->next, cur->prev);
        cur = cur->next;
    }
    spin_unlock(&heap_lock);
}

/* ---- e820 memory map ---- */

typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;
} e820_entry_t;

extern uint8_t _kernel_end[];

static uint64_t mem_total_kb;
static uint64_t mem_free_kb;

void mm_init(uint32_t mem_lower, uint32_t mem_upper, uint32_t mmap_addr, uint32_t mmap_length) {
    mem_total_kb = 0;
    mem_free_kb = 0;
    int region_count = 0;

    kprintf("Memory regions:\n");

    if (mmap_addr && mmap_length) {
        uint32_t addr = mmap_addr;
        while (addr < mmap_addr + mmap_length) {
            e820_entry_t *e = (e820_entry_t *)(uint64_t)addr;
            uint64_t start = e->base;
            uint64_t end = e->base + e->length;
            uint64_t size_kb = e->length / 1024;
            mem_total_kb += size_kb;
            if (e->type == 1)
                mem_free_kb += size_kb;

            kprintf("  %016lx - %016lx  %s (%llu KB)\n",
                    start, end,
                    e->type == 1 ? "free" : "reserved",
                    size_kb);

            addr += sizeof(e820_entry_t);
            region_count++;
        }
    } else {
        mem_total_kb = mem_lower + mem_upper;
        mem_free_kb = mem_upper;
        kprintf("  (using multiboot mem_upper: %u KB)\n", mem_upper);
    }

    kprintf("Total: %llu KB, Free: %llu KB (%d regions)\n\n",
            mem_total_kb, mem_free_kb, region_count);
}

uint64_t mm_total(void) { return mem_total_kb; }
uint64_t mm_free(void) { return mem_free_kb; }
uint64_t mm_used(void) { return mem_total_kb - mem_free_kb; }

void mm_print_regions(void) {
    kprintf("Memory: %llu KB total, %llu KB free, %llu KB used\n",
            mem_total_kb, mem_free_kb, mem_total_kb - mem_free_kb);
}
