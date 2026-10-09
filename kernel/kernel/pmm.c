#include "pmm.h"
#include "kprintf.h"
#include "spinlock.h"
#include "string.h"
#include "vmm.h"

#define MAX_PAGES (4 * 1024 * 1024)
#define BITMAP_SIZE (MAX_PAGES / 8)

/* ── Free stack for O(1) single-page allocation ── */
#define FREE_STACK_SIZE MAX_PAGES
static uint64_t free_stack[FREE_STACK_SIZE];
static uint64_t free_sp;          /* stack pointer (index into free_stack) */

static uint8_t bitmap[BITMAP_SIZE] __attribute__((aligned(4096)));
static uint64_t total_page_count;
static uint64_t first_free_page;
static uint64_t free_page_count;
static spinlock_t pmm_lock = SPINLOCK_INIT;

static inline void bitmap_set(size_t i) {
    if (i >= total_page_count) return;
    bitmap[i / 8] |= (1u << (i % 8));
}

static inline void bitmap_clear(size_t i) {
    if (i >= total_page_count) return;
    bitmap[i / 8] &= ~(1u << (i % 8));
}

static inline int bitmap_test(size_t i) {
    if (i >= total_page_count) return 1;
    return (bitmap[i / 8] >> (i % 8)) & 1;
}

static size_t addr_to_page(uint64_t addr) {
    return addr >> PAGE_SHIFT;
}

static uint64_t page_to_addr(size_t page) {
    return (uint64_t)page << PAGE_SHIFT;
}

extern uint64_t phys_base;
int limine_kernel_phys_range(uint64_t *start, uint64_t *end);

/* ── Free stack push/pop for O(1) alloc ── */
static inline void free_stack_push(size_t page) {
    if (free_sp < FREE_STACK_SIZE)
        free_stack[free_sp++] = page;
}

static inline int free_stack_pop(size_t *page) {
    if (free_sp == 0) return 0;
    *page = free_stack[--free_sp];
    return 1;
}

/* ── Bitmap scan helpers for multi-page allocation ── */

/* Skip to next free bit starting from position `pos`.
 * Returns the index of the next free bit, or total_page_count if none found. */
static size_t bitmap_find_free(size_t pos) {
    while (pos < total_page_count) {
        /* Check byte at a time — 8 bits per byte */
        size_t byte_idx = pos / 8;
        if (byte_idx >= BITMAP_SIZE) break;
        uint8_t b = bitmap[byte_idx];
        if (b != 0xFF) {
            /* At least one free bit in this byte */
            int bit = __builtin_ffs(~b);
            size_t found = byte_idx * 8 + (bit - 1);
            if (found < total_page_count && found >= pos)
                return found;
        }
        /* Jump to next byte boundary */
        pos = (byte_idx + 1) * 8;
    }
    return total_page_count;
}

/* ── Init ── */

void pmm_init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end) {
    spin_lock(&pmm_lock);

    total_page_count = mem_size / PAGE_SIZE;
    if (total_page_count > MAX_PAGES) total_page_count = MAX_PAGES;

    memset(bitmap, 0, BITMAP_SIZE);
    free_sp = 0;
    free_page_count = 0;

    uint64_t phys_kstart, phys_kend;
    if (limine_kernel_phys_range(&phys_kstart, &phys_kend) < 0) {
        /* Fallback (GRUB/multiboot honours p_paddr): kernel at ELF phys base */
        phys_kstart = kernel_start + phys_base;
        phys_kend   = kernel_end   + phys_base;
    }

    /* Protect legacy low memory (BIOS/EBDA); cap at 2 MB so a kernel loaded
     * high by Limine doesn't reserve the whole low address space. */
    size_t low_end = addr_to_page(phys_kstart < 0x200000 ? phys_kstart : 0x200000);
    for (size_t i = 0; i < low_end && i < total_page_count; i++)
        bitmap_set(i);

    size_t kstart = addr_to_page(phys_kstart);
    size_t kend   = addr_to_page(phys_kend + PAGE_SIZE - 1);
    if (kend > total_page_count) kend = total_page_count;

    for (size_t i = kstart; i < kend; i++)
        bitmap_set(i);

    first_free_page = kend < total_page_count ? kend : total_page_count;

    /* Build free stack: push all free pages (reversed so lowest addresses come first) */
    for (size_t i = total_page_count; i > 0; ) {
        i--;
        if (!bitmap_test(i))
            free_stack_push(i);
    }
    free_page_count = free_sp;

    spin_unlock(&pmm_lock);

    kprintf("PMM: %lu pages total (%lu MB), %lu free (%lu MB), kernel=%lu-%lu (%lu pages)\n",
            total_page_count, total_page_count * 4096 / 1024 / 1024,
            free_page_count, free_page_count * 4096 / 1024 / 1024,
            kstart, kend, kend > kstart ? kend - kstart : 0);
    kprintf("PMM: free_stack=%lu KB, bitmap=%lu KB, BSS overhead=%lu pages\n",
            (uint64_t)(sizeof(free_stack)) / 1024,
            (uint64_t)BITMAP_SIZE / 1024,
            kend > kstart ? kend - kstart : 0);
}

/* ── O(1) single-page allocation via free stack ── */

uint64_t pmm_alloc_page(void) {
    spin_lock(&pmm_lock);
    size_t page;
    /* The free stack can hold stale entries: pmm_alloc_pages() marks pages
       allocated in the bitmap but never removes them from the stack, so a
       page may still be on the stack while already owned (e.g. by the general
       heap). Popping it again would hand the same page to a second allocator
       and corrupt the heap — re-check the bitmap and skip stale entries. */
    while (free_stack_pop(&page)) {
        if (bitmap_test(page))
            continue;
        bitmap_set(page);
        free_page_count--;
        spin_unlock(&pmm_lock);

        return page_to_addr(page);
    }
    spin_unlock(&pmm_lock);
    kprintf("PMM: alloc_page FAILED sp=%lu free=%lu total=%lu\n",
            free_sp, free_page_count, total_page_count);
    return 0;
}

/* ── Multi-page allocation with bitmap search ── */

uint64_t pmm_alloc_pages(size_t n) {
    if (n == 0) return 0;
    if (n == 1) return pmm_alloc_page();

    spin_lock(&pmm_lock);

    /* For small allocations, pop contiguous pages from the free stack so the
     * same frames a later alloc_pages() run would take from the bitmap are
     * also removed from the stack — the bitmap bit alone isn't enough, since
     * a stray free of that range would clear it and hand the same frame to a
     * second owner (e.g. a user allocation aliasing a kernel syscall stack). */
    if (n <= 4) {
        size_t cand[4];
        size_t got = 0;
        /* Peek-gather up to n contiguous free frames from the stack without
         * disturbing the ones we don't end up using. */
        while (got < n && free_sp > 0) {
            size_t page = free_stack[free_sp - 1];
            if (bitmap_test(page)) { free_sp--; continue; }
            /* Require the candidate to extend the run contiguously upward. */
            if (got == 0 || page == cand[got - 1] + 1) {
                cand[got++] = page;
                free_sp--;
            } else {
                break;
            }
        }
        if (got == n) {
            for (size_t j = 0; j < n; j++) {
                bitmap_set(cand[j]);
                free_page_count--;
            }
            if (cand[0] >= first_free_page)
                first_free_page = cand[0] + n;
            spin_unlock(&pmm_lock);
            return page_to_addr(cand[0]);
        }
        /* Partial run — restore what we popped but didn't keep. */
        while (got > 0) free_stack_push(cand[--got]);

        /* Fall back to bitmap scan (large sparse regions). */
        size_t pos = 0;
        while (pos < total_page_count) {
            size_t start = bitmap_find_free(pos);
            if (start + n > total_page_count) break;

            /* Check if n contiguous pages are free */
            int found = 1;
            for (size_t j = 0; j < n; j++) {
                if (bitmap_test(start + j)) { found = 0; break; }
            }
            if (found) {
                for (size_t j = 0; j < n; j++) {
                    bitmap_set(start + j);
                    free_page_count--;
                }
                if (start >= first_free_page)
                    first_free_page = start + n;
                spin_unlock(&pmm_lock);

                return page_to_addr(start);
            }
            pos = start + 1;
        }
    } else {
        /* Large allocation: scan bitmap with streak tracking */
        size_t streak = 0;
        size_t streak_start = 0;
        for (size_t i = 0; i < total_page_count; i++) {
            if (!bitmap_test(i)) {
                if (streak == 0) streak_start = i;
                streak++;
                if (streak == n) {
                    for (size_t j = streak_start; j < streak_start + n; j++) {
                        bitmap_set(j);
                        free_page_count--;
                    }
                    if (streak_start + n > first_free_page)
                        first_free_page = streak_start + n;
                    spin_unlock(&pmm_lock);
                    return page_to_addr(streak_start);
                }
            } else {
                streak = 0;
            }
        }
    }

    spin_unlock(&pmm_lock);
    kprintf("PMM: alloc_pages(%lu) FAILED, free=%lu total=%lu\n",
            n, free_page_count, total_page_count);
    return 0;
}

/* ── Free pages — push back onto free stack ── */

void pmm_free_page(uint64_t vma) {
    if (!vma) return;
    size_t i = addr_to_page(vma);
    if (i >= total_page_count) return;
    spin_lock(&pmm_lock);
    if (!bitmap_test(i)) {
        /* Already free — double free. Do NOT push again or the free stack
           accumulates duplicates and the same page gets allocated twice. */
        spin_unlock(&pmm_lock);
        return;
    }
    bitmap_clear(i);
    free_stack_push(i);
    free_page_count++;
    if (i < first_free_page) first_free_page = i;
    spin_unlock(&pmm_lock);
}

void pmm_free_pages(uint64_t vma, size_t n) {
    if (!vma || n == 0) return;
    size_t start = addr_to_page(vma);
    if (start + n > total_page_count) n = total_page_count - start;
    spin_lock(&pmm_lock);
    size_t freed = 0;
    for (size_t i = start; i < start + n; i++) {
        if (bitmap_test(i)) {
            bitmap_clear(i);
            free_stack_push(i);
            freed++;
        }
    }
    free_page_count += freed;
    if (freed && start < first_free_page) first_free_page = start;
    spin_unlock(&pmm_lock);
}

int pmm_is_page_allocated(uint64_t vma) {
    if (!vma) return 0;
    size_t i = addr_to_page(vma);
    if (i >= total_page_count) return 0;
    return bitmap_test(i);
}

uint64_t pmm_total_pages(void) { return total_page_count; }

uint64_t pmm_count_free(void) {
    spin_lock(&pmm_lock);
    uint64_t count = free_page_count;
    spin_unlock(&pmm_lock);
    return count;
}

uint64_t pmm_count_used(void) { return total_page_count - pmm_count_free(); }
