#include "virtio_net.h"
#include "nic.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"
#include "../kernel/pmm.h"


static uint8_t  vnet_bus, vnet_slot, vnet_func;
static uint16_t vnet_iobase;
static uint8_t  vnet_mac[6];
static int      vnet_ok;
static int      vnet_link_up;

/* Virtqueues */
#define VTX 0
#define VRX 1

static int vq_size[2];
static struct vring_desc *vq_desc[2];
static struct vring_avail *vq_avail[2];
static struct vring_used *vq_used[2];
static int vq_free_head[2];

static uint8_t tx_bufs[VIRTIO_NET_NUM_TX][VIRTIO_NET_BUF_LEN] __attribute__((aligned(16)));
static uint8_t rx_bufs[VIRTIO_NET_NUM_RX][VIRTIO_NET_BUF_LEN] __attribute__((aligned(16)));

/* ───── I/O port helpers ───── */

static inline uint32_t vnet_readl(uint16_t off) {
    return inl(vnet_iobase + off);
}
static inline void vnet_writel(uint16_t off, uint32_t v) {
    outl(vnet_iobase + off, v);
}
static inline uint16_t vnet_readw(uint16_t off) {
    return inw(vnet_iobase + off);
}
static inline void vnet_writew(uint16_t off, uint16_t v) {
    outw(vnet_iobase + off, v);
}
static inline uint8_t vnet_readb(uint16_t off) {
    return inb(vnet_iobase + off);
}
static inline void vnet_writeb(uint16_t off, uint8_t v) {
    outb(vnet_iobase + off, v);
}

/* ───── Feature negotiation ───── */

static uint32_t vnet_get_features(void) {
    return vnet_readl(VIRTIO_PCI_HOST_FEATURES);
}

static void vnet_set_features(uint32_t features) {
    vnet_writel(VIRTIO_PCI_GUEST_FEATURES, features);
}

/* ───── Device status ───── */

static void vnet_set_status(uint8_t status) {
    vnet_writeb(VIRTIO_PCI_STATUS, status);
}

static uint8_t vnet_get_status(void) {
    return vnet_readb(VIRTIO_PCI_STATUS);
}

static void vnet_reset(void) {
    vnet_set_status(0);
    while (vnet_get_status() != 0)
        for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
}

/* ───── Virtqueue setup ───── */

static void vq_notify(int queue) {
    vnet_writew(VIRTIO_PCI_QUEUE_NOTIFY, queue);
}

static int vq_setup(int queue, int num) {
    vnet_writew(VIRTIO_PCI_QUEUE_SEL, queue);

    int sz = vnet_readw(VIRTIO_PCI_QUEUE_NUM);
    if (sz == 0 || sz > 32768) return -1;
    if (num > 0 && num < sz) sz = num;
    /* Virtio requires power-of-2 ring size */
    if (sz & (sz - 1)) {
        int pow2 = 1;
        while (pow2 < sz) pow2 <<= 1;
        sz = pow2 >> 1;
        if (sz < 2) return -1;
    }

    /* Allocate descriptor table + avail ring + used ring in one page (or more if needed) */
    int desc_bytes = sz * sizeof(struct vring_desc);
    int avail_bytes = 6 + 2 * sz;
    int used_bytes = 6 + 8 * sz;

    int total = desc_bytes + avail_bytes + used_bytes;
    int pages = (total + 4095) / 4096;
    if (pages < 1) pages = 1;

    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) return -1;
    memset((void *)phys_to_virt(phys), 0, pages * 4096);

    vq_desc[queue] = (struct vring_desc *)phys_to_virt(phys);
    vq_avail[queue] = (struct vring_avail *)(phys_to_virt(phys) + desc_bytes);
    vq_used[queue] = (struct vring_used *)(phys_to_virt(phys) + desc_bytes + avail_bytes);
    vq_size[queue] = sz;
    vq_free_head[queue] = 0;

    /* Build free descriptor chain */
    for (int i = 0; i < sz - 1; i++) {
        vq_desc[queue][i].addr = 0;
        vq_desc[queue][i].len = 0;
        vq_desc[queue][i].flags = 0;
        vq_desc[queue][i].next = i + 1;
    }
    vq_desc[queue][sz - 1].next = 0xFFFF;

    /* Tell device the queue PFN */
    vnet_writel(VIRTIO_PCI_QUEUE_PFN, (uint32_t)(phys >> 12));

    kprintf("virtio-net: queue %d size %d PFN 0x%x\n", queue, sz, (uint32_t)(phys >> 12));
    return sz;
}

static int vq_alloc_desc(int queue) {
    if (vq_free_head[queue] >= vq_size[queue]) return -1;
    int d = vq_free_head[queue];
    vq_free_head[queue] = vq_desc[queue][d].next;
    vq_desc[queue][d].next = 0xFFFF;
    return d;
}

static void vq_free_desc(int queue, int d) {
    vq_desc[queue][d].next = vq_free_head[queue];
    vq_free_head[queue] = d;
}

static void vq_submit(int queue, int head) {
    int idx = vq_avail[queue]->idx & (vq_size[queue] - 1);
    vq_avail[queue]->ring[idx] = head;
    __sync_synchronize();
    vq_avail[queue]->idx++;
    __sync_synchronize();
    vq_notify(queue);
}

static int vq_collect(int queue) {
    __sync_synchronize();
    if (vq_used[queue]->idx == 0) return -1;
    uint16_t last = vq_used[queue]->idx - 1;
    uint16_t idx = last & (vq_size[queue] - 1);
    int id = vq_used[queue]->ring[idx].id;
    vq_used[queue]->idx--;
    return id;
}

/* ───── Initialize virtio-net ───── */

static void vnet_read_config(uint16_t off, void *buf, int len) {
    for (int i = 0; i < len; i++)
        ((uint8_t *)buf)[i] = vnet_readb(VIRTIO_PCI_DEV_CFG + off + i);
}

static int find_virtio_net(void) {
    return pci_find_device(VIRTIO_VENDOR, VIRTIO_DEV_NET,
                           &vnet_bus, &vnet_slot, &vnet_func);
}

int virtio_net_init(void) {
    if (!find_virtio_net()) {
        kprintf("virtio-net: not found\n");
        return -1;
    }

    uint32_t bar0 = pci_config_read(vnet_bus, vnet_slot, vnet_func, 0x10);
    if (bar0 & 1) {
        vnet_iobase = (uint16_t)(bar0 & ~0x3);
    } else {
        vnet_iobase = (uint16_t)(bar0 & ~0xF);
    }

    /* Enable bus master + IO space */
    pci_config_write(vnet_bus, vnet_slot, vnet_func, 0x04, 0x0007);

    kprintf("virtio-net: found at %02x:%02x.%d iobase=0x%04x\n",
            vnet_bus, vnet_slot, vnet_func, vnet_iobase);

    /* Reset device */
    vnet_reset();

    /* Acknowledge + Driver status */
    vnet_set_status(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Negotiate features: request MAC + STATUS */
    uint32_t features = vnet_get_features();
    uint32_t guest_features = features & (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
    vnet_set_features(guest_features);

    vnet_set_status(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    if (!(vnet_get_status() & VIRTIO_STATUS_FEATURES_OK)) {
        kprintf("virtio-net: feature negotiation failed\n");
        vnet_set_status(VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* Read MAC from device config */
    if (features & VIRTIO_NET_F_MAC) {
        vnet_read_config(0, vnet_mac, 6);
    } else {
        vnet_mac[0] = 0x02; vnet_mac[1] = 0x00;
        vnet_mac[2] = 0x00; vnet_mac[3] = 0x00;
        vnet_mac[4] = 0x00; vnet_mac[5] = 0x01;
    }

    /* Set up virtqueues */
    if (vq_setup(VTX, VIRTIO_NET_NUM_TX) < 0) {
        kprintf("virtio-net: TX queue setup failed\n");
        vnet_set_status(VIRTIO_STATUS_FAILED);
        return -1;
    }
    if (vq_setup(VRX, VIRTIO_NET_NUM_RX) < 0) {
        kprintf("virtio-net: RX queue setup failed\n");
        vnet_set_status(VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* Submit empty RX buffers */
    for (int i = 0; i < VIRTIO_NET_NUM_RX; i++) {
        int d = vq_alloc_desc(VRX);
        if (d < 0) break;
        vq_desc[VRX][d].addr = virt_to_phys((uintptr_t)rx_bufs[i]);
        vq_desc[VRX][d].len = VIRTIO_NET_BUF_LEN;
        vq_desc[VRX][d].flags = 0x02; /* WRITE */
        vq_submit(VRX, d);
    }

    /* Driver OK */
    vnet_set_status(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                    VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    /* Check link status */
    if (features & VIRTIO_NET_F_STATUS) {
        uint16_t status;
        vnet_read_config(6, &status, 2);
        vnet_link_up = (status & 1) != 0;
        kprintf("virtio-net: link %s\n", vnet_link_up ? "UP" : "DOWN");
    } else {
        vnet_link_up = 1;
    }

    vnet_ok = 1;
    return 0;
}

/* ───── Send ───── */

int virtio_net_send(const void *data, int len) {
    if (!vnet_ok || !vnet_link_up) return -1;
    if (len + (int)sizeof(struct virtio_net_hdr) > VIRTIO_NET_BUF_LEN) return -1;

    int d = vq_alloc_desc(VTX);
    if (d < 0) return -1;

    uint8_t *buf = tx_bufs[d % VIRTIO_NET_NUM_TX];
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)buf;
    memset(hdr, 0, sizeof(*hdr));
    memcpy(buf + sizeof(struct virtio_net_hdr), data, len);

    vq_desc[VTX][d].addr = virt_to_phys((uintptr_t)buf);
    vq_desc[VTX][d].len = len + sizeof(struct virtio_net_hdr);
    vq_desc[VTX][d].flags = 0; /* device reads */

    vq_submit(VTX, d);

    return len;
}

/* ───── Receive ───── */

int virtio_net_recv(void *buf, int max_len) {
    if (!vnet_ok) return -1;

    int d = vq_collect(VRX);
    if (d < 0) return 0;

    uint32_t pkt_len = vq_used[VRX]->ring[d & (vq_size[VRX] - 1)].len;
    if (pkt_len < sizeof(struct virtio_net_hdr)) {
        vq_free_desc(VRX, d);
        return 0;
    }

    pkt_len -= sizeof(struct virtio_net_hdr);
    if (pkt_len > (uint32_t)max_len) pkt_len = max_len;

    uint8_t *src = rx_bufs[d % VIRTIO_NET_NUM_RX] + sizeof(struct virtio_net_hdr);
    memcpy(buf, src, pkt_len);

    /* Recycle the buffer */
    uint64_t phys_addr = virt_to_phys((uintptr_t)rx_bufs[d % VIRTIO_NET_NUM_RX]);
    vq_desc[VRX][d].addr = phys_addr;
    vq_desc[VRX][d].len = VIRTIO_NET_BUF_LEN;
    vq_desc[VRX][d].flags = 0x02;
    vq_submit(VRX, d);

    return (int)pkt_len;
}

/* ───── MAC ───── */

void virtio_net_get_mac(uint8_t *mac) {
    memcpy(mac, vnet_mac, 6);
}

/* ───── NIC registration ───── */

nic_driver_t virtio_net_nic = {
    .name = "virtio-net",
    .probe = virtio_net_init,
    .send = virtio_net_send,
    .recv = virtio_net_recv,
    .get_mac = virtio_net_get_mac,
};
