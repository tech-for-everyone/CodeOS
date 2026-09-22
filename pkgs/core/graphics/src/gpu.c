#include "gpu.h"
#include "virtio_gpu.h"
#include "arch/x86_64/fb.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"

static gpu_backend_t active_backend = GPU_BACKEND_NONE;

gpu_backend_t gpu_init(void) {
    /* Prefer a framebuffer already supplied by the bootloader.  Probing a
     * second display device after Limine has initialized the screen can make
     * the backend and the visible framebuffer disagree. */
    if (fb_getwidth() > 0 && fb_getheight() > 0) {
        active_backend = GPU_BACKEND_LIMINE;
        kprintf("GPU: Limine GOP %ux%u\n", fb_getwidth(), fb_getheight());
        return active_backend;
    }

    /* Virtio-GPU is the fallback when no bootloader framebuffer exists. */
    if (virtio_gpu_init() == 0 && virtio_gpu_available()) {
        active_backend = GPU_BACKEND_VIRTIO;
        int w, h;
        virtio_gpu_get_size(&w, &h);
        kprintf("GPU: Virtio-GPU %dx%d\n", w, h);
        return active_backend;
    }

    active_backend = GPU_BACKEND_NONE;
    kprintf("GPU: no display backend found\n");
    return active_backend;
}

void *gpu_get_framebuffer(void) {
    switch (active_backend) {
        case GPU_BACKEND_VIRTIO:  return virtio_gpu_get_framebuffer();
        case GPU_BACKEND_LIMINE:
        case GPU_BACKEND_PCI_VBE: return (void *)fb_get_active_buffer();
        default: return 0;
    }
}

uint64_t gpu_get_framebuffer_phys(void) {
    switch (active_backend) {
        case GPU_BACKEND_VIRTIO:  return virtio_gpu_get_framebuffer_phys();
        case GPU_BACKEND_LIMINE:
        case GPU_BACKEND_PCI_VBE: return fb_get_addr_phys();
        default: return 0;
    }
}

uint32_t gpu_get_width(void) {
    switch (active_backend) {
        case GPU_BACKEND_VIRTIO: { int w, h; virtio_gpu_get_size(&w, &h); return (uint32_t)w; }
        case GPU_BACKEND_LIMINE:
        case GPU_BACKEND_PCI_VBE: return fb_getwidth();
        default: return 0;
    }
}

uint32_t gpu_get_height(void) {
    switch (active_backend) {
        case GPU_BACKEND_VIRTIO: { int w, h; virtio_gpu_get_size(&w, &h); return (uint32_t)h; }
        case GPU_BACKEND_LIMINE:
        case GPU_BACKEND_PCI_VBE: return fb_getheight();
        default: return 0;
    }
}

uint32_t gpu_get_pitch(void) {
    switch (active_backend) {
        case GPU_BACKEND_VIRTIO: {
            int w, h;
            virtio_gpu_get_size(&w, &h);
            return (uint32_t)w * 4;
        }
        case GPU_BACKEND_LIMINE:
        case GPU_BACKEND_PCI_VBE: return (uint32_t)fb_get_pitch();
        default: return 0;
    }
}

uint8_t gpu_get_bpp(void) {
    switch (active_backend) {
        case GPU_BACKEND_VIRTIO:  return 32;
        case GPU_BACKEND_LIMINE:
        case GPU_BACKEND_PCI_VBE: return fb_get_bpp();
        default: return 0;
    }
}

void gpu_flush(void) {
    if (active_backend == GPU_BACKEND_VIRTIO)
        virtio_gpu_flush();
    /* Limine/VBE framebuffers are directly visible — no flush needed */
}

int gpu_set_mode(uint32_t width, uint32_t height) {
    (void)width; (void)height;
    /* TODO: Virtio-GPU can support resolution changes via RESOURCE_CREATE_2D */
    return -1;
}

const char *gpu_backend_name(void) {
    switch (active_backend) {
        case GPU_BACKEND_VIRTIO:   return "virtio-gpu";
        case GPU_BACKEND_LIMINE:   return "limine-gop";
        case GPU_BACKEND_PCI_VBE:  return "pci-vbe";
        default:                   return "none";
    }
}
