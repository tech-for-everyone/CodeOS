#include "usb_xhci.h"
#include "usb_ehci.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"
#include "../kernel/pmm.h"
#include "mouse.h"
#include "keyboard.h"
#include "hid_keys.h"

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

/* USB configuration/interface/endpoint descriptors for parsing */
struct usb_config_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

struct usb_iface_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
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

/* ───── HID keyboard/mouse tracking ───── */
static int xhci_kbd_dev_idx = -1;
static uint8_t xhci_kbd_prev_report[16];
static int xhci_kbd_prev_valid;

static int xhci_mouse_dev_idx = -1;
static int xhci_touch_dev_idx = -1;

/* HID-to-ASCII tables (same as EHCI driver) */
static const char hid_to_ascii[256] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
    [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4',
    [0x22] = '5', [0x23] = '6', [0x24] = '7', [0x25] = '8',
    [0x26] = '9', [0x27] = '0', [0x28] = '\n', [0x2A] = '\b',
    [0x2B] = '\t', [0x2C] = ' ', [0x2D] = '-', [0x2E] = '=',
    [0x2F] = '[', [0x30] = ']', [0x31] = '\\', [0x33] = ';',
    [0x34] = '\'', [0x35] = '`', [0x36] = ',', [0x37] = '.',
    [0x38] = '/',
};

static const char hid_to_ascii_shift[256] = {
    [0x04] = 'A', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D',
    [0x08] = 'E', [0x09] = 'F', [0x0A] = 'G', [0x0B] = 'H',
    [0x0C] = 'I', [0x0D] = 'J', [0x0E] = 'K', [0x0F] = 'L',
    [0x10] = 'M', [0x11] = 'N', [0x12] = 'O', [0x13] = 'P',
    [0x14] = 'Q', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
    [0x18] = 'U', [0x19] = 'V', [0x1A] = 'W', [0x1B] = 'X',
    [0x1C] = 'Y', [0x1D] = 'Z',
    [0x1E] = '!', [0x1F] = '@', [0x20] = '#', [0x21] = '$',
    [0x22] = '%', [0x23] = '^', [0x24] = '&', [0x25] = '*',
    [0x26] = '(', [0x27] = ')', [0x28] = '\n', [0x2A] = '\b',
    [0x2B] = '\t', [0x2C] = ' ', [0x2D] = '_', [0x2E] = '+',
    [0x2F] = '{', [0x30] = '}', [0x31] = '|', [0x33] = ':',
    [0x34] = '"', [0x35] = '~', [0x36] = '<', [0x37] = '>',
    [0x38] = '?',
};

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

/* ───── USB HID keyboard polling ───── */

int xhci_kbd_available(void) {
    return xhci_kbd_dev_idx >= 0 && xhci_kbd_dev_idx < num_devs;
}

int xhci_kbd_poll(void) {
    if (xhci_kbd_dev_idx < 0 || xhci_kbd_dev_idx >= num_devs) return 0;
    struct usb_dev *dev = &dev_states[xhci_kbd_dev_idx].dev;

    uint8_t report[16];
    memset(report, 0, sizeof(report));

    /* Try reading up to 16 bytes — many gaming keyboards (NKRO) send >8 byte reports */
    int ret = xhci_control_transfer(dev, 1, 0xA1, 0x01,
                                    0x0100, 0, 16, report);
    if (ret < 0) {
        /* Fallback: try raw input report (report ID 0) with 8 bytes */
        memset(report, 0, 8);
        ret = xhci_control_transfer(dev, 1, 0xA1, 0x01,
                                    0x0000, 0, 8, report);
        if (ret < 0) return 0;
    }

    uint8_t mod = report[0];

    /* Update global modifier state (keyboard.c globals) for Alt+Tab, Ctrl+W etc. */
    extern int shift_down, ctrl_down, alt_down, super_down;
    shift_down  = (mod & HID_MOD_SHIFT) ? 1 : 0;
    ctrl_down   = (mod & HID_MOD_CTRL)  ? 1 : 0;
    alt_down    = (mod & HID_MOD_ALT)   ? 1 : 0;
    super_down  = (mod & HID_MOD_GUI)   ? 1 : 0;

    /* Boot reports carry up to six keys; NKRO keyboards commonly carry more.
       Return one newly pressed usage per poll and remember the complete
       report, so rollover and simultaneous chords do not lose keys. */
    uint8_t key = 0;
    for (int i = 2; i < (int)sizeof(report); i++) {
        if (!report[i]) continue;
        int was_down = 0;
        for (int j = 2; j < (int)sizeof(xhci_kbd_prev_report); j++)
            if (xhci_kbd_prev_report[j] == report[i]) { was_down = 1; break; }
        if (!was_down || !xhci_kbd_prev_valid) { key = report[i]; break; }
    }
    memcpy(xhci_kbd_prev_report, report, sizeof(report));
    xhci_kbd_prev_valid = 1;
    if (!key) return 0;

    /* Check for special keys first (arrows, F-keys, Escape, etc.) */
    int sp = hid_special_key(key);
    if (sp) return sp;

    /* Standard alphanumeric/punctuation */
    char c = 0;
    if (mod & HID_MOD_SHIFT)
        c = hid_to_ascii_shift[key];
    if (!c)
        c = hid_to_ascii[key];

    /* Ctrl combinations */
    if ((mod & HID_MOD_CTRL) && c >= 'a' && c <= 'z')
        c = c - 'a' + 1;

    return (unsigned char)c;
}

/* ───── USB HID touch/tablet (absolute) ───── */

int xhci_touch_available(void) {
    return xhci_touch_dev_idx >= 0 && xhci_touch_dev_idx < num_devs;
}

int xhci_touch_poll(int *x, int *y, int *btn) {
    if (xhci_touch_dev_idx < 0 || xhci_touch_dev_idx >= num_devs)
        return 0;
    struct usb_dev *dev = &dev_states[xhci_touch_dev_idx].dev;

    uint8_t report[16];
    int ret = xhci_control_transfer(dev, 1, 0xA1, 0x01,
                                    0x0100, 0, sizeof(report), report);
    if (ret < 0) {
        ret = xhci_control_transfer(dev, 1, 0xA1, 0x01,
                                    0x0000, 0, 8, report);
        if (ret < 0) return 0;
    }

    int bx = (int)(int8_t)report[1] | ((int)report[2] << 8)
           | ((int)report[3] << 16) | ((int)report[4] << 24);
    int by = (int)(int8_t)report[5] | ((int)report[6] << 8)
           | ((int)report[7] << 16) | ((int)report[8] << 24);

    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    *x = bx;
    *y = by;
    *btn = report[0] & 0x03;

    return 1;
}

/* ───── USB HID relative mouse (boot protocol) ───── */
/* Standard USB mouse boot protocol report:
   byte 0: buttons (bit0=left, bit1=right, bit2=middle, bit3=, bit4=X1, bit5=X2)
   byte 1: X displacement (signed)
   byte 2: Y displacement (signed)
   byte 3: wheel (signed, optional) */

int xhci_mouse_available(void) {
    return xhci_mouse_dev_idx >= 0 && xhci_mouse_dev_idx < num_devs;
}

int xhci_mouse_poll(int *dx, int *dy, int *buttons, int *wheel) {
    if (xhci_mouse_dev_idx < 0 || xhci_mouse_dev_idx >= num_devs)
        return 0;
    struct usb_dev *dev = &dev_states[xhci_mouse_dev_idx].dev;

    uint8_t report[8];
    int ret = xhci_control_transfer(dev, 1, 0xA1, 0x01,
                                    0x0100, 0, sizeof(report), report);
    if (ret < 0) {
        ret = xhci_control_transfer(dev, 1, 0xA1, 0x01,
                                    0x0000, 0, 4, report);
        if (ret < 0) return 0;
    }

    *dx = (int)(int8_t)report[1];
    *dy = (int)(int8_t)report[2];
    *buttons = report[0] & 0x07;
    if (report[0] & 0x10) *buttons |= MOUSE_X1;
    if (report[0] & 0x20) *buttons |= MOUSE_X2;
    *wheel = (ret > 3) ? (int)(int8_t)report[3] : 0;

    return 1;
}

/* ─── Classify a device as keyboard/mouse/touch after enumeration ─── */
static void classify_hid_device(int dev_idx, struct usb_dev *dev) {
    int hid_proto = dev->hid_protocol ? dev->hid_protocol : dev->protocol;

    /* Check VID:PID for known keyboards */
    int is_known_kbd = 0;
    if (dev->vendor_id == 0x3554 && dev->product_id == 0xFA09)  /* AULA F87 dongle */
        is_known_kbd = 1;
    if (dev->vendor_id == 0x3151 && dev->product_id == 0x4010)  /* another AULA variant */
        is_known_kbd = 1;

    /* Check VID:PID for known mice */
    int is_known_mouse = 0;
    if (dev->vendor_id == 0x046D && dev->product_id == 0xC53F)  /* Logitech G305 receiver */
        is_known_mouse = 1;
    if (dev->vendor_id == 0x046D && dev->product_id == 0xC547)  /* Logitech Unifying */
        is_known_mouse = 1;

    if (is_known_kbd && xhci_kbd_dev_idx < 0) {
        xhci_kbd_dev_idx = dev_idx;
        kprintf("xhci-kbd: keyboard ready (VID=%04x PID=%04x)\n",
                dev->vendor_id, dev->product_id);
        return;
    }
    if (is_known_mouse && xhci_mouse_dev_idx < 0) {
        xhci_mouse_dev_idx = dev_idx;
        kprintf("xhci-mouse: mouse ready (VID=%04x PID=%04x)\n",
                dev->vendor_id, dev->product_id);
        return;
    }

    /* Standard HID classification by protocol */
    if (dev->is_hid || dev->class_code == 0x03) {
        if (hid_proto == 1 && xhci_kbd_dev_idx < 0) {
            xhci_kbd_dev_idx = dev_idx;
            kprintf("xhci-kbd: keyboard ready (proto=%d)\n", hid_proto);
        } else if (hid_proto == 2) {
            /* Mouse / touchscreen / tablet — differentiate by endpoint size */
            if (dev->ep_in_maxp <= 8 && xhci_mouse_dev_idx < 0) {
                xhci_mouse_dev_idx = dev_idx;
                kprintf("xhci-mouse: relative input device ready (maxp=%d)\n",
                        dev->ep_in_maxp);
            } else if (xhci_touch_dev_idx < 0) {
                xhci_touch_dev_idx = dev_idx;
                kprintf("xhci-touch: absolute input device ready\n");
            }
        } else if (hid_proto != 2 && xhci_kbd_dev_idx < 0) {
            /* Unknown HID protocol — assume keyboard (common for gaming boards) */
            xhci_kbd_dev_idx = dev_idx;
            kprintf("xhci-kbd: keyboard ready (unknown proto=%d)\n", hid_proto);
        }
    }
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

    int cur_dev_idx = num_devs;
    num_devs++;

    /* Parse configuration descriptor to find HID interfaces */
    uint8_t cfg_buf[256];
    memset(cfg_buf, 0, sizeof(cfg_buf));
    int cfg_ret = xhci_control_transfer(&st->dev, 1, 0x80, 6,
                                        (2 << 8), 0, 256, cfg_buf);
    if (cfg_ret >= 0) {
        struct usb_config_desc *cd = (struct usb_config_desc *)cfg_buf;
        int pos = cd->bLength;
        int total = cd->wTotalLength;
        if (total > 256) total = 256;

        while (pos + 1 < total) {
            uint8_t len = cfg_buf[pos];
            uint8_t type = cfg_buf[pos + 1];
            if (len == 0) break;
            if (pos + len > total) break;

            if (type == 4) { /* Interface descriptor */
                struct usb_iface_desc *iface = (struct usb_iface_desc *)(cfg_buf + pos);
                if (iface->bInterfaceClass == 0x03) {
                    kprintf("xhci: HID interface (subclass=%02x proto=%02x)\n",
                            iface->bInterfaceSubClass, iface->bInterfaceProtocol);
                    st->dev.is_hid = 1;
                    st->dev.hid_subclass = iface->bInterfaceSubClass;
                    st->dev.hid_protocol = iface->bInterfaceProtocol;
                }
            }

            if (type == 5) { /* Endpoint descriptor */
                uint8_t addr = cfg_buf[pos + 2];
                uint16_t maxp = cfg_buf[pos + 4] | ((uint16_t)cfg_buf[pos + 5] << 8);
                uint8_t attr = cfg_buf[pos + 3];
                if (addr & 0x80) {
                    st->dev.ep_in_addr = addr;
                    st->dev.ep_in_attr = attr;
                    st->dev.ep_in_maxp = maxp;
                } else {
                    st->dev.ep_out_addr = addr;
                    st->dev.ep_out_attr = attr;
                    st->dev.ep_out_maxp = maxp;
                }
            }
            pos += len;
        }
    }

    /* Classify HID devices and activate them */
    int is_hid = st->dev.is_hid || st->dev.class_code == 0x03;

    /* Also check known VID:PIDs for composite devices with class 0 */
    if (!is_hid && st->dev.class_code == 0x00) {
        if ((dd.idVendor == 0x3554 && dd.idProduct == 0xFA09) ||
            (dd.idVendor == 0x3151 && dd.idProduct == 0x4010) ||
            (dd.idVendor == 0x046D && dd.idProduct == 0xC53F) ||
            (dd.idVendor == 0x046D && dd.idProduct == 0xC547)) {
            is_hid = 1;
            st->dev.is_hid = 1;
        }
    }

    if (is_hid) {
        /* SET_CONFIGURATION(1) to activate the device */
        xhci_control_transfer(&st->dev, 0, 0x00, 9, 1, 0, 0, NULL);
        /* SET_PROTOCOL(0) — request boot protocol */
        xhci_control_transfer(&st->dev, 0, 0x21, 0x0B, 0, 0, 0, NULL);
        /* SET_IDLE(0) — suppress NAK when no change */
        xhci_control_transfer(&st->dev, 0, 0x21, 0x0A, 0, 0, 0, NULL);

        classify_hid_device(cur_dev_idx, &st->dev);
    }

    /* CDC ECM */
    if (st->dev.class_code == 0x02 || st->dev.class_code == 0xEF ||
        st->dev.class_code == 0x00) {
        if (st->dev.ep_in_addr && st->dev.ep_out_addr && !is_hid) {
            xhci_control_transfer(&st->dev, 0, 0x00, 9, 1, 0, 0, NULL);
            kprintf("xhci: CDC ECM device configured, ep_in=0x%02x ep_out=0x%02x\n",
                    st->dev.ep_in_addr, st->dev.ep_out_addr);
        }
    }
}

/* ─── Helper: init a single xHCI controller ─── */
static int xhci_init_controller(uint8_t bus, uint8_t slot, uint8_t func) {
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

    return 0;
}

int xhci_init(void) {
    uint8_t bus, slot, func;
    int found = 0;

    /* Iterate ALL xHCI controllers on the PCI bus.
       This captures AMD 500-series (1022:43EE), AMD Matisse (1022:149C),
       NVIDIA TU104 (10DE:1AD8), and any other xHCI host. */
    for (int idx = 0; idx < 16; idx++) {
        if (!pci_find_class_idx(XHCI_CLASS, XHCI_SUBCLASS, idx, &bus, &slot, &func))
            break;

        uint8_t prog_if = (pci_config_read(bus, slot, func, 0x08) >> 8) & 0xFF;
        if (prog_if != XHCI_PROGIF) {
            kprintf("xhci: skipping %02x:%02x.%x prog_if=0x%02x (not xHCI)\n",
                    bus, slot, func, prog_if);
            continue;
        }

        uint32_t vid_did = pci_config_read(bus, slot, func, 0x00);
        uint16_t vid = vid_did & 0xFFFF;
        uint16_t did = (vid_did >> 16) & 0xFFFF;
        kprintf("xhci: probing controller %02x:%02x.%x VID=%04x DID=%04x\n",
                bus, slot, func, vid, did);

        if (xhci_init_controller(bus, slot, func) == 0)
            found = 1;

        /* If we found keyboard and mouse, no need to probe more controllers */
        if (xhci_kbd_dev_idx >= 0 && xhci_mouse_dev_idx >= 0)
            break;
    }

    if (!found) {
        kprintf("xhci: no xHCI controller found or started\n");
        return -1;
    }

    kprintf("xhci: %d device(s), kbd=%s mouse=%s\n", num_devs,
            xhci_kbd_dev_idx >= 0 ? "yes" : "no",
            xhci_mouse_dev_idx >= 0 ? "yes" : "no");
    return 0;
}
