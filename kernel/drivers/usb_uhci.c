#include "usb_uhci.h"
#include "usb_ehci.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "mouse.h"
#include "keyboard.h"
#include "hid_keys.h"

/* PCI class for UHCI */
#define UHCI_CLASS    0x0C
#define UHCI_SUBCLASS 0x03
#define UHCI_PROGIF   0x00

/* Maximum number of TDs in our pool */
#define TD_POOL_SIZE  36

/* UHCI controller state */
static uint16_t uhci_io_base;
static int      uhci_ok;

/* HID class code */
#define USB_HID_CLASS 3

/* ───── Page-aligned DMA buffers ───── */

/* Frame list: 1024 × 4 bytes = 4096, page-aligned */
static uint32_t frame_list[UHCI_FRAMELIST_SIZE] __attribute__((aligned(4096)));

/* Control QH (8-byte aligned) */
static struct uhci_qh ctrl_qh __attribute__((aligned(8)));

/* TD pool (16-byte aligned) */
static struct uhci_td td_pool[TD_POOL_SIZE] __attribute__((aligned(16)));
static int td_pool_used;

/* DMA buffers (aligned to 16 for UHCI) */
static uint8_t setup_buf[8] __attribute__((aligned(16)));
static uint8_t ctrl_data[512] __attribute__((aligned(16)));

/* ───── USB device state ───── */

#define MAX_UHCI_DEVS 4
static struct usb_dev uhci_devs[MAX_UHCI_DEVS];
static int num_uhci_devs;

/* ───── Physical address helper (identity-mapped kernel) ───── */

static uint32_t v2p(void *v) {
    return (uint32_t)(uintptr_t)v;
}

/* ───── I/O port helpers ───── */

static uint16_t ureadw(uint16_t reg) {
    return inw(uhci_io_base + reg);
}

static void uwritew(uint16_t reg, uint16_t val) {
    outw(uhci_io_base + reg, val);
}

static void uwritel(uint16_t reg, uint32_t val) {
    outl(uhci_io_base + reg, val);
}

static uint32_t ureadl(uint16_t reg) {
    return inl(uhci_io_base + reg);
}

/* ───── TD pool allocator ───── */

static struct uhci_td *td_alloc(void) {
    if (td_pool_used >= TD_POOL_SIZE)
        return NULL;
    struct uhci_td *td = &td_pool[td_pool_used++];
    memset(td, 0, sizeof(*td));
    td->link = TD_LINK_TERMINATE;
    return td;
}

static void td_pool_reset(void) {
    td_pool_used = 0;
}

/* ───── Initialize a TD ───── */

static void td_init(struct uhci_td *td, int pid, int dev_addr, int endpoint,
                    int toggle, int maxlen, uint32_t buf_phys, int is_in,
                    int low_speed)
{
    memset(td, 0, sizeof(*td));
    (void)is_in;
    td->link = TD_LINK_TERMINATE;
    td->ctrl = TD_CTRL_ACTIVE | TD_CTRL_SPD;
    if (low_speed)
        td->ctrl |= TD_CTRL_LS;
    td->token = TD_TOKEN_DEVADDR(dev_addr) |
                TD_TOKEN_ENDPT(endpoint) |
                TD_TOKEN_PID(pid) |
                TD_TOKEN_TOGGLE(toggle) |
                TD_TOKEN_MAXLEN(maxlen);
    td->buffer = buf_phys;
}

/* ───── Controller reset ───── */

static void uhci_controller_reset(void) {
    /* HCRESET */
    uwritew(UHCI_USBCMD, UHCI_CMD_HCRESET);
    for (volatile int i = 0; i < 100000; i++) {
        if (!(ureadw(UHCI_USBCMD) & UHCI_CMD_HCRESET))
            break;
    }
    /* Wait for halted */
    for (volatile int i = 0; i < 100000; i++) {
        if (ureadw(UHCI_USBSTS) & UHCI_STS_HCH)
            break;
    }
}

/* ───── Schedule initialization ───── */

static void start_schedule(void) {
    /* Set all frame list entries to point to the ctrl QH (QH pointer) */
    uint32_t qh_link = v2p(&ctrl_qh) | 0x02; /* bit 1 = 1: QH pointer */
    for (int i = 0; i < UHCI_FRAMELIST_SIZE; i++)
        frame_list[i] = qh_link;

    /* QH: no active transfer (element terminated) */
    ctrl_qh.head = TD_LINK_TERMINATE;
    ctrl_qh.element = TD_LINK_TERMINATE;

    /* Set frame list base */
    uwritel(UHCI_FLBASEADD, v2p(frame_list));

    /* SOF timing (default 64) */
    outb(uhci_io_base + UHCI_SOFMOD, 64);

    /* Set CF and RS */
    uwritew(UHCI_USBCMD, UHCI_CMD_CF | UHCI_CMD_RS);

    /* Wait for not halted */
    for (volatile int i = 0; i < 100000; i++) {
        if (!(ureadw(UHCI_USBSTS) & UHCI_STS_HCH))
            break;
    }

    kprintf("uhci: usbcmd=0x%04x usbsts=0x%04x frnum=0x%04x flbase=0x%08x\n",
            ureadw(UHCI_USBCMD), ureadw(UHCI_USBSTS), ureadw(UHCI_FRNUM),
            ureadl(UHCI_FLBASEADD));
}

/* ───── Wait for a TD to complete (poll active bit) ───── */

static int wait_td(struct uhci_td *td, int timeout_ms) {
    int max_loops = timeout_ms * 100;
    for (int i = 0; i < max_loops; i++) {
        if (!(td->ctrl & TD_CTRL_ACTIVE)) {
            /* Check for errors */
            uint32_t err = td->ctrl & (TD_CTRL_STALLED | TD_CTRL_DBUFERR |
                                       TD_CTRL_BABBLE | TD_CTRL_CRCTO);
            if (err)
                return -1;
            return 0;
        }
        for (volatile int w = 0; w < 1000; w++) asm volatile("pause");
    }
    return -1; /* timeout */
}

/* ───── USB control transfer (using UHCI async schedule) ───── */

static int uhci_control_transfer(struct usb_dev *dev, int dir_in,
                                 uint8_t bmReqType, uint8_t bRequest,
                                 uint16_t wValue, uint16_t wIndex,
                                 uint16_t wLength, void *data)
{
    if (!uhci_ok || !dev) return -1;

    int dev_addr = dev->address;
    int max_packet = dev->max_packet;
    int low_speed = (dev->speed == 1);
    if (max_packet <= 0) max_packet = 64;

    td_pool_reset();

    /* Build setup packet (8 bytes) */
    setup_buf[0] = bmReqType;
    setup_buf[1] = bRequest;
    setup_buf[2] = wValue & 0xFF;
    setup_buf[3] = (wValue >> 8) & 0xFF;
    setup_buf[4] = wIndex & 0xFF;
    setup_buf[5] = (wIndex >> 8) & 0xFF;
    setup_buf[6] = wLength & 0xFF;
    setup_buf[7] = (wLength >> 8) & 0xFF;

    /* Setup TD: SETUP, toggle=0, maxlen=8 */
    struct uhci_td *td = td_alloc();
    if (!td) return -1;
    td_init(td, UHCI_PID_SETUP, dev_addr, 0, 0, 8, v2p(setup_buf), 0, low_speed);
    struct uhci_td *first_td = td;
    struct uhci_td *prev_td = td;

    /* Data stage (if wLength > 0) */
    int data_len = (int)wLength;
    if (data_len > 0) {
        if (!data) return -1;
        if (data_len > 512) data_len = 512;

        /* Copy data to DMA bounce buffer */
        if (dir_in) {
            /* IN: buffer will be filled later */
        } else {
            memcpy(ctrl_data, data, data_len);
        }

        int remaining = data_len;
        int toggle = 1;
        uint32_t buf_off = 0;
        int data_pid = dir_in ? UHCI_PID_IN : UHCI_PID_OUT;

        while (remaining > 0) {
            int chunk = remaining;
            if (chunk > max_packet) chunk = max_packet;

            td = td_alloc();
            if (!td) return -1;
            td_init(td, data_pid, dev_addr, 0, toggle, max_packet,
                    v2p(ctrl_data) + buf_off, dir_in, low_speed);

            prev_td->link = v2p(td) | TD_LINK_DEPTH;
            prev_td = td;

            buf_off += chunk;
            remaining -= chunk;
            toggle ^= 1;
        }
    }

    /* Status stage: opposite direction, toggle=1, maxlen=0 (zero-length) */
    int status_pid = (dir_in || data_len == 0) ? UHCI_PID_OUT : UHCI_PID_IN;
    int status_in = (status_pid == UHCI_PID_IN);

    td = td_alloc();
    if (!td) return -1;
    /* For status, maxlen=0 encodes as 0x3F in the 6-bit field, which means 256
       bytes max per packet. Since actual transfer is zero-length, this is fine. */
    td_init(td, status_pid, dev_addr, 0, 1, 0, 0, status_in, low_speed);
    /* Mark status TD with IOC so we know when it's done */
    td->ctrl |= TD_CTRL_IOC;

    prev_td->link = v2p(td) | TD_LINK_DEPTH;

    /* Store the status TD pointer for polling */
    struct uhci_td *status_td = td;

    /* Wait for any previous transfer to complete */
    for (volatile int i = 0; i < 10000; i++) {
        if (ctrl_qh.element & TD_LINK_TERMINATE)
            break;
        asm volatile("pause");
    }

    /* Attach TD chain to QH */
    ctrl_qh.element = v2p(first_td);

    /* Poll for completion */
    int ret = wait_td(status_td, 500);

    if (ret < 0) {
        kprintf("uhci: ctrl xfer fail: ctrl=0x%08x token=0x%08x buf=0x%08x\n",
                status_td->ctrl, status_td->token, status_td->buffer);
        kprintf("uhci: qh element=0x%08x head=0x%08x\n",
                ctrl_qh.element, ctrl_qh.head);
    }

    /* Detach chain from QH */
    ctrl_qh.element = TD_LINK_TERMINATE;

    /* Copy data back if IN transfer */
    if (ret == 0 && data_len > 0 && data && dir_in) {
        memcpy(data, ctrl_data, data_len);
    }

    return ret;
}

/* ───── Device enumeration helpers ───── */

static struct usb_dev *add_uhci_dev(int address) {
    if (num_uhci_devs >= MAX_UHCI_DEVS)
        return NULL;
    struct usb_dev *d = &uhci_devs[num_uhci_devs++];
    memset(d, 0, sizeof(*d));
    d->address = address;
    return d;
}

/* USB descriptor structs (duplicated from usb_ehci.c for independence) */
struct usb_device_descriptor {
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

struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor {
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

struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

/* ───── Enumerate a device on a UHCI port ───── */

static int get_dev_desc(struct usb_dev *dev) {
    struct usb_device_descriptor dd;
    memset(&dd, 0, sizeof(dd));
    int ret = uhci_control_transfer(dev, USB_DIR_IN, 0x80,
                                    USB_REQ_GET_DESCRIPTOR,
                                    USB_DESC_DEVICE << 8, 0, 8, &dd);
    if (ret < 0) return -1;
    dev->max_packet = dd.bMaxPacketSize0;
    ret = uhci_control_transfer(dev, USB_DIR_IN, 0x80,
                                USB_REQ_GET_DESCRIPTOR,
                                USB_DESC_DEVICE << 8, 0, sizeof(dd), &dd);
    if (ret < 0) return -1;
    dev->vendor_id = dd.idVendor;
    dev->product_id = dd.idProduct;
    dev->class_code = dd.bDeviceClass;
    dev->subclass = dd.bDeviceSubClass;
    dev->protocol = dd.bDeviceProtocol;
    return 0;
}

static int set_address(struct usb_dev *dev, int addr) {
    return uhci_control_transfer(dev, USB_DIR_OUT, 0x00,
                                USB_REQ_SET_ADDRESS,
                                addr, 0, 0, NULL);
}

static int get_config_desc(struct usb_dev *dev) {
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    int ret = uhci_control_transfer(dev, USB_DIR_IN, 0x80,
                                    USB_REQ_GET_DESCRIPTOR,
                                    USB_DESC_CONFIG << 8, 0, 256, buf);
    if (ret < 0) return -1;

    struct usb_config_descriptor *cd = (struct usb_config_descriptor *)buf;
    int total = cd->wTotalLength;
    if (total > 256) total = 256;
    int pos = cd->bLength;

    while (pos + 1 < total) {
        uint8_t len = buf[pos];
        uint8_t type = buf[pos + 1];
        if (len == 0) break;
        if (pos + len > total) break;

        if (type == USB_DESC_IFACE) {
            struct usb_interface_descriptor *iface =
                (struct usb_interface_descriptor *)(buf + pos);
            if (iface->bInterfaceClass == USB_HID_CLASS) {
                kprintf("uhci: HID interface (subclass=%02x proto=%02x)\n",
                        iface->bInterfaceSubClass, iface->bInterfaceProtocol);
                dev->is_hid = 1;
                dev->hid_subclass = iface->bInterfaceSubClass;
                dev->hid_protocol = iface->bInterfaceProtocol;
            }
        }

        if (type == USB_DESC_ENDPOINT) {
            struct usb_endpoint_descriptor *ep =
                (struct usb_endpoint_descriptor *)(buf + pos);
            uint8_t addr = ep->bEndpointAddress;
            if (addr & 0x80) {
                dev->ep_in_addr = addr;
                dev->ep_in_attr = ep->bmAttributes;
                dev->ep_in_maxp = ep->wMaxPacketSize;
            } else {
                dev->ep_out_addr = addr;
                dev->ep_out_attr = ep->bmAttributes;
                dev->ep_out_maxp = ep->wMaxPacketSize;
            }
        }
        pos += len;
    }
    return 0;
}

/* ───── HID keyboard state ───── */

static int uhci_kbd_dev_idx = -1;
static uint8_t uhci_kbd_prev_report[16];
static int uhci_kbd_prev_valid;

static const char hid_to_ascii[256] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
    [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z', [0x1E] = '1', [0x1F] = '2',
    [0x20] = '3', [0x21] = '4', [0x22] = '5', [0x23] = '6',
    [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
    [0x28] = '\n', [0x2A] = '\b', [0x2B] = '\t', [0x2C] = ' ',
    [0x2D] = '-', [0x2E] = '=', [0x2F] = '[', [0x30] = ']',
    [0x31] = '\\', [0x33] = ';', [0x34] = '\'', [0x35] = '`',
    [0x36] = ',', [0x37] = '.', [0x38] = '/',
};

static const char hid_to_ascii_shift[256] = {
    [0x04] = 'A', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D',
    [0x08] = 'E', [0x09] = 'F', [0x0A] = 'G', [0x0B] = 'H',
    [0x0C] = 'I', [0x0D] = 'J', [0x0E] = 'K', [0x0F] = 'L',
    [0x10] = 'M', [0x11] = 'N', [0x12] = 'O', [0x13] = 'P',
    [0x14] = 'Q', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
    [0x18] = 'U', [0x19] = 'V', [0x1A] = 'W', [0x1B] = 'X',
    [0x1C] = 'Y', [0x1D] = 'Z', [0x1E] = '!', [0x1F] = '@',
    [0x20] = '#', [0x21] = '$', [0x22] = '%', [0x23] = '^',
    [0x24] = '&', [0x25] = '*', [0x26] = '(', [0x27] = ')',
    [0x28] = '\n', [0x2A] = '\b', [0x2B] = '\t', [0x2C] = ' ',
    [0x2D] = '_', [0x2E] = '+', [0x2F] = '{', [0x30] = '}',
    [0x31] = '|', [0x33] = ':', [0x34] = '"', [0x35] = '~',
    [0x36] = '<', [0x37] = '>', [0x38] = '?',
};

/* ───── Public API ───── */

int uhci_kbd_available(void) {
    return uhci_kbd_dev_idx >= 0 && uhci_kbd_dev_idx < num_uhci_devs;
}

int uhci_kbd_poll(void) {
    if (uhci_kbd_dev_idx < 0 || uhci_kbd_dev_idx >= num_uhci_devs)
        return 0;

    struct usb_dev *dev = &uhci_devs[uhci_kbd_dev_idx];

    uint8_t report[16];
    memset(report, 0, sizeof(report));

    int ret = uhci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                    0x0100, 0, 16, report);
    if (ret < 0) {
        memset(report, 0, 8);
        ret = uhci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                    0x0000, 0, 8, report);
        if (ret < 0) return 0;
    }

    uint8_t mod = report[0];

    /* Update global modifier state */
    extern int shift_down, ctrl_down, alt_down, super_down;
    shift_down  = (mod & HID_MOD_SHIFT) ? 1 : 0;
    ctrl_down   = (mod & HID_MOD_CTRL)  ? 1 : 0;
    alt_down    = (mod & HID_MOD_ALT)   ? 1 : 0;
    super_down  = (mod & HID_MOD_GUI)   ? 1 : 0;

    uint8_t key = 0;
    for (int i = 2; i < (int)sizeof(report); i++) {
        if (!report[i]) continue;
        int was_down = 0;
        for (int j = 2; j < (int)sizeof(uhci_kbd_prev_report); j++)
            if (uhci_kbd_prev_report[j] == report[i]) { was_down = 1; break; }
        if (!was_down || !uhci_kbd_prev_valid) { key = report[i]; break; }
    }
    memcpy(uhci_kbd_prev_report, report, sizeof(report));
    uhci_kbd_prev_valid = 1;
    if (!key) return 0;

    /* Check for special keys first */
    int sp = hid_special_key(key);
    if (sp) return sp;

    char c = 0;
    if (mod & HID_MOD_SHIFT)
        c = hid_to_ascii_shift[key];
    if (!c)
        c = hid_to_ascii[key];

    if ((mod & HID_MOD_CTRL) && c >= 'a' && c <= 'z')
        c = c - 'a' + 1;

    return (unsigned char)c;
}

/* ───── USB HID touch/tablet (absolute) ───── */

static int uhci_touch_dev_idx = -1;
static int uhci_mouse_dev_idx = -1;

int uhci_touch_available(void) {
    return uhci_touch_dev_idx >= 0 && uhci_touch_dev_idx < num_uhci_devs;
}

int uhci_touch_poll(int *x, int *y, int *btn) {
    if (uhci_touch_dev_idx < 0 || uhci_touch_dev_idx >= num_uhci_devs)
        return 0;
    struct usb_dev *dev = &uhci_devs[uhci_touch_dev_idx];

    uint8_t report[16];
    int ret = uhci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                    0x0100, 0, sizeof(report), report);
    if (ret < 0) {
        ret = uhci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
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

int uhci_mouse_available(void) {
    return uhci_mouse_dev_idx >= 0 && uhci_mouse_dev_idx < num_uhci_devs;
}

int uhci_mouse_poll(int *dx, int *dy, int *buttons, int *wheel) {
    if (uhci_mouse_dev_idx < 0 || uhci_mouse_dev_idx >= num_uhci_devs)
        return 0;
    struct usb_dev *dev = &uhci_devs[uhci_mouse_dev_idx];

    uint8_t report[8];
    int ret = uhci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                    0x0100, 0, sizeof(report), report);
    if (ret < 0) {
        ret = uhci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
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

/* ───── Port enumeration ───── */

static void enumerate_port(int port_idx) {
    uint16_t portsc = ureadw(UHCI_PORTSC(port_idx));

    if (!(portsc & UHCI_PORT_CCS))
        return;

    kprintf("uhci: device on port %d\n", port_idx);

    /* Reset port */
    portsc &= ~(UHCI_PORT_PEC | UHCI_PORT_CSC);
    portsc |= UHCI_PORT_PR;
    uwritew(UHCI_PORTSC(port_idx), portsc);
    for (volatile int i = 0; i < 500000; i++) asm volatile("pause");

    /* End reset */
    portsc = ureadw(UHCI_PORTSC(port_idx));
    portsc &= ~UHCI_PORT_PR;
    uwritew(UHCI_PORTSC(port_idx), portsc);
    for (volatile int i = 0; i < 500000; i++) asm volatile("pause");

    /* Check if port is enabled */
    portsc = ureadw(UHCI_PORTSC(port_idx));
    if (!(portsc & UHCI_PORT_PE)) {
        kprintf("uhci: port %d not enabled after reset\n", port_idx);
        return;
    }

    int is_low = (portsc & UHCI_PORT_LSDA) != 0;
    kprintf("uhci: port %d speed=%s\n", port_idx,
            is_low ? "low" : "full");

    int new_addr = num_uhci_devs + 1;
    struct usb_dev *dev = add_uhci_dev(0);
    if (!dev) return;
    dev->speed = is_low ? 1 : 0;
    dev->speed = is_low ? 1 : 0;
    dev->max_packet = 64;

    /* Get device descriptor (first 8 bytes to know max packet) using address 0 */
    if (get_dev_desc(dev) < 0) {
        kprintf("uhci: failed to get device descriptor\n");
        num_uhci_devs--;
        return;
    }

    /* Set address: TDs use address 0, wValue = new_addr */
    if (set_address(dev, new_addr) < 0) {
        kprintf("uhci: set address failed\n");
        num_uhci_devs--;
        return;
    }
    dev->address = new_addr;
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");

    /* Re-read full device descriptor with new address */
    {
        struct usb_device_descriptor dd;
        memset(&dd, 0, sizeof(dd));
        int ret = uhci_control_transfer(dev, USB_DIR_IN, 0x80,
                                        USB_REQ_GET_DESCRIPTOR,
                                        USB_DESC_DEVICE << 8, 0, sizeof(dd), &dd);
        if (ret >= 0) {
            dev->max_packet = dd.bMaxPacketSize0;
            dev->vendor_id = dd.idVendor;
            dev->product_id = dd.idProduct;
            dev->class_code = dd.bDeviceClass;
            dev->subclass = dd.bDeviceSubClass;
            dev->protocol = dd.bDeviceProtocol;
        }
    }

    kprintf("uhci: device %d VID=%04x PID=%04x class=%02x subclass=%02x\n",
            dev->address, dev->vendor_id, dev->product_id,
            dev->class_code, dev->subclass);

    /* Parse config and find endpoints */
    get_config_desc(dev);

    /* HID detection — differentiate keyboard (proto=1) vs mouse/tablet (proto=2/0) */
    if (dev->class_code == USB_HID_CLASS || dev->subclass == USB_HID_CLASS || dev->is_hid) {
        int hid_proto = dev->hid_protocol ? dev->hid_protocol : dev->protocol;
        kprintf("uhci: HID device detected (subclass=0x%02x proto=0x%02x)\n",
                dev->hid_subclass, hid_proto);
        if (dev->ep_in_maxp > 0) {
            if (hid_proto == 2) {
                /* Mouse / touchscreen / tablet — differentiate by endpoint size */
                if (dev->ep_in_maxp <= 8 && uhci_mouse_dev_idx < 0) {
                    uhci_mouse_dev_idx = num_uhci_devs - 1;
                    kprintf("uhci-mouse: relative input device ready (maxp=%d)\n",
                            dev->ep_in_maxp);
                } else {
                    uhci_touch_dev_idx = num_uhci_devs - 1;
                    kprintf("uhci-touch: absolute input device ready\n");
                }
            } else {
                uhci_kbd_dev_idx = num_uhci_devs - 1;
                kprintf("uhci-kbd: keyboard ready\n");
            }

            /* Set configuration 1 */
            uhci_control_transfer(dev, USB_DIR_OUT, 0x00,
                                  USB_REQ_SET_CONFIGURATION, 1, 0, 0, NULL);
            if (hid_proto == 1) {
                uhci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                      USB_REQ_SET_PROTOCOL, 0, 0, 0, NULL);
                uhci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                      USB_REQ_SET_IDLE, 0, 0, 0, NULL);
            }
        }
    }
}

/* ───── Initialization ───── */

int uhci_init(void) {
    int dev_idx = 0;
    int found = 0;

    uint8_t bus, slot, func;
    while (pci_find_class_idx(UHCI_CLASS, UHCI_SUBCLASS, dev_idx,
                              &bus, &slot, &func)) {
        uint32_t rev = pci_config_read(bus, slot, func, 0x08);
        uint8_t prog_if = (rev >> 8) & 0xFF;
        if (prog_if != UHCI_PROGIF) {
            dev_idx++;
            continue;
        }
        uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
        uint32_t bar4 = pci_config_read(bus, slot, func, 0x20);

        kprintf("uhci: bar0=0x%08x bar4=0x%08x\n", bar0, bar4);

        /* Find the actual I/O BAR (could be at 0x10 or 0x20 depending on device) */
        uint32_t bar_val = bar0;
        if ((!bar0 || bar0 == 0xFFFFFFFF) && bar4 && bar4 != 0xFFFFFFFF && (bar4 & 1))
            bar_val = bar4;

        uhci_io_base = bar_val & ~0x03;

        kprintf("uhci: found at %02x:%02x.%x I/O=0x%04x\n",
                bus, slot, func, uhci_io_base);

        /* Enable I/O space + bus master */
        pci_config_write(bus, slot, func, 0x04, 0x0005);

        /* Reset controller */
        uhci_controller_reset();

        /* Start schedule */
        start_schedule();

        uhci_ok = 1;
        found = 1;

        /* Enumerate ports (each UHCI controller has 2 ports on ICH9) */
        for (int p = 0; p < 2; p++) {
            enumerate_port(p);
        }

        /* Only use the first UHCI controller with activity */
        break;
    }

    if (!found) {
        kprintf("uhci: no UHCI controller found\n");
        return -1;
    }

    kprintf("uhci: %d device(s) found\n", num_uhci_devs);
    return 0;
}
