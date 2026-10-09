#include "usb_hub.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

/* USB Hub class requests */
#define HUB_REQ_GET_STATUS      0
#define HUB_REQ_CLEAR_FEATURE   1
#define HUB_REQ_SET_FEATURE     3
#define HUB_REQ_GET_DESCRIPTOR  6

/* Hub feature selectors */
#define HUB_FEAT_C_PORT_CONNECTION  0
#define HUB_FEAT_C_PORT_RESET       4
#define HUB_FEAT_PORT_ENABLE        1
#define HUB_FEAT_PORT_RESET         4

/* Hub descriptor types */
#define USB_DT_HUB  0x29

#pragma pack(push, 1)
struct usb_hub_desc {
    uint8_t  bDescLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;
    uint8_t  bHubContrCurrent;
    /* bitmaps follow, variable length */
};

struct usb_hub_status {
    uint16_t wHubStatus;
    uint16_t wHubChange;
};

struct usb_port_status {
    uint16_t wPortStatus;
    uint16_t wPortChange;
};
#pragma pack(pop)

/* Hub state */
#define MAX_HUBS 8
#define MAX_HUB_PORTS 8

static struct {
    struct usb_dev *dev;
    int             nports;
    int             maxpkt;
    uint8_t         ep_in;       /* interrupt IN endpoint */
    int             active;
} hubs[MAX_HUBS];
static int num_hubs;

/* ─── Internal ─── */

static int hub_get_port_status(struct usb_dev *dev, int port, struct usb_port_status *ps) {
    uint8_t data[4];
    memset(data, 0, sizeof(data));
    int r = ehci_control_transfer(dev, USB_DIR_IN, 0xA3, HUB_REQ_GET_STATUS,
                                  0, port + 1, sizeof(data), data);
    if (r < 0) return -1;
    ps->wPortStatus = data[0] | ((uint16_t)data[1] << 8);
    ps->wPortChange = data[2] | ((uint16_t)data[3] << 8);
    return 0;
}

static int hub_set_port_feature(struct usb_dev *dev, int port, int feature) {
    return ehci_control_transfer(dev, USB_DIR_OUT, 0x23, HUB_REQ_SET_FEATURE,
                                 feature, port + 1, 0, NULL);
}

static int hub_clear_port_feature(struct usb_dev *dev, int port, int feature) {
    return ehci_control_transfer(dev, USB_DIR_OUT, 0x23, HUB_REQ_CLEAR_FEATURE,
                                 feature, port + 1, 0, NULL);
}

static int hub_get_hub_desc(struct usb_dev *dev, struct usb_hub_desc *desc) {
    return ehci_control_transfer(dev, USB_DIR_IN, 0xA0, HUB_REQ_GET_DESCRIPTOR,
                                 USB_DT_HUB << 8, 0, sizeof(*desc), desc);
}

/* ─── Port reset ─── */

int usb_hub_reset_port(int hub_idx, int port) {
    if (hub_idx < 0 || hub_idx >= num_hubs) return -1;
    struct usb_dev *dev = hubs[hub_idx].dev;
    if (!dev) return -1;

    /* Set PORT_RESET */
    hub_set_port_feature(dev, port, HUB_FEAT_PORT_RESET);

    /* Wait 50ms for reset to complete */
    for (volatile int i = 0; i < 50000; i++) asm volatile("pause");

    /* Check if port is enabled */
    struct usb_port_status ps;
    if (hub_get_port_status(dev, port, &ps) < 0) return -1;

    /* Clear reset change */
    hub_clear_port_feature(dev, port, HUB_FEAT_C_PORT_RESET);

    return (ps.wPortStatus & 2) ? 0 : -1;  /* bit 1 = PORT_ENABLE */
}

/* ─── Init / probe ─── */

int usb_hub_init(void) {
    num_hubs = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* Hub class: class=0x09 */
        if (d->class_code != 0x09) continue;
        if (num_hubs >= MAX_HUBS) break;

        struct usb_hub_desc desc;
        memset(&desc, 0, sizeof(desc));
        if (hub_get_hub_desc(d, &desc) < 0) {
            kprintf("usb_hub: failed to read hub descriptor\n");
            continue;
        }

        hubs[num_hubs].dev = d;
        hubs[num_hubs].nports = desc.bNbrPorts;
        hubs[num_hubs].maxpkt = d->max_packet;
        hubs[num_hubs].active = 1;

        kprintf("usb_hub: hub at dev %d, %d ports (VID=%04x PID=%04x)\n",
                i, desc.bNbrPorts, d->vendor_id, d->product_id);

        /* Enable power to all ports */
        for (int p = 0; p < desc.bNbrPorts; p++) {
            hub_set_port_feature(d, p, HUB_FEAT_PORT_ENABLE);
        }

        num_hubs++;
    }

    return (num_hubs > 0) ? 0 : -1;
}

/* ─── Public API ─── */

int usb_hub_available(void) { return num_hubs > 0; }

int usb_hub_get_port_count(int hub_idx) {
    if (hub_idx < 0 || hub_idx >= num_hubs) return 0;
    return hubs[hub_idx].nports;
}

int usb_hub_get_port_status(int hub_idx, int port) {
    if (hub_idx < 0 || hub_idx >= num_hubs) return -1;
    struct usb_port_status ps;
    if (hub_get_port_status(hubs[hub_idx].dev, port, &ps) < 0) return -1;
    return (int)ps.wPortStatus;
}
