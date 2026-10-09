#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "types.h"

#define VIRTIO_VENDOR   0x1AF4
#define VIRTIO_DEV_NET  0x1000

#define VIRTIO_PCI_HOST_FEATURES  0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN      0x08
#define VIRTIO_PCI_QUEUE_NUM      0x0C
#define VIRTIO_PCI_QUEUE_SEL      0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY   0x10
#define VIRTIO_PCI_STATUS         0x12
#define VIRTIO_PCI_ISR            0x13
#define VIRTIO_PCI_DEV_CFG        0x14

#define VIRTIO_STATUS_ACK         1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED      128

#define VIRTIO_NET_F_CSUM         (1 << 0)
#define VIRTIO_NET_F_GUEST_CSUM   (1 << 1)
#define VIRTIO_NET_F_MAC          (1 << 5)
#define VIRTIO_NET_F_GSO          (1 << 6)
#define VIRTIO_NET_F_STATUS       (1 << 16)

#define VIRTIO_NET_NUM_TX 8
#define VIRTIO_NET_NUM_RX 8
#define VIRTIO_NET_BUF_LEN 2048

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_off;
} __attribute__((packed));

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

int virtio_net_init(void);
int virtio_net_send(const void *data, int len);
int virtio_net_recv(void *buf, int max_len);
void virtio_net_get_mac(uint8_t *mac);

#include "nic.h"
extern nic_driver_t virtio_net_nic;

#endif
