#include "virtio_gpu.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/pci.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"
#include "kernel/vmm.h"
#include "kernel/pmm.h"

/* ── PCI discovery ── */
#define VIRTIO_GPU_VEND  0x1AF4
#define VIRTIO_GPU_DEV   0x1040

static uint8_t  gpu_bus, gpu_slot, gpu_func;
static uint16_t gpu_iobase;
static int      gpu_ok;

/* ── Virtio ring structures (same as virtio-net) ── */

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

/* ── Control queue (single virtqueue) ── */
#define VQ_CTL 0

static int vq_size;
static struct vring_desc  *desc;
static struct vring_avail *avail;
static struct vring_used *used;
static int vq_free_head;

/* ── Framebuffer ── */
static uint32_t *gpu_fb;       /* virtual address of framebuffer */
static uint64_t  gpu_fb_phys;  /* physical address */
static uint32_t  gpu_fb_pages; /* pages allocated */
static uint32_t  gpu_w, gpu_h;
static uint32_t  gpu_pitch;
static uint32_t  gpu_res_id = 1;

/* ── I/O port helpers (legacy virtio PCI) ── */

static inline uint32_t gpu_readl(uint16_t off) { return inl(gpu_iobase + off); }
static inline void     gpu_writel(uint16_t off, uint32_t v) { outl(gpu_iobase + off, v); }
static inline uint16_t gpu_readw(uint16_t off) { return inw(gpu_iobase + off); }
static inline void     gpu_writew(uint16_t off, uint16_t v) { outw(gpu_iobase + off, v); }
static inline uint8_t  gpu_readb(uint16_t off) { return inb(gpu_iobase + off); }
static inline void     gpu_writeb(uint16_t off, uint8_t v) { outb(gpu_iobase + off, v); }

/* ── Device status ── */

static void gpu_set_status(uint8_t s) { gpu_writeb(0x12, s); }
static uint8_t gpu_get_status(void)   { return gpu_readb(0x12); }

static void gpu_reset(void) {
    gpu_set_status(0);
    for (volatile int i = 0; i < 10000; i++) asm volatile("pause");
}

/* ── Virtqueue helpers ── */

static void vq_notify(void) { gpu_writew(0x10, VQ_CTL); }

static int vq_alloc_desc(void) {
    if (vq_free_head >= vq_size) return -1;
    int d = vq_free_head;
    vq_free_head = desc[d].next;
    desc[d].next = 0xFFFF;
    return d;
}

static void vq_free_desc(int d) {
    desc[d].next = vq_free_head;
    vq_free_head = d;
}

static void vq_submit(int head) {
    int idx = avail->idx & (vq_size - 1);
    avail->ring[idx] = head;
    __sync_synchronize();
    avail->idx++;
    __sync_synchronize();
    vq_notify();
}

/* Wait for device to consume a request and write back the response.
 * Returns 0 on success (response type field matches). */
static int vq_wait_resp(uint16_t used_before) {
    /* Spin-wait for used ring to advance (device completed a request) */
    for (int tries = 0; tries < 200000; tries++) {
        __sync_synchronize();
        if (used->idx != used_before) break;
        asm volatile("pause");
    }
    __sync_synchronize();
    return used->idx != used_before ? 0 : -1;
}

/* ── Virtqueue setup ── */

static int vq_setup(void) {
    gpu_writew(0x0E, VQ_CTL);  /* QUEUE_SEL */

    int sz = gpu_readw(0x0C);  /* QUEUE_NUM */
    if (sz == 0 || sz > 1024) return -1;
    /* Round down to power of 2 */
    if (sz & (sz - 1)) {
        int p = 1;
        while (p < sz) p <<= 1;
        sz = p >> 1;
        if (sz < 2) return -1;
    }

    int desc_bytes = sz * sizeof(struct vring_desc);
    int avail_bytes = 6 + 2 * sz;
    int used_bytes  = 6 + 8 * sz;
    int used_offset = (desc_bytes + avail_bytes + 4095) & ~4095;
    int total = used_offset + used_bytes;
    int pages = (total + 4095) / 4096;

    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) return -1;
    memset((void *)phys_to_virt(phys), 0, pages * 4096);

    desc  = (struct vring_desc  *)phys_to_virt(phys);
    avail = (struct vring_avail *)(phys_to_virt(phys) + desc_bytes);
    used  = (struct vring_used  *)(phys_to_virt(phys) + used_offset);
    vq_size = sz;
    vq_free_head = 0;

    for (int i = 0; i < sz - 1; i++) {
        desc[i].addr = 0; desc[i].len = 0;
        desc[i].flags = 0; desc[i].next = i + 1;
    }
    desc[sz - 1].next = 0xFFFF;

    gpu_writel(0x08, (uint32_t)(phys >> 12)); /* QUEUE_PFN */
    return sz;
}

/* ── Virtio-GPU control structures ── */

#define VIRTIO_GPU_F_VIRGL        (1 << 1)
#define VIRTIO_GPU_F_EDID         (1 << 2)

/* Control header */
struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

/* Command types */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO   0x100
#define VIRTIO_GPU_CMD_RES_CREATE_2D      0x101
#define VIRTIO_GPU_CMD_RES_UNREF          0x102
#define VIRTIO_GPU_CMD_SET_SCANOUT        0x103
#define VIRTIO_GPU_CMD_RES_FLUSH          0x104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST   0x105
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST 0x106
#define VIRTIO_GPU_CMD_RES_ATTACH_BACKING 0x107
#define VIRTIO_GPU_CMD_RES_DETACH_BACKING 0x108

#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO   0x1100
#define VIRTIO_GPU_RESP_OK_NODATA         0x1101

/* Formats */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  1

/* Rect */
struct virtio_gpu_rect {
    uint32_t x, y, width, height;
};

/* Display info response */
struct virtio_gpu_display_one {
    struct virtio_gpu_rect rect;
    uint32_t enabled;
    uint32_t flags;
};

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[16]; /* up to 16 scanouts */
};

/* Resource create 2D */
struct virtio_gpu_res_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

/* Resource attach backing */

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
};

struct virtio_gpu_res_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    /* followed by nr_entries x virtio_gpu_mem_entry */
};

/* Set scanout */
struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect dst_rect;
    uint32_t scanout_id;
    uint32_t resource_id;
};

/* Transfer to host */
struct virtio_gpu_transfer {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect rect;
    uint32_t offset;
    uint32_t resource_id;
    uint32_t padding;
};

/* Response header (just the hdr is enough — we check hdr.type) */
struct virtio_gpu_resp {
    struct virtio_gpu_ctrl_hdr hdr;
    uint8_t data[64];
} __attribute__((packed));

/* ── Send a command (request + inline data) and collect response.
 * The request and response share descriptors in a chain:
 *   desc[0]: request (host→device)
 *   desc[1]: inline data (host→device), e.g. mem_entry array
 *   desc[2]: response (device→host, WRITE flag)
 * Or just desc[0]+desc[2] if no extra data. ── */

static int gpu_cmd(void *req, int req_len,
                   void *extra, int extra_len,
                   void *resp, int resp_len) {
    int d0 = vq_alloc_desc();
    if (d0 < 0) return -1;
    int d1 = -1;

    desc[d0].addr  = virt_to_phys((uint64_t)(uintptr_t)req);
    desc[d0].len   = req_len;
    desc[d0].flags = 0;  /* device reads */
    desc[d0].next  = 0xFFFF;

    int last = d0;

    /* Optional extra data descriptor */
    if (extra && extra_len > 0) {
        d1 = vq_alloc_desc();
        if (d1 < 0) { vq_free_desc(d0); return -1; }
        desc[d1].addr  = virt_to_phys((uint64_t)(uintptr_t)extra);
        desc[d1].len   = extra_len;
        desc[d1].flags = 0;  /* device reads */
        desc[d1].next  = 0xFFFF;
        desc[last].next = d1;
        desc[last].flags |= 0x01;  /* NEXT */
        last = d1;
    }

    /* Response descriptor */
    int dr = vq_alloc_desc();
    if (dr < 0) {
        if (d1 >= 0) vq_free_desc(d1);
        vq_free_desc(d0);
        return -1;
    }
    desc[dr].addr  = virt_to_phys((uint64_t)(uintptr_t)resp);
    desc[dr].len   = resp_len;
    desc[dr].flags = 0x02;  /* WRITE (device writes) */
    desc[dr].next  = 0xFFFF;
    desc[last].next = dr;
    desc[last].flags |= 0x01;  /* NEXT */

    uint16_t used_before = used->idx;
    vq_submit(d0);
    int result = vq_wait_resp(used_before);

    /* Free response desc, then the chain */
    vq_free_desc(dr);
    if (d1 >= 0) vq_free_desc(d1);
    vq_free_desc(d0);

    return result;
}

/* ── High-level GPU commands ── */

static int gpu_get_display_info(struct virtio_gpu_resp_display_info *info) {
    struct virtio_gpu_ctrl_hdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    memset(info, 0, sizeof(*info));

    /* Zero response to detect success */
    struct virtio_gpu_resp *r = (struct virtio_gpu_resp *)info;
    memset(r, 0, sizeof(*info));

    if (gpu_cmd(&hdr, sizeof(hdr), 0, 0, info, sizeof(*info)) < 0)
        return -1;
    return ((struct virtio_gpu_ctrl_hdr *)info)->type ==
           VIRTIO_GPU_RESP_OK_DISPLAY_INFO ? 0 : -1;
}

static int gpu_create_2d(uint32_t res_id, uint32_t w, uint32_t h) {
    struct virtio_gpu_res_create_2d cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type       = VIRTIO_GPU_CMD_RES_CREATE_2D;
    cmd.resource_id    = res_id;
    cmd.format         = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    cmd.width          = w;
    cmd.height         = h;

    struct virtio_gpu_resp resp;
    memset(&resp, 0, sizeof(resp));
    if (gpu_cmd(&cmd, sizeof(cmd), 0, 0, &resp, sizeof(resp)) < 0)
        return -1;
    return resp.hdr.type == VIRTIO_GPU_RESP_OK_NODATA ? 0 : -1;
}

static int gpu_attach_backing(uint32_t res_id, uint64_t phys_addr, uint32_t length) {
    struct {
        struct virtio_gpu_res_attach_backing cmd;
        struct virtio_gpu_mem_entry entry;
    } __attribute__((packed)) pkt;

    memset(&pkt, 0, sizeof(pkt));
    pkt.cmd.hdr.type      = VIRTIO_GPU_CMD_RES_ATTACH_BACKING;
    pkt.cmd.resource_id   = res_id;
    pkt.cmd.nr_entries    = 1;
    pkt.entry.addr        = phys_addr;
    pkt.entry.length      = length;

    struct virtio_gpu_resp resp;
    memset(&resp, 0, sizeof(resp));
    if (gpu_cmd(&pkt, sizeof(pkt), 0, 0, &resp, sizeof(resp)) < 0)
        return -1;
    return resp.hdr.type == VIRTIO_GPU_RESP_OK_NODATA ? 0 : -1;
}

static int gpu_set_scanout(uint32_t res_id, uint32_t scanout, uint32_t w, uint32_t h) {
    struct virtio_gpu_set_scanout cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type     = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd.scanout_id   = scanout;
    cmd.resource_id  = res_id;
    cmd.dst_rect.x   = 0;
    cmd.dst_rect.y   = 0;
    cmd.dst_rect.width  = w;
    cmd.dst_rect.height = h;

    struct virtio_gpu_resp resp;
    memset(&resp, 0, sizeof(resp));
    if (gpu_cmd(&cmd, sizeof(cmd), 0, 0, &resp, sizeof(resp)) < 0)
        return -1;
    return resp.hdr.type == VIRTIO_GPU_RESP_OK_NODATA ? 0 : -1;
}

static int gpu_transfer_to_host(uint32_t res_id, uint32_t w, uint32_t h) {
    struct virtio_gpu_transfer cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type   = VIRTIO_GPU_CMD_TRANSFER_TO_HOST;
    cmd.resource_id = res_id;
    cmd.rect.x = 0; cmd.rect.y = 0;
    cmd.rect.width = w; cmd.rect.height = h;
    cmd.offset = 0;

    struct virtio_gpu_resp resp;
    memset(&resp, 0, sizeof(resp));
    if (gpu_cmd(&cmd, sizeof(cmd), 0, 0, &resp, sizeof(resp)) < 0)
        return -1;
    return resp.hdr.type == VIRTIO_GPU_RESP_OK_NODATA ? 0 : -1;
}

/* ── Flush entire framebuffer to screen ── */
void virtio_gpu_flush(void) {
    if (!gpu_ok) return;
    gpu_transfer_to_host(gpu_res_id, gpu_w, gpu_h);
}

/* ── Get framebuffer pointer (virtual) ── */
void *virtio_gpu_get_framebuffer(void) {
    return gpu_fb ? (void *)gpu_fb : 0;
}

uint64_t virtio_gpu_get_framebuffer_phys(void) {
    return gpu_fb_phys;
}

void virtio_gpu_get_size(int *w, int *h) {
    *w = (int)gpu_w;
    *h = (int)gpu_h;
}

int virtio_gpu_get_bpp(void) { return 32; }

/* ── Init ── */

int virtio_gpu_init(void) {
    /* Discover the virtio-gpu PCI device */
    if (!pci_find_device(VIRTIO_GPU_VEND, VIRTIO_GPU_DEV,
                         &gpu_bus, &gpu_slot, &gpu_func)) {
        kprintf("virtio-gpu: not found\n");
        return -1;
    }

    uint32_t bar0 = pci_config_read(gpu_bus, gpu_slot, gpu_func, 0x10);
    if (bar0 & 1) {
        gpu_iobase = (uint16_t)(bar0 & ~0x3);
    } else {
        gpu_iobase = (uint16_t)(bar0 & 0xF);
    }

    /* Enable bus master + IO space */
    pci_config_write(gpu_bus, gpu_slot, gpu_func, 0x04, 0x0007);

    kprintf("virtio-gpu: found at %02x:%02x.%d iobase=0x%04x\n",
            gpu_bus, gpu_slot, gpu_func, gpu_iobase);

    gpu_reset();
    gpu_set_status(1); /* ACK */
    gpu_set_status(1 | 2); /* ACK | DRIVER */

    /* Negotiate features — we don't need virgl or EDID */
    uint32_t feats = gpu_readl(0x00);
    gpu_writel(0x04, feats & 0); /* accept no optional features */
    gpu_set_status(1 | 2 | 8); /* + FEATURES_OK */
    if (!(gpu_get_status() & 8)) {
        kprintf("virtio-gpu: feature negotiation failed\n");
        gpu_set_status(128);
        return -1;
    }

    if (vq_setup() < 0) {
        kprintf("virtio-gpu: control queue setup failed\n");
        gpu_set_status(128);
        return -1;
    }

    gpu_set_status(1 | 2 | 8 | 4); /* + DRIVER_OK */

    /* Query display info */
    struct virtio_gpu_resp_display_info info;
    if (gpu_get_display_info(&info) < 0) {
        kprintf("virtio-gpu: display info request failed\n");
        gpu_set_status(128);
        return -1;
    }

    /* Find first enabled display (scanout 0) */
    gpu_w = 0; gpu_h = 0;
    for (int i = 0; i < 16; i++) {
        if (info.pmodes[i].enabled) {
            gpu_w = info.pmodes[i].rect.width;
            gpu_h = info.pmodes[i].rect.height;
            kprintf("virtio-gpu: display %d: %ux%u\n", i, gpu_w, gpu_h);
            break;
        }
    }

    if (gpu_w == 0 || gpu_h == 0) {
        kprintf("virtio-gpu: no active display, falling back 1024x768\n");
        gpu_w = 1024;
        gpu_h = 768;
    }

    gpu_pitch = gpu_w * 4;  /* BGRA8888 */

    /* Allocate framebuffer via PMM */
    uint32_t fb_bytes = gpu_pitch * gpu_h;
    gpu_fb_pages = (fb_bytes + 4095) / 4096;
    gpu_fb_phys  = pmm_alloc_pages(gpu_fb_pages);
    if (!gpu_fb_phys) {
        kprintf("virtio-gpu: framebuffer alloc failed\n");
        gpu_set_status(128);
        return -1;
    }
    gpu_fb = (uint32_t *)phys_to_virt(gpu_fb_phys);
    memset(gpu_fb, 0, fb_bytes);

    /* Create 2D resource */
    gpu_create_2d(gpu_res_id, gpu_w, gpu_h);

    /* Attach memory backing */
    gpu_attach_backing(gpu_res_id, gpu_fb_phys, fb_bytes);

    /* Set scanout */
    gpu_set_scanout(gpu_res_id, 0, gpu_w, gpu_h);

    /* Initial transfer to host (blank screen) */
    gpu_transfer_to_host(gpu_res_id, gpu_w, gpu_h);

    gpu_ok = 1;
    kprintf("virtio-gpu: ready %ux%u fb=0x%lx\n", gpu_w, gpu_h, (unsigned long)gpu_fb_phys);
    return 0;
}

int virtio_gpu_available(void) { return gpu_ok; }
