#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include "types.h"

int      virtio_gpu_init(void);
int      virtio_gpu_available(void);
void     virtio_gpu_get_size(int *w, int *h);
void    *virtio_gpu_get_framebuffer(void);
uint64_t virtio_gpu_get_framebuffer_phys(void);
void     virtio_gpu_flush(void);
int      virtio_gpu_get_bpp(void);

#endif
