#include "usb_ehci.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"
#include "mouse.h"
#include "keyboard.h"
#include "hid_keys.h"

#include "../kernel/pmm.h"

/* EHCI PCI class */
#define EHCI_CLASS    0x0C
#define EHCI_SUBCLASS 0x03
#define EHCI_PROGIF   0x20

/* Capability register offsets */
#define EHCI_CAPLENGTH 0x00
#define EHCI_HCIPARAMS 0x04
#define EHCI_HCCPARAMS 0x08

/* Operational register offsets (from base + caplength) */
#define EHCI_USBCMD      0x00
#define EHCI_USBSTS      0x04
#define EHCI_USBINTR     0x08
#define EHCI_FRINDEX     0x0C
#define EHCI_CTRLDSSEG   0x10
#define EHCI_PERIODICLISTBASE 0x14
#define EHCI_ASYNCLIST   0x18
#define EHCI_CONFIGFLAG  0x40
#define EHCI_PORTSC(n)   (0x44 + (n) * 4)

/* USBCMD bits */
#define CMD_RS      (1 << 0)
#define CMD_HCRESET (1 << 1)
#define CMD_FRAME_LIST_SIZE_1024 0  /* bits 3:2 = 00 for 1024 elements */
#define CMD_PSE     (1 << 4)
#define CMD_ASE     (1 << 5)

/* USBSTS bits */
#define STS_HCHALTED (1 << 12)
#define STS_PCD      (1 << 2)

/* PORTSC bits */
#define PORT_CCS  (1 << 0)
#define PORT_CSC  (1 << 1)
#define PORT_PE   (1 << 2)
#define PORT_EC   (1 << 3)
#define PORT_PR   (1 << 8)
#define PORT_PP   (1 << 12)
#define PORT_SPEED_SHIFT 26
#define PORT_SPEED_MASK  (3 << 26)

/* QH constants */
#define QH_NAK_RELOAD(n)  ((n) << 20)
#define QH_CONTROL        0
#define QH_BULK           2
#define QH_EPS_HIGH       (2 << 12)
#define QH_EPS_FULL       (0 << 12)
#define QH_EPS_LOW        (1 << 12)
#define QH_DEV_ADDR(n)    ((n) << 8)
#define QH_INACTIVATE     (1 << 7)
#define QH_ENDPOINT(n)    ((n) << 15)
#define QH_TOGGLE(n)      ((n) << 14)
#define QH_HEAD           (1 << 15)  /* Head of Reclamation List */
#define QH_RECLAIM        (1 << 30)
#define QH_NEXT_TERMINATE 0x01

/* qTD token bits */
#define QTD_PID_OUT       0
#define QTD_PID_IN        (1 << 8)
#define QTD_PID_SETUP     (2 << 8)
#define QTD_PID(n)        (n)
#define QTD_ACTIVE        (1 << 7)
#define QTD_HALTED        (1 << 6)
#define QTD_DT_BUFFER_ERR (1 << 5)
#define QTD_BABBLE        (1 << 4)
#define QTD_CERR(n)       ((n) << 10)
#define QTD_CERR_MAX      3
#define QTD_STATUS(n)     ((n) << 0)
#define QTD_TOGGLE(n)     ((n) << 14)
#define QTD_BYTES(n)      ((n) << 16)
#define QTD_TOTAL_BYTES(n) ((n) << 16)
#define QTD_IOC           (1 << 15)

/* ───── EHCI data structures (must be 32-byte aligned) ───── */

struct ehci_qh {
    uint32_t next_low;
    uint32_t next_high;
    uint32_t ep_char;
    uint32_t ep_caps;
    uint32_t cur_qtd_low;
    uint32_t cur_qtd_high;
    uint32_t qtd_next_low;
    uint32_t qtd_next_high;
    uint32_t alt_qtd_low;
    uint32_t alt_qtd_high;
    uint32_t token;
    uint32_t buf[5];       /* 5 * 4 = 20 bytes */
    /* total = 64 bytes */
} __attribute__((aligned(32), packed));

struct ehci_qtd {
    uint32_t next_low;
    uint32_t next_high;
    uint32_t alt_low;
    uint32_t alt_high;
    uint32_t token;
    uint32_t buf[5];
} __attribute__((aligned(32), packed));

/* ───── EHCI controller state ───── */

static uint8_t  ehci_bus, ehci_slot, ehci_func;
static uintptr_t ehci_mmio_base;
static int      ehci_caplen;
static int      ehci_nports;
static int      ehci_ok;

/* Buffers for descriptors (use physically contiguous pages) */
#define EHCI_DESC_BUF_SIZE 4096
static uint8_t desc_buf[EHCI_DESC_BUF_SIZE] __attribute__((aligned(4096)));

/* Periodic frame list (1024 entries, page-aligned) */
#define EHCI_FRAME_LIST_SIZE 1024
static uint32_t ehci_frame_list[EHCI_FRAME_LIST_SIZE] __attribute__((aligned(4096)));

/* Maximum number of USB devices */
#define MAX_USB_DEVS 8
static struct usb_dev usb_devs[MAX_USB_DEVS];
static int num_usb_devs;

/* ───── MMIO helpers ───── */

static uint32_t e_read(uint16_t reg) {
    return *(volatile uint32_t *)(uintptr_t)(ehci_mmio_base + ehci_caplen + reg);
}

static void e_write(uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)(ehci_mmio_base + ehci_caplen + reg) = val;
}

static uint32_t cap_read(uint16_t reg) {
    return *(volatile uint32_t *)(uintptr_t)(ehci_mmio_base + reg);
}

/* ───── QH/qTD helpers (physically addressed in EHCI) ───── */

static uint32_t ehci_virt_to_phys(void *v) {
    return (uint32_t)virt_to_phys((uintptr_t)v);
}

static void qh_init_control(struct ehci_qh *qh, int dev_addr, int max_packet) {
    memset((void *)qh, 0, sizeof(*qh));
    qh->next_low = QH_NEXT_TERMINATE;
    qh->ep_char = QH_DEV_ADDR(dev_addr) | QH_EPS_FULL |
                  QH_NAK_RELOAD(4) | QH_CONTROL | QH_INACTIVATE;
    qh->ep_caps = (max_packet & 0x7FF) << 16;
    qh->token = 1; /* data toggle = 1 for data0 */
}

static void qh_init_bulk(struct ehci_qh *qh, int dev_addr, int ep, int max_packet) {
    memset((void *)qh, 0, sizeof(*qh));
    qh->next_low = QH_NEXT_TERMINATE;
    qh->ep_char = QH_DEV_ADDR(dev_addr) | QH_EPS_FULL |
                  QH_NAK_RELOAD(4) | QH_BULK | QH_ENDPOINT(ep) | QH_INACTIVATE;
    qh->ep_caps = (max_packet & 0x7FF) << 16;
}

static void qtd_init(struct ehci_qtd *qtd, uint32_t pid, void *data, int len, int toggle) {
    memset((void *)qtd, 0, sizeof(*qtd));
    qtd->next_low = QH_NEXT_TERMINATE;
    qtd->alt_low = QH_NEXT_TERMINATE;
    qtd->token = QTD_ACTIVE | QTD_CERR(QTD_CERR_MAX) | QTD_PID(pid) |
                 QTD_TOGGLE(toggle) | QTD_TOTAL_BYTES(len);
    if (len > 0 && data)
        qtd->buf[0] = ehci_virt_to_phys(data);
    else
        qtd->token = QTD_ACTIVE | QTD_CERR(QTD_CERR_MAX) | QTD_PID(pid) |
                     QTD_TOTAL_BYTES(0);
}

/* ───── Async schedule helpers ───── */

/* Pool of static QHs and qTDs for operations */
static struct ehci_qh  ctrl_qh __attribute__((aligned(32)));
static struct ehci_qtd setup_qtd __attribute__((aligned(32)));
static struct ehci_qtd data_qtd __attribute__((aligned(32)));
static struct ehci_qtd status_qtd __attribute__((aligned(32)));

/* Buffer for control transfer setup packet (8 bytes) */
static uint8_t setup_buf[8] __attribute__((aligned(32)));
/* Buffer for control data */
static uint8_t ctrl_data_buf[512] __attribute__((aligned(32)));

/* Async list head QH (always at the front of the async list) */
static struct ehci_qh async_head __attribute__((aligned(32)));

static void start_async_schedule(void) {
    e_write(EHCI_ASYNCLIST, ehci_virt_to_phys(&async_head));
    e_write(EHCI_USBCMD, e_read(EHCI_USBCMD) | CMD_ASE);
    for (volatile int i = 0; i < 1000; i++) asm volatile("pause");
}

static void stop_async_schedule(void) {
    e_write(EHCI_USBCMD, e_read(EHCI_USBCMD) & ~CMD_ASE);
    for (volatile int i = 0; i < 10000; i++) {
        if (!(e_read(EHCI_USBSTS) & (1 << 5))) break;
        asm volatile("pause");
    }
}

static int wait_qtd_complete(struct ehci_qtd *qtd, int timeout_ms) {
    int max_loops = timeout_ms * 100;
    for (int i = 0; i < max_loops; i++) {
        if (!(qtd->token & QTD_ACTIVE)) {
            if (qtd->token & QTD_HALTED) return -1;
            return 0;
        }
        for (volatile int w = 0; w < 1000; w++) asm volatile("pause");
    }
    return -1;
}

/* ───── USB keyboard driver ───── */

#define USB_HID_CLASS   3

struct usb_kbd_state {
    uint8_t mod;
    uint8_t keys[6];
};



/* ───── Control transfer ───── */

int ehci_control_transfer(struct usb_dev *dev, int dir_in,
                          uint8_t bmReqType, uint8_t bRequest,
                          uint16_t wValue, uint16_t wIndex,
                          uint16_t wLength, void *data) {
    if (!ehci_ok) return -1;

    /* Build setup packet */
    setup_buf[0] = bmReqType;
    setup_buf[1] = bRequest;
    setup_buf[2] = wValue & 0xFF;
    setup_buf[3] = (wValue >> 8) & 0xFF;
    setup_buf[4] = wIndex & 0xFF;
    setup_buf[5] = (wIndex >> 8) & 0xFF;
    setup_buf[6] = wLength & 0xFF;
    setup_buf[7] = (wLength >> 8) & 0xFF;

    /* Initialize QH for control endpoint */
    qh_init_control(&ctrl_qh, dev->address, dev->max_packet);

    /* Setup qTD (setup PID, always DATA0) */
    qtd_init(&setup_qtd, QTD_PID_SETUP, setup_buf, 8, 0);

    /* Data qTD (if wLength > 0) */
    int data_len = 0;
    if (wLength > 0) {
        if (!data) return -1;
        if (wLength > 512) return -1;
        memcpy(ctrl_data_buf, data, (int)wLength);
        data_len = (int)wLength;
        qtd_init(&data_qtd, dir_in ? QTD_PID_IN : QTD_PID_OUT,
                 ctrl_data_buf, data_len, 1);
        setup_qtd.next_low = ehci_virt_to_phys(&data_qtd);
    }

    /* Status qTD (opposite direction, DATA1) */
    int status_dir = (dir_in || wLength == 0) ? QTD_PID_OUT : QTD_PID_IN;
    if (wLength > 0)
        data_qtd.next_low = ehci_virt_to_phys(&status_qtd);
    else
        setup_qtd.next_low = ehci_virt_to_phys(&status_qtd);
    qtd_init(&status_qtd, status_dir, NULL, 0, 1);
    status_qtd.token |= QTD_IOC;

    /* Point QH overlay to the first qTD (setup) */
    ctrl_qh.qtd_next_low = ehci_virt_to_phys(&setup_qtd);
    ctrl_qh.qtd_next_high = 0;
    ctrl_qh.token = 0;  /* clear stale overlay: Active=0, Halted=0 → fetch from qtd_next */

    /* Link QH to async list */
    stop_async_schedule();
    ctrl_qh.next_low = async_head.next_low;
    async_head.next_low = ehci_virt_to_phys(&ctrl_qh);
    ctrl_qh.ep_char &= ~QH_INACTIVATE;
    start_async_schedule();

    /* Wait for completion */
    int ret = wait_qtd_complete(&status_qtd, 500);

    /* Remove QH from async list (restore self-loop) */
    stop_async_schedule();
    async_head.next_low = ehci_virt_to_phys(&async_head);
    start_async_schedule();

    if (ret == 0 && wLength > 0 && data && dir_in) {
        memcpy(data, ctrl_data_buf, data_len);
    }
    return ret;
}

/* ───── Bulk transfer ───── */

int ehci_bulk_transfer(struct usb_dev *dev, int endpoint,
                       int dir_in, void *data, int len) {
    if (!ehci_ok || !dev) return -1;
    if (!data) return -1;
    if (len <= 0) return -1;
    if (len > 512) len = 512;

    struct ehci_qh *qh = (struct ehci_qh *)(desc_buf);
    struct ehci_qtd *qtd = (struct ehci_qtd *)(desc_buf + 128);

    /* Use bounce buffer for DMA to avoid alignment/crossing-page issues */
    if (dir_in) {
        qtd_init(qtd, QTD_PID_IN, ctrl_data_buf, len, 0);
    } else {
        memcpy(ctrl_data_buf, data, len);
        qtd_init(qtd, QTD_PID_OUT, ctrl_data_buf, len, 0);
    }
    qtd->token |= QTD_IOC;

    qh_init_bulk(qh, dev->address, endpoint, dev->ep_out_maxp);
    qh->qtd_next_low = ehci_virt_to_phys(qtd);
    qh->ep_char &= ~QH_INACTIVATE;

    stop_async_schedule();
    qh->next_low = async_head.next_low;
    async_head.next_low = ehci_virt_to_phys(qh);
    start_async_schedule();

    int ret = wait_qtd_complete(qtd, 500);

    stop_async_schedule();
    async_head.next_low = QH_NEXT_TERMINATE;
    start_async_schedule();

    if (ret != 0)
        return -1;
    if (dir_in) {
        memcpy(data, ctrl_data_buf, (int)(len - ((qtd->token >> 16) & 0x7FFF)));
    }
    return (int)(len - ((qtd->token >> 16) & 0x7FFF));
}

/* ───── USB device enumeration ───── */

static struct usb_dev *add_usb_dev(int address) {
    if (num_usb_devs >= MAX_USB_DEVS) return NULL;
    struct usb_dev *d = &usb_devs[num_usb_devs++];
    memset(d, 0, sizeof(*d));
    d->address = address;
    return d;
}

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

static int get_dev_desc(struct usb_dev *dev) {
    struct usb_device_descriptor dd;
    memset(&dd, 0, sizeof(dd));
    /* First get only 8 bytes to learn max packet size */
    int ret = ehci_control_transfer(dev, USB_DIR_IN, 0x80, USB_REQ_GET_DESCRIPTOR,
                                    USB_DESC_DEVICE << 8, 0, 8, &dd);
    if (ret < 0) return -1;
    dev->max_packet = dd.bMaxPacketSize0;
    /* Get full descriptor */
    ret = ehci_control_transfer(dev, USB_DIR_IN, 0x80, USB_REQ_GET_DESCRIPTOR,
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
    return ehci_control_transfer(dev, USB_DIR_OUT, 0x00, USB_REQ_SET_ADDRESS,
                                 addr, 0, 0, NULL);
}

static int set_config(struct usb_dev *dev, int cfg) {
    return ehci_control_transfer(dev, USB_DIR_OUT, 0x00, USB_REQ_SET_CONFIGURATION,
                                 cfg, 0, 0, NULL);
}

static uint8_t config_buf[256];

static int parse_endpoints(struct usb_dev *dev) {
    struct usb_config_descriptor *cd = (struct usb_config_descriptor *)config_buf;
    int ret = ehci_control_transfer(dev, USB_DIR_IN, 0x80, USB_REQ_GET_DESCRIPTOR,
                                    USB_DESC_CONFIG << 8, 0, 256, config_buf);
    if (ret < 0) return -1;

    int pos = cd->bLength;
    int total = cd->wTotalLength;
    if (total > 256) total = 256;

    while (pos + 1 < total) {
        uint8_t len = config_buf[pos];
        uint8_t type = config_buf[pos + 1];
        if (len == 0) break;
        if (pos + len > total) break;

        if (type == USB_DESC_IFACE) {
            struct usb_interface_descriptor *iface = (struct usb_interface_descriptor *)(config_buf + pos);
            if (iface->bInterfaceClass == 0x02 && iface->bInterfaceSubClass == 0x06) {
                dev->num_endpoints = 0;
            }
            if (iface->bInterfaceClass == 0x0A) {
                dev->num_endpoints = iface->bNumEndpoints;
            }
            if (iface->bInterfaceClass == USB_HID_CLASS) {
                kprintf("usb: HID interface (subclass=%02x proto=%02x)\n",
                        iface->bInterfaceSubClass, iface->bInterfaceProtocol);
                dev->is_hid = 1;
                dev->hid_subclass = iface->bInterfaceSubClass;
                dev->hid_protocol = iface->bInterfaceProtocol;
            }
        }

        if (type == USB_DESC_ENDPOINT) {
            struct usb_endpoint_descriptor *ep = (struct usb_endpoint_descriptor *)(config_buf + pos);
            uint8_t addr = ep->bEndpointAddress;
            int maxp = ep->wMaxPacketSize;
            if (addr & 0x80) {
                dev->ep_in_addr = addr;
                dev->ep_in_attr = ep->bmAttributes;
                dev->ep_in_maxp = maxp;
            } else {
                dev->ep_out_addr = addr;
                dev->ep_out_attr = ep->bmAttributes;
                dev->ep_out_maxp = maxp;
            }
        }
        pos += len;
    }
    return 0;
}

/* ───── Setup async list head ───── */

static void init_async_head(void) {
    memset((void *)&async_head, 0, sizeof(async_head));
    /* EHCI async list is circular: head points to itself when empty */
    async_head.next_low = ehci_virt_to_phys(&async_head);
    async_head.ep_char = QH_HEAD | QH_INACTIVATE | QH_NAK_RELOAD(4);
    async_head.ep_caps = 0x40000400;
    async_head.qtd_next_low = QH_NEXT_TERMINATE;
}

/* ───── USB HID keyboard state ───── */

static int usb_kbd_dev_idx = -1;
static uint8_t usb_kbd_prev_report[16];
static int usb_kbd_prev_valid;

static int usb_touch_dev_idx = -1;
static int usb_mouse_dev_idx = -1;

/* Only usages defined by the boot keyboard protocol are initialized.  The
 * designated entries keep this table exact-sized and also make it easy to
 * extend for vendor keys without a fragile 256-byte literal. */
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

/* ───── USB HID keyboard polling ───── */

static int usb_kbd_find_dev(struct usb_dev **out) {
    if (usb_kbd_dev_idx < 0 || usb_kbd_dev_idx >= num_usb_devs)
        return -1;
    *out = &usb_devs[usb_kbd_dev_idx];
    return 0;
}

int ehci_kbd_available(void) {
    return usb_kbd_dev_idx >= 0 && usb_kbd_dev_idx < num_usb_devs;
}

int ehci_touch_available(void) {
    return usb_touch_dev_idx >= 0 && usb_touch_dev_idx < num_usb_devs;
}

int ehci_kbd_poll(void) {
    struct usb_dev *dev;
    if (usb_kbd_find_dev(&dev) < 0) return 0;

    uint8_t report[16];
    memset(report, 0, sizeof(report));

    /* Try reading up to 16 bytes — many gaming keyboards (NKRO) send >8 byte reports */
    int ret = ehci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                    0x0100, 0, 16, report);
    if (ret < 0) {
        /* Fallback: try raw input report (report ID 0) with 8 bytes */
        memset(report, 0, 8);
        ret = ehci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
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
        for (int j = 2; j < (int)sizeof(usb_kbd_prev_report); j++)
            if (usb_kbd_prev_report[j] == report[i]) { was_down = 1; break; }
        if (!was_down || !usb_kbd_prev_valid) { key = report[i]; break; }
    }
    memcpy(usb_kbd_prev_report, report, sizeof(report));
    usb_kbd_prev_valid = 1;
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

int ehci_touch_poll(int *x, int *y, int *btn) {
    if (usb_touch_dev_idx < 0 || usb_touch_dev_idx >= num_usb_devs)
        return 0;
    struct usb_dev *dev = &usb_devs[usb_touch_dev_idx];

    /* QEMU usb-tablet report format (boot protocol mouse):
       byte 0: buttons
       bytes 1-4: X (signed 32-bit LE)
       bytes 5-8: Y (signed 32-bit LE) */
    uint8_t report[16];
    int ret = ehci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                    0x0100, 0, sizeof(report), report);
    if (ret < 0) {
        ret = ehci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
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

int ehci_mouse_available(void) {
    return usb_mouse_dev_idx >= 0 && usb_mouse_dev_idx < num_usb_devs;
}

int ehci_mouse_poll(int *dx, int *dy, int *buttons, int *wheel) {
    if (usb_mouse_dev_idx < 0 || usb_mouse_dev_idx >= num_usb_devs)
        return 0;
    struct usb_dev *dev = &usb_devs[usb_mouse_dev_idx];

    uint8_t report[8];
    int ret = ehci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                    0x0100, 0, sizeof(report), report);
    if (ret < 0) {
        ret = ehci_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
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

/* ───── Public API ───── */

int ehci_get_num_devs(void) { return num_usb_devs; }
struct usb_dev *ehci_get_dev(int idx) {
    if (idx < 0 || idx >= num_usb_devs) return NULL;
    return &usb_devs[idx];
}

static void enumerate_port(int port) {
    uint32_t portsc = e_read(EHCI_PORTSC(port));

    if (!(portsc & PORT_CCS)) return;
    kprintf("ehci: device on port %d\n", port);

    /* Reset port */
    e_write(EHCI_PORTSC(port), portsc | PORT_PR);
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");
    e_write(EHCI_PORTSC(port), (e_read(EHCI_PORTSC(port)) & ~PORT_PR) | PORT_PP);
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");

    /* Enable port */
    portsc = e_read(EHCI_PORTSC(port));
    if (!(portsc & PORT_PE)) {
        e_write(EHCI_PORTSC(port), portsc | PORT_PE);
        for (volatile int i = 0; i < 50000; i++) asm volatile("pause");
    }

    portsc = e_read(EHCI_PORTSC(port));
    int speed = (portsc >> PORT_SPEED_SHIFT) & 3;
    kprintf("ehci: port %d speed=%s\n", port,
            speed == 0 ? "full" : speed == 1 ? "low" : "high");

    /* First device uses default address 0 before SET_ADDRESS */
    int new_addr = num_usb_devs + 1;
    struct usb_dev *dev = add_usb_dev(0);
    if (!dev) return;
    dev->speed = speed;
    dev->max_packet = 64;

    /* Get device descriptor (first 8 bytes to know max packet) using address 0 */
    if (get_dev_desc(dev) < 0) {
        kprintf("ehci: failed to get device descriptor\n");
        num_usb_devs--;
        return;
    }

    /* Set address: TD/QH still uses address 0, wValue = new_addr */
    if (set_address(dev, new_addr) < 0) {
        kprintf("ehci: set address failed\n");
        num_usb_devs--;
        return;
    }
    /* Device now responds to new_addr; update dev */
    dev->address = new_addr;
    for (volatile int i = 0; i < 100000; i++) asm volatile("pause");

    /* Get full device descriptor using new address */
    struct usb_device_descriptor dd;
    int ret = ehci_control_transfer(dev, USB_DIR_IN, 0x80, USB_REQ_GET_DESCRIPTOR,
                                    USB_DESC_DEVICE << 8, 0, sizeof(dd), &dd);
    if (ret >= 0) {
        dev->max_packet = dd.bMaxPacketSize0;
        dev->vendor_id = dd.idVendor;
        dev->product_id = dd.idProduct;
        dev->class_code = dd.bDeviceClass;
        dev->subclass = dd.bDeviceSubClass;
        dev->protocol = dd.bDeviceProtocol;
    }

    kprintf("ehci: device %d VID=%04x PID=%04x class=%02x subclass=%02x\n",
            dev->address, dev->vendor_id, dev->product_id,
            dev->class_code, dev->subclass);

    /* Parse configuration and find endpoints */
    parse_endpoints(dev);

    /* Try HID — differentiate keyboard (proto=1) vs mouse/tablet (proto=2/0) */
    if (dev->class_code == USB_HID_CLASS || dev->subclass == USB_HID_CLASS || dev->is_hid) {
        int hid_proto = dev->hid_protocol ? dev->hid_protocol : dev->protocol;
        kprintf("usb: HID device detected (subclass=0x%02x proto=0x%02x)\n",
                dev->hid_subclass, hid_proto);
        if (dev->ep_in_addr) {
            if (hid_proto == 2) {
                /* Mouse / touchscreen / tablet — differentiate by endpoint size:
                   small (≤8) = relative mouse, larger = absolute tablet/touch */
                if (dev->ep_in_maxp <= 8 && usb_mouse_dev_idx < 0) {
                    usb_mouse_dev_idx = num_usb_devs - 1;
                    set_config(dev, 1);
                    ehci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                          USB_REQ_SET_PROTOCOL, 0, 0, 0, NULL);
                    ehci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                          USB_REQ_SET_IDLE, 0, 0, 0, NULL);
                    kprintf("usb-mouse: relative input device ready (maxp=%d)\n",
                            dev->ep_in_maxp);
                } else {
                    usb_touch_dev_idx = num_usb_devs - 1;
                    set_config(dev, 1);
                    ehci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                          USB_REQ_SET_PROTOCOL, 0, 0, 0, NULL);
                    ehci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                          USB_REQ_SET_IDLE, 0, 0, 0, NULL);
                    kprintf("usb-touch: absolute input device ready\n");
                }
            } else {
                /* Keyboard (protocol 1 or unknown) */
                usb_kbd_dev_idx = num_usb_devs - 1;
                set_config(dev, 1);
                ehci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                      USB_REQ_SET_PROTOCOL, 0, 0, 0, NULL);
                ehci_control_transfer(dev, USB_DIR_OUT, 0x21,
                                      USB_REQ_SET_IDLE, 0, 0, 0, NULL);
                kprintf("usb-kbd: keyboard ready\n");
            }
        }
    }

    /* CDC ECM */
    if (dev->class_code == 0x02 || dev->class_code == 0xEF || dev->class_code == 0x00) {
        if (dev->ep_in_addr && dev->ep_out_addr) {
            set_config(dev, 1);
            kprintf("ehci: CDC ECM device configured, ep_in=0x%02x ep_out=0x%02x\n",
                    dev->ep_in_addr, dev->ep_out_addr);
        }
    }
}

int ehci_init(void) {
    int dev_idx = 0;
    int found = 0;

    while (pci_find_class_idx(EHCI_CLASS, EHCI_SUBCLASS, dev_idx,
                              &ehci_bus, &ehci_slot, &ehci_func)) {
        uint32_t rev = pci_config_read(ehci_bus, ehci_slot, ehci_func, 0x08);
        uint8_t prog_if = (rev >> 8) & 0xFF;
        if (prog_if != EHCI_PROGIF) {
            dev_idx++;
            continue;
        }
        uint32_t bar0 = pci_config_read(ehci_bus, ehci_slot, ehci_func, 0x10);
        ehci_mmio_base = (uintptr_t)phys_to_virt(bar0 & ~0xF);

        if (bar0 & 0x04) {
            uint32_t bar1 = pci_config_read(ehci_bus, ehci_slot, ehci_func, 0x14);
            ehci_mmio_base |= ((uint64_t)bar1 << 32);
        }

        pci_config_write(ehci_bus, ehci_slot, ehci_func, 0x04, 0x0006);

        ehci_caplen = cap_read(EHCI_CAPLENGTH) & 0xFF;
        uint32_t hcip = cap_read(EHCI_HCIPARAMS);
        ehci_nports = (hcip >> 0) & 0x0F;
        uint32_t hccp = cap_read(EHCI_HCCPARAMS);

        kprintf("ehci: found at %02x:%02x.%x MMIO=0x%x caplen=%d nports=%d hccp=0x%08x\n",
                ehci_bus, ehci_slot, ehci_func,
                (uint32_t)ehci_mmio_base, ehci_caplen, ehci_nports, hccp);

        if (ehci_nports == 0) {
            dev_idx++;
            continue;
        }

        e_write(EHCI_USBCMD, CMD_HCRESET);
        for (volatile int i = 0; i < 1000000; i++) {
            if (!(e_read(EHCI_USBCMD) & CMD_HCRESET)) break;
        }

        e_write(EHCI_USBCMD, CMD_FRAME_LIST_SIZE_1024);
        e_write(EHCI_CTRLDSSEG, 0);
        init_async_head();

        /* Initialize periodic frame list (all terminate) */
        for (int i = 0; i < EHCI_FRAME_LIST_SIZE; i++)
            ehci_frame_list[i] = QH_NEXT_TERMINATE;
        e_write(EHCI_PERIODICLISTBASE, ehci_virt_to_phys(ehci_frame_list));

        e_write(EHCI_CONFIGFLAG, 1);

        e_write(EHCI_USBCMD, e_read(EHCI_USBCMD) | CMD_PSE | CMD_RS);
        for (volatile int i = 0; i < 100000; i++) {
            if (!(e_read(EHCI_USBSTS) & STS_HCHALTED)) break;
        }

        if (e_read(EHCI_USBSTS) & STS_HCHALTED) {
            kprintf("ehci: failed to start\n");
            dev_idx++;
            continue;
        }

        ehci_ok = 1;
        found = 1;

        for (int p = 0; p < ehci_nports; p++) {
            kprintf("ehci: port %d PORTSC=0x%08x\n", p, e_read(EHCI_PORTSC(p)));
            enumerate_port(p);
        }
        break;
    }

    if (!found) {
        kprintf("ehci: no USB 2.0 controller with ports found\n");
        return -1;
    }

    kprintf("ehci: %d device(s) found\n", num_usb_devs);
    return 0;
}
