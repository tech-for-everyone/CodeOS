#ifndef GPU_H
#define GPU_H

#include "types.h"

/*
 * GPU abstraction layer — unified interface over multiple backends:
 *   1. Virtio-GPU  (QEMU -device virtio-gpu-pci)
 *   2. Limine GOP  (framebuffer provided by Limine bootloader)
 *   3. PCI VBE/HDMI (direct VGA modeset fallback)
 *
 * The active backend is selected at boot by gpu_init().
 */

typedef enum {
    GPU_BACKEND_NONE = 0,
    GPU_BACKEND_VIRTIO,
    GPU_BACKEND_LIMINE,
    GPU_BACKEND_PCI_VBE,
} gpu_backend_t;

/* Initialize GPU subsystem — probes backends in priority order.
 * Returns the selected backend (GPU_BACKEND_NONE if no display found). */
gpu_backend_t gpu_init(void);

/* Framebuffer access */
void    *gpu_get_framebuffer(void);       /* virtual address */
uint64_t gpu_get_framebuffer_phys(void);  /* physical address */
uint32_t gpu_get_width(void);
uint32_t gpu_get_height(void);
uint32_t gpu_get_pitch(void);             /* bytes per scanline */
uint8_t  gpu_get_bpp(void);

/* Flush pending changes to the display (for double-buffered / virtio) */
void gpu_flush(void);

/* Change display mode (not yet supported on all backends).
 * Returns 0 on success, -1 if unsupported. */
int gpu_set_mode(uint32_t width, uint32_t height);

/* Query backend name */
const char *gpu_backend_name(void);

#endif
