#include "usb_xhci.h"
#include "usb_ehci.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"
#include "../kernel/pmm.h"
#include "mouse.h"

#define XHCI_CLASS    0x0C
#define XHCI_SUBCLASS 0x03
#define XHCI_PROGIF   0x30

#define MAX_SLOTS 8
#define TRB_RING_SIZE 64
#define EVENT_RING_SIZE 32
#define ERST_ENTRIES 1
#define MAX_DEVS 8

enum trb_type {
    TRB_NORMAL      = 1,
    TRB_SETUP       = 2,
    TRB_DATA        = 3,
    TRB_STATUS      = 4,
    TRB_LINK        = 6,
    TRB_ENABLE_SLOT = 9,
    TRB_ADDRESS_DEV = 11,
    TRB_CONFIG_EP   = 12,
    TRB_EVAL_CTX    = 13,
    TRB_RESET_EP     = 14,
    TRB_STOP_EP      = 15,
    TRB_NOOP_CMD     = 23,
    TRB_TRANSFER_EVENT   = 1 << 5,
    TRB_CMD_COMPL_EVENT  = 0 << 5,
    TRB_PORT_STATUS_EVENT = 3 << 5,
};

enum trb_ctrl {
    TRB_CHAIN  = 0x10,
    TRB_IOC    = 0x20,
    TRB_IDT    = 0x40,
    TRB_CYCLE  = 1,
    TRB_TOGGLE = 2,
    TRB_BSR    = 0x100,
};

struct xhci_trb {
    uint32_t params[4];
} __attribute__((packed));

struct xhci_slot_ctx {
    uint32_t dw[8];
} __attribute__((packed));

struct xhci_ep_ctx {
    uint32_t dw[8];
} __attribute__((packed));

struct xhci_input_ctx {
    uint32_t dev_ctx_flags[8];
    struct xhci_slot_ctx slot;
    struct xhci_ep_ctx  ep[31];
} __attribute__((packed));

struct xhci_dev_ctx {
    struct xhci_slot_ctx slot;
    struct xhci_ep_ctx  ep[31];
} __attribute__((packed));

struct xhci_erst_entry {
    uint64_t seg_addr;
    uint32_t seg_size;
    uint32_t reserved;
} __attribute__((packed));

struct usb_device_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

struct xhci_dev_state {
    int slot_id;
    struct usb_dev dev;
    struct xhci_dev_ctx *dev_ctx;
    struct xhci_input_ctx *input_ctx;
};

/* MMIO bases */
static uintptr_t xhci_mmio_base;
static uintptr_t xhci_op_base;
static uintptr_t xhci_doorbell_base;
static uintptr_t xhci_runtime_base;
static uintptr_t xhci_port_base;
static uint8_t xhci_caplen;
static int xhci_nports;
static int xhci_max_slots;
static int xhci_ok;

/* Ring buffers */
static struct xhci_trb *cmd_ring;
static struct xhci_trb *event_ring;
static struct xhci_erst_entry *erst;
static uint64_t *dcbaa;
static uint64_t cmd_ring_paddr;
static uint64_t event_ring_paddr;
static uint64_t erst_paddr;
static uint64_t dcbaa_paddr;

static int cmd_cycle;
static int event_cycle;
static int cmd_enq_idx;
static int event_deq_idx;
static int last_cmd_slot;

static struct xhci_dev_state dev_states[MAX_DEVS];
static int num_devs;

static struct xhci_trb *dev_transfer_rings[MAX_SLOTS + 1];
static uint64_t dev_transfer_paddrs[MAX_SLOTS + 1];
static int dev_transfer_cycle[MAX_SLOTS + 1];
static int dev_transfer_idx[MAX_SLOTS + 1];

/* Bounce buffer for control transfer data (higher-half mapped, DMA-safe) */
static uint8_t *xfer_buf;
static uint64_t xfer_buf_paddr;

/* MMIO helpers */
static uint32_t xh_r32(uintptr_t base, uint16_t o) {
    return *(volatile uint32_t *)(base + o);
}
static void xh_w32(uintptr_t base, uint16_t o, uint32_t v) {
    *(volatile uint32_t *)(base + o) = v;
}
static void xh_w64(uintptr_t base, uint16_t o, uint64_t v) {
    xh_w32(base, o, (uint32_t)(v & 0xFFFFFFFF));
    xh_w32(base, o + 4, (uint32_t)((v >> 32) & 0xFFFFFFFF));
}

static uint32_t op_r(uint16_t reg) { return xh_r32(xhci_op_base, reg); }
static void op_w(uint16_t reg, uint32_t v) { xh_w32(xhci_op_base, reg, v); }
static void op_w64(uint16_t reg, uint64_t v) { xh_w64(xhci_op_base, reg, v); }

/* virt_to_phys/phys_to_virt from vmm.h:
 *   phys_to_virt(phys)   = phys_to_virt_base + phys
 *   virt_to_phys(virt)   = virt - phys_to_virt_base
 */

static void doorbell_ring(int slot, int ep) {
    xh_w32(xhci_doorbell_base, slot * 4, ep & 0xFF);
}

static void *alloc_page(void) {
    return (void *)phys_to_virt((uint64_t)pmm_alloc_page());
}

/* Command TRB ring: push a single TRB */
static void cmd_push(uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3) {
    cmd_ring[cmd_enq_idx].params[0] = p0;
    cmd_ring[cmd_enq_idx].params[1] = p1;
    cmd_ring[cmd_enq_idx].params[2] = p2;
    cmd_ring[cmd_enq_idx].params[3] = p3 | cmd_cycle;
    cmd_enq_idx = (cmd_enq_idx + 1) % (TRB_RING_SIZE - 1);
    if (cmd_enq_idx == TRB_RING_SIZE - 1) {
        cmd_ring[cmd_enq_idx].params[0] = (uint32_t)(cmd_ring_paddr & 0xFFFFFFFF);
        cmd_ring[cmd_enq_idx].params[1] = (uint32_t)((cmd_ring_paddr >> 32) & 0xFFFFFFFF);
        cmd_ring[cmd_enq_idx].params[2] = 0;
        cmd_ring[cmd_enq_idx].params[3] = (TRB_LINK << 10) | TRB_CYCLE | cmd_cycle;
        cmd_enq_idx = 0;
        cmd_cycle ^= 1;
    }
}

/* Wait for a command completion event */
static int wait_cmd(void) {
    for (int i = 0; i < 500000; i++) {
        volatile uint32_t *ev = (void *)&event_ring[event_deq_idx];
        if (((int)(ev[3] & 1)) != event_cycle) continue;
        int type = (ev[3] >> 10) & 0x3F;
        event_deq_idx = (event_deq_idx + 1) % EVENT_RING_SIZE;
        if (event_deq_idx == 0) event_cycle ^= 1;
        xh_w64(xhci_runtime_base, 0x20 + 0x18,
               virt_to_phys((uint64_t)(uintptr_t)(&event_ring[event_deq_idx])));
        if (type == 33) { /* Command Completion Event */
            last_cmd_slot = (ev[3] >> 24) & 0xFF;
            int cc = (ev[2] >> 24) & 0xFF;
            if (cc == 1) return 0;
            kprintf("xhci: command completion code=%d slot=%d\n",
                    cc, last_cmd_slot);
            return -1;
        }
    }
    kprintf("xhci: command completion timeout\n");
    return -1;
}

/* Set up a transfer ring for a slot (must be page-aligned) */
static void setup_ring(int sid) {
    void *buf = alloc_page();
    dev_transfer_rings[sid] = (struct xhci_trb *)buf;
    dev_transfer_paddrs[sid] = virt_to_phys((uint64_t)(uintptr_t)buf);
    dev_transfer_cycle[sid] = 1;
    dev_transfer_idx[sid] = 0;
}

/* Push a TRB onto a slot's transfer ring */
static void tpush(int sid, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3) {
    struct xhci_trb *ring = dev_transfer_rings[sid];
    int idx = dev_transfer_idx[sid];
    ring[idx].params[0] = p0;
    ring[idx].params[1] = p1;
    ring[idx].params[2] = p2;
    ring[idx].params[3] = p3 | dev_transfer_cycle[sid];
    idx = (idx + 1) % (TRB_RING_SIZE - 1);
    if (idx == TRB_RING_SIZE - 1) {
        ring[TRB_RING_SIZE - 1].params[0] =
            (uint32_t)(dev_transfer_paddrs[sid] & 0xFFFFFFFF);
        ring[TRB_RING_SIZE - 1].params[1] =
            (uint32_t)((dev_transfer_paddrs[sid] >> 32) & 0xFFFFFFFF);
        ring[TRB_RING_SIZE - 1].params[2] = 0;
        ring[TRB_RING_SIZE - 1].params[3] =
            (TRB_LINK << 10) | TRB_CHAIN | TRB_CYCLE | dev_transfer_cycle[sid];
        idx = 0;
        dev_transfer_cycle[sid] ^= 1;
    }
    dev_transfer_idx[sid] = idx;
}

/* Wait for a transfer event on any slot */
static int wait_xfer(void) {
    for (int i = 0; i < 500000; i++) {
        volatile uint32_t *ev = (void *)&event_ring[event_deq_idx];
        if (((int)(ev[3] & 1)) != event_cycle) continue;
        int type = (ev[3] >> 10) & 0x3F;
        int cc = (ev[2] >> 24) & 0xFF;
        event_deq_idx = (event_deq_idx + 1) % EVENT_RING_SIZE;
        if (event_deq_idx == 0) event_cycle ^= 1;
        xh_w64(xhci_runtime_base, 0x20 + 0x18,
               virt_to_phys((uint64_t)(uintptr_t)(&event_ring[event_deq_idx])));
        if (type == 32) { /* Transfer Event */
            if (cc == 1) return 0;
            return -1;
        }
    }
    return -1;
}

/* ─── Control transfer ─── */
int xhci_control_transfer(struct usb_dev *dev, int dir_in,
                          uint8_t bmReqType, uint8_t bRequest,
                          uint16_t wValue, uint16_t wIndex,
                          uint16_t wLength, void *data) {
    if (!xhci_ok) return -1;
    struct xhci_dev_state *st = NULL;
    for (int i = 0; i < num_devs; i++) {
        if (&dev_states[i].dev == dev) { st = &dev_states[i]; break; }
    }
    if (!st) return -1;
    int sid = st->slot_id;

    /* Build setup packet */
    uint32_t s0 = (uint32_t)bmReqType
                | ((uint32_t)bRequest << 8)
                | ((uint32_t)wValue << 16);
    uint32_t s1 = (uint32_t)wIndex
                | ((uint32_t)wLength << 16);

    /* Data direction for data stage */
    int data_dir = (dir_in && wLength > 0) ? 1 : 0;

    /* Setup stage: encode 8 bytes directly in TRB (IDT=1) */
    tpush(sid, s0, s1 | (8 << 16), 0, (TRB_SETUP << 10) | TRB_IDT);

    /* Data stage (if wLength > 0) */
    if (wLength > 0 && data) {
        if (wLength > 512) wLength = 512;
        if (!dir_in)
            memcpy(xfer_buf, data, wLength);
        tpush(sid,
              (uint32_t)(xfer_buf_paddr & 0xFFFFFFFF),
              ((uint32_t)((xfer_buf_paddr >> 32) & 0xFFFFFFFF)) | ((uint32_t)wLength << 17),
              0,
              (TRB_DATA << 10) | (data_dir << 16) | TRB_CHAIN);
    }

    /* Status stage: direction opposite of data */
    int status_dir = data_dir ? 0 : 1;
    tpush(sid, 0, 0, 0,
          (TRB_STATUS << 10) | (status_dir << 16) | TRB_IOC);

    doorbell_ring(sid, 1);
    int ret = wait_xfer();
    if (ret == 0 && wLength > 0 && data && dir_in)
        memcpy(data, xfer_buf, wLength);
    return ret;
}

int xhci_bulk_transfer(struct usb_dev *dev, int endpoint,
                       int dir_in, void *data, int len) {
    if (!xhci_ok || !dev || !data || len <= 0) return -1;
    struct xhci_dev_state *st = NULL;
    for (int i = 0; i < num_devs; i++) {
        if (&dev_states[i].dev == dev) { st = &dev_states[i]; break; }
    }
    if (!st) return -1;
    int sid = st->slot_id;

    /* Bulk endpoint is EP [endpoint] on the transfer ring.
       The doorbell index for EP N is N. EP 1 for bulk-in, EP 2 for bulk-out
       (typical for HID/mass storage). We need to figure out the endpoint
       number from the usb_dev's ep_in_addr or ep_out_addr. */
    int ep_num;
    if (endpoint == 0) {
        /* Use the device's in/out endpoint addresses */
        ep_num = dir_in ? (dev->ep_in_addr & 0x0F) : (dev->ep_out_addr & 0x0F);
    } else {
        ep_num = endpoint;
    }
    if (ep_num < 1) ep_num = 1;

    /* Allocate a bounce buffer from xfer_buf for small transfers */
    int xfer_len = len;
    if (xfer_len > 4096) xfer_len = 4096;

    if (!dir_in)
        memcpy(xfer_buf, data, xfer_len);

    /* Normal TRB */
    tpush(sid,
          (uint32_t)(xfer_buf_paddr & 0xFFFFFFFF),
          (uint32_t)((xfer_buf_paddr >> 32) & 0xFFFFFFFF) | ((uint32_t)xfer_len << 17),
          0,
          (TRB_NORMAL << 10) | TRB_IOC);

    doorbell_ring(sid, ep_num);
    int ret = wait_xfer();
    if (ret == 0 && dir_in)
        memcpy(data, xfer_buf, xfer_len);
    return (ret == 0) ? xfer_len : -1;
}

int xhci_get_num_devs(void) { return num_devs; }
struct usb_dev *xhci_get_dev(int idx) {
    if (idx < 0 || idx >= num_devs) return NULL;
    return &dev_states[idx].dev;
}

int xhci_kbd_available(void) { return 0; }
int xhci_kbd_poll(void) { return 0; }
int xhci_touch_available(void) { return 0; }
int xhci_touch_poll(int *x, int *y, int *btn) { (void)x;(void)y;(void)btn; return 0; }
int xhci_mouse_available(void) { return 0; }
int xhci_mouse_poll(int *dx, int *dy, int *buttons, int *wheel) {
    (void)dx;(void)dy;(void)buttons;(void)wheel; return 0;
}

/* ─── Enumerate a port ─── */
static void enumerate_port(int port) {
    uint32_t portsc = xh_r32(xhci_port_base, port * 0x10);
    if (!(portsc & 1)) return;
    kprintf("xhci: device on port %d\n", port);

    /* Reset port */
    xh_w32(xhci_port_base, port * 0x10, portsc | (1 << 4));
    for (volatile int d = 0; d < 500000; d++) asm volatile("pause");

    portsc = xh_r32(xhci_port_base, port * 0x10);
    int reset_tries = 10000;
    while ((portsc & (1 << 4)) && reset_tries--) {
        for (volatile int d = 0; d < 1000; d++) asm volatile("pause");
        portsc = xh_r32(xhci_port_base, port * 0x10);
    }
    if (reset_tries <= 0) { kprintf("xhci: port %d reset timeout\n", port); return; }

    /* Enable slot */
    cmd_push(0, 0, 0, (TRB_ENABLE_SLOT << 10));
    doorbell_ring(0, 0);
    if (wait_cmd() < 0) { kprintf("xhci: enable slot failed\n"); return; }
    int slot_id = last_cmd_slot;
    kprintf("xhci: slot %d\n", slot_id);

    if (num_devs >= MAX_DEVS) return;
    struct xhci_dev_state *st = &dev_states[num_devs];
    memset(st, 0, sizeof(*st));
    st->slot_id = slot_id;
    /* Before the first descriptor read, EP0 uses the speed-dependent
       default packet size. Full-speed USB keyboards (including QEMU's
       usb-kbd and typical gaming boards) start at 8 bytes. */
    int port_speed = (xh_r32(xhci_port_base, port * 0x10) >> 10) & 0xF;
    st->dev.max_packet = port_speed >= 3 ? (port_speed == 4 ? 512 : 64) : 8;
    kprintf("xhci: port %d speed-id=%d ep0-mps=%d\n",
            port, port_speed, st->dev.max_packet);
    st->dev_ctx = (struct xhci_dev_ctx *)alloc_page();
    st->input_ctx = (struct xhci_input_ctx *)alloc_page();
    memset(st->dev_ctx, 0, 4096);
    memset(st->input_ctx, 0, 4096);
    dcbaa[slot_id] = virt_to_phys((uint64_t)(uintptr_t)st->dev_ctx);
    setup_ring(slot_id);

    /* Address device (BSR=0 → assign address) */
    /* Input Control Context: add slot context and endpoint 0 context. */
    st->input_ctx->dev_ctx_flags[1] = 3;
    /* Input context: slot context plus endpoint 0. */
    st->input_ctx->slot.dw[0] =
        (((xh_r32(xhci_port_base, port * 0x10) >> 10) & 0xF) << 20) |
        (1U << 27);
    st->input_ctx->slot.dw[1] = (uint32_t)(port + 1) << 16;
    st->input_ctx->slot.dw[1] = 0;
    st->input_ctx->slot.dw[2] = 0;
    st->input_ctx->slot.dw[3] = 0;
    st->input_ctx->ep[0].dw[1] = (3U << 1) | (4U << 3) |
                                 ((uint32_t)st->dev.max_packet << 16);
    st->input_ctx->ep[0].dw[2] =
        (uint32_t)(dev_transfer_paddrs[slot_id] & 0xFFFFFFFF) | 1;
    st->input_ctx->ep[0].dw[3] =
        (uint32_t)((dev_transfer_paddrs[slot_id] >> 32) & 0xFFFFFFFF);

    uint64_t ictx = virt_to_phys((uint64_t)(uintptr_t)st->input_ctx);
    cmd_push((uint32_t)(ictx & 0xFFFFFFFF),
             ((uint32_t)((ictx >> 32) & 0xFFFFFFFF)) | (slot_id << 24),
             0,
             (TRB_ADDRESS_DEV << 10));
    doorbell_ring(0, 0);
    if (wait_cmd() < 0) { kprintf("xhci: address device failed\n"); return; }

    st->dev.address = slot_id;
    st->dev.max_packet = 64;
    st->dev.speed = (xh_r32(xhci_port_base, port * 0x10) >> 10) & 7;

    /* Get device descriptor */
    struct usb_device_desc dd;
    if (xhci_control_transfer(&st->dev, 1, 0x80, 6, 1 << 8, 0, sizeof(dd), &dd) < 0) {
        kprintf("xhci: get dev desc failed\n"); return;
    }
    st->dev.vendor_id = dd.idVendor;
    st->dev.product_id = dd.idProduct;
    st->dev.class_code = dd.bDeviceClass;
    st->dev.subclass = dd.bDeviceSubClass;
    st->dev.protocol = dd.bDeviceProtocol;
    st->dev.max_packet = dd.bMaxPacketSize0;

    kprintf("xhci: device slot=%d VID=%04x PID=%04x class=%02x\n",
            slot_id, dd.idVendor, dd.idProduct, dd.bDeviceClass);

    num_devs++;
}

int xhci_init(void) {
    uint8_t bus, slot, func;
    if (!pci_find_class_idx(XHCI_CLASS, XHCI_SUBCLASS, 0, &bus, &slot, &func))
        return -1;

    uint8_t prog_if = (pci_config_read(bus, slot, func, 0x08) >> 8) & 0xFF;
    if (prog_if != XHCI_PROGIF) {
        kprintf("xhci: prog_if 0x%02x != 0x30\n", prog_if);
        return -1;
    }

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    xhci_mmio_base = (uintptr_t)(uint64_t)(bar0 & ~0xF);
    if (bar0 & 4) {
        uint32_t bar1 = pci_config_read(bus, slot, func, 0x14);
        xhci_mmio_base |= ((uint64_t)bar1 << 32);
    }

    uintptr_t mmio_v = (uintptr_t)(void *)phys_to_virt((uint64_t)xhci_mmio_base);
    pci_config_write(bus, slot, func, 0x04, 0x0006);

    xhci_caplen = xh_r32(mmio_v, 0) & 0xFF;
    xhci_op_base = mmio_v + xhci_caplen;

    uint32_t hcsp1 = xh_r32(mmio_v, 4);
    uint32_t hccp1 = xh_r32(mmio_v, 0x10);
    xhci_nports = (hcsp1 >> 24) & 0xFF;
    xhci_max_slots = hcsp1 & 0xFF;
    if (xhci_max_slots > MAX_SLOTS) xhci_max_slots = MAX_SLOTS;

    uint32_t db_off = xh_r32(mmio_v, 0x14) & ~3;
    uint32_t rt_off = xh_r32(mmio_v, 0x18) & ~0x1F;
    xhci_doorbell_base = mmio_v + db_off;
    xhci_runtime_base = mmio_v + rt_off;
    xhci_port_base = xhci_op_base + 0x400;

    kprintf("xhci: pci %02x:%02x.%x mmio=0x%lx op=0x%lx nports=%d ctx=%d\n",
            bus, slot, func, (unsigned long)xhci_mmio_base,
            (unsigned long)(xhci_op_base - mmio_v), xhci_nports,
            (hccp1 & 4) ? 64 : 32);

    /* Reset */
    op_w(0, 1 << 1);
    for (volatile int i = 0; i < 1000000; i++) {
        if (!(op_r(0) & (1 << 1))) break;
    }

    cmd_ring = (struct xhci_trb *)alloc_page();
    event_ring = (struct xhci_trb *)alloc_page();
    erst = (struct xhci_erst_entry *)alloc_page();
    dcbaa = (uint64_t *)alloc_page();
    xfer_buf = (uint8_t *)alloc_page();
    memset(cmd_ring, 0, 4096);
    memset(event_ring, 0, 4096);
    memset(erst, 0, 4096);
    memset(dcbaa, 0, 4096);
    memset(xfer_buf, 0, 4096);
    cmd_ring_paddr = virt_to_phys((uint64_t)(uintptr_t)cmd_ring);
    event_ring_paddr = virt_to_phys((uint64_t)(uintptr_t)event_ring);
    erst_paddr = virt_to_phys((uint64_t)(uintptr_t)erst);
    dcbaa_paddr = virt_to_phys((uint64_t)(uintptr_t)dcbaa);
    xfer_buf_paddr = virt_to_phys((uint64_t)(uintptr_t)xfer_buf);
    cmd_cycle = 1;
    cmd_enq_idx = 0;
    event_cycle = 1;
    event_deq_idx = 0;
    last_cmd_slot = 0;

    erst->seg_addr = virt_to_phys((uint64_t)event_ring);
    erst->seg_size = EVENT_RING_SIZE;

    /* DCBAAP */
    op_w64(0x30, dcbaa_paddr);
    /* Configure max slots */
    op_w(0x38, xhci_max_slots);

    /* Event ring: ERSTSZ, ERSTBA, ERDP */
    xh_w32(xhci_runtime_base, 0x20 + 0x08, ERST_ENTRIES);
    xh_w64(xhci_runtime_base, 0x20 + 0x10, erst_paddr);
    xh_w64(xhci_runtime_base, 0x20 + 0x18, event_ring_paddr + event_deq_idx * 16);

    /* CRCR */
    op_w64(0x18, cmd_ring_paddr | 1);

    for (volatile int i = 0; i < 10000; i++) asm volatile("pause");

    /* Start controller */
    op_w(0, op_r(0) | 1);
    for (volatile int i = 0; i < 1000000; i++) {
        if (!(op_r(4) & 1)) break;
    }
    if (op_r(4) & 1) {
        kprintf("xhci: start failed (cmd=0x%08x sts=0x%08x pages=0x%08x)\n",
                op_r(0), op_r(4), op_r(8));
        return -1;
    }

    xhci_ok = 1;
    kprintf("xhci: controller running, %d ports\n", xhci_nports);

    for (int p = 0; p < xhci_nports; p++) {
        uint32_t ps = xh_r32(xhci_port_base, p * 0x10);
        if ((ps & 1) && !(ps & 2))
            enumerate_port(p);
    }

    kprintf("xhci: %d device(s)\n", num_devs);
    return 0;
}
