#include "../limine.h"
#include "kprintf.h"
#include "../arch/x86_64/fb.h"
#include "mm.h"
#include "vmm.h"

extern void _start_limine(void);

struct limine_requests_pack {
    uint64_t base_revision[3];
    uint64_t start_marker[4];
    struct limine_framebuffer_request fb;
    struct limine_memmap_request mmap;
    struct limine_hhdm_request hhdm;
    struct limine_rsdp_request rsdp;
    struct limine_entry_point_request entry;
    struct limine_executable_cmdline_request cmdline;
    uint64_t end_marker[2];
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_requests_pack limine_requests = {
    .base_revision = { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 0 },
    .start_marker  = { 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf,
                       0x785c6ed015d3e316, 0x181e920a7852b9d9 },
    .fb = {
        .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
        .revision = 0,
        .response = NULL,
    },
    .mmap = {
        .id = LIMINE_MEMMAP_REQUEST_ID,
        .revision = 0,
        .response = NULL,
    },
    .hhdm = {
        .id = LIMINE_HHDM_REQUEST_ID,
        .revision = 0,
        .response = NULL,
    },
    .rsdp = {
        .id = LIMINE_RSDP_REQUEST_ID,
        .revision = 0,
        .response = NULL,
    },
    .entry = {
        .id = LIMINE_ENTRY_POINT_REQUEST_ID,
        .revision = 0,
        .response = NULL,
        .entry = &_start_limine,
    },
    .cmdline = {
        .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
        .revision = 0,
        .response = NULL,
    },
    .end_marker = { 0xadc0e0531bb10d03, 0x9572709f31764c62 },
};

int limine_booted(void) {
    /* A framebuffer response may be absent (e.g. QEMU without a VGA device),
     * but the hhdm/mmap responses are always provided once we are running
     * under Limine. */
    return limine_requests.hhdm.response != NULL ||
           limine_requests.mmap.response != NULL;
}

uint64_t limine_hhdm_offset(void) {
    if (limine_requests.hhdm.response)
        return limine_requests.hhdm.response->offset;
    return 0;
}

/* Find the physical range occupied by the kernel image + modules.
 * Returns 0 on success, -1 if unavailable. */
int limine_kernel_phys_range(uint64_t *start, uint64_t *end) {
    if (!limine_booted() || !limine_requests.mmap.response)
        return -1;

    *start = ~0ULL;
    *end = 0;
    for (uint64_t i = 0; i < limine_requests.mmap.response->entry_count; i++) {
        struct limine_memmap_entry *e = limine_requests.mmap.response->entries[i];
        if (e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            if (e->base < *start) *start = e->base;
            if (e->base + e->length > *end) *end = e->base + e->length;
        }
    }
    return (*start == ~0ULL) ? -1 : 0;
}

int limine_setup_fb(void) {
    if (!limine_booted()) return -1;
    if (!limine_requests.fb.response ||
        limine_requests.fb.response->framebuffer_count == 0 ||
        limine_requests.fb.response->framebuffers == NULL)
        return -1;

    struct limine_framebuffer *fb = limine_requests.fb.response->framebuffers[0];
    kprintf("Limine: framebuffer %lux%lu %ubpp at 0x%p\n",
            fb->width, fb->height, fb->bpp, fb->address);

    fb_init(virt_to_phys((uint64_t)fb->address), fb->width, fb->height,
            fb->pitch, fb->bpp,
            fb->memory_model == LIMINE_FRAMEBUFFER_RGB ? 0 : 1);
    kprintf_set_output(fb_putchar, fb_clear);
    fb_cursor_init();
    fb_cursor_show();
    kprintf("Display: Framebuffer (Limine, %lux%lu)\n", fb->width, fb->height);
    return 0;
}

static uint64_t limine_memmap_total(void) {
    if (!limine_booted()) return 0;

    uint64_t total = 0;
    for (uint64_t i = 0; i < limine_requests.mmap.response->entry_count; i++) {
        struct limine_memmap_entry *e = limine_requests.mmap.response->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE)
            total += e->length;
    }
    return total;
}

void limine_setup_mm(uint64_t *mem_size) {
    if (!limine_booted()) return;

    uint64_t total_usable = limine_memmap_total();
    uint64_t total_kb = total_usable / 1024;

    kprintf("Limine: usable memory %llu KB\n", total_kb);
    mm_init(0, (uint32_t)(total_kb > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : total_kb), 0, 0);

    *mem_size = total_usable;
}

const char *limine_get_cmdline(void) {
    if (!limine_booted() || !limine_requests.cmdline.response)
        return "";
    return limine_requests.cmdline.response->cmdline;
}
