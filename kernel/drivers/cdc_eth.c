#include "cdc_eth.h"
#include "nic.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

/* CDC Ethernet Management Element Request codes */
#define CDC_SET_ETH_PACKET_FILTER 0x43
#define CDC_GET_ETH_STATISTIC     0x42

/* Packet filter bits */
#define CDC_PF_PROMISCUOUS  (1 << 0)
#define CDC_PF_ALL_MULTICAST (1 << 1)
#define CDC_PF_DIRECTED     (1 << 2)

/* ───── CDC Ethernet device state ───── */

static struct usb_dev *cdc_dev;
static uint8_t cdc_mac[6];
static int cdc_ok;

/* Buffer for RX/TX using USB bulk transfers */
static uint8_t cdc_rx_buf[2048] __attribute__((aligned(64)));
static uint8_t cdc_tx_buf[2048] __attribute__((aligned(64)));

/* ───── CDC Ethernet management functions ───── */

static int cdc_send_management(uint8_t req, uint16_t value, uint16_t index,
                               uint16_t length, void *data) {
    if (!cdc_dev) return -1;
    /* Management elements use interface control requests (bmReqType=0x21) */
    return ehci_control_transfer(cdc_dev, USB_DIR_OUT, 0x21, req,
                                 value, index, length, data);
}

/* ───── NIC ops ───── */

static int cdc_eth_send(const void *data, int len) {
    if (!cdc_ok || !cdc_dev) return -1;
    if (len > 2048) len = 2048;

    memcpy(cdc_tx_buf, data, len);

    int ret = ehci_bulk_transfer(cdc_dev, cdc_dev->ep_out_addr & 0x0F,
                                 USB_DIR_OUT, cdc_tx_buf, len);
    return ret < 0 ? -1 : len;
}

static int cdc_eth_recv(void *buf, int max_len) {
    if (!cdc_ok || !cdc_dev) return -1;

    int ret = ehci_bulk_transfer(cdc_dev, cdc_dev->ep_in_addr & 0x0F,
                                 USB_DIR_IN, cdc_rx_buf, 2048);
    if (ret <= 0) return -1;

    int len = ret;
    if (len > max_len) len = max_len;
    memcpy(buf, cdc_rx_buf, len);
    return len;
}

static void cdc_eth_get_mac(uint8_t *mac) {
    memcpy(mac, cdc_mac, 6);
}

/* Try to find the MAC in the USB descriptor buffer.
   The CDC ECM functional descriptor's iMACAddress field
   is a string descriptor index we'd need to read. */
static int cdc_try_read_mac_string(void) {
    /* Read string descriptor at index stored in functional descriptor.
       Since we can't easily find that index without re-parsing config,
       we set a well-known fallback. Many CDC-ECM devices use a specific
       vendor request to get the MAC. */

    /* Alternative: try vendor-specific control request for MAC */
    uint8_t buf[6];
    memset(buf, 0, 6);

    /* Try common vendor commands for MAC retrieval */
    /* ASIX: vendor request 0x01 (read MAC) */
    if (cdc_dev->vendor_id == CDC_ETH_VENDOR_ASIX) {
        if (ehci_control_transfer(cdc_dev, USB_DIR_IN, 0xC0, 0x01,
                                  0, 0, 6, buf) == 0) {
            memcpy(cdc_mac, buf, 6);
            return 0;
        }
    }

    /* SMSC: vendor request 0xA0 (read MAC) */
    if (cdc_dev->vendor_id == CDC_ETH_VENDOR_SMSC) {
        if (ehci_control_transfer(cdc_dev, USB_DIR_IN, 0xC0, 0xA0,
                                  0, 0, 6, buf) == 0) {
            memcpy(cdc_mac, buf, 6);
            return 0;
        }
    }

    /* Fallback: generate MAC based on vendor/product */
    cdc_mac[0] = (cdc_dev->vendor_id >> 8) & 0xFF;
    cdc_mac[1] = cdc_dev->vendor_id & 0xFF;
    cdc_mac[2] = (cdc_dev->product_id >> 8) & 0xFF;
    cdc_mac[3] = cdc_dev->product_id & 0xFF;
    cdc_mac[4] = cdc_dev->address;
    cdc_mac[5] = 0x01;
    return 0;
}

/* ───── Probe ───── */

int cdc_eth_init(void) {
    /* Scan USB devices for CDC ECM class */
    int ndevs = ehci_get_num_devs();
    for (int i = 0; i < ndevs; i++) {
        struct usb_dev *dev = ehci_get_dev(i);
        if (!dev) continue;

        /* Check for CDC ECM: class=0x02, subclass=0x06 or vendor-specific */
        int is_cdc = 0;
        if (dev->class_code == 0xEF || dev->class_code == 0xFF) {
            /* Vendor-specific or miscellaneous - check endpoints */
            if (dev->ep_in_addr && dev->ep_out_addr)
                is_cdc = 1;
        }
        if (dev->class_code == 0x02 || dev->subclass == 0x06) {
            is_cdc = 1;
        }

        if (!is_cdc && !(dev->ep_in_addr && dev->ep_out_addr))
            continue;

        cdc_dev = dev;

        /* Set promiscuous packet filter */
        cdc_send_management(CDC_SET_ETH_PACKET_FILTER,
                            CDC_PF_PROMISCUOUS | CDC_PF_DIRECTED,
                            0, 0, NULL);

        /* Get MAC address */
        cdc_try_read_mac_string();

        cdc_ok = 1;
        kprintf("cdc_eth: USB Ethernet at dev %d, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                dev->address,
                cdc_mac[0], cdc_mac[1], cdc_mac[2],
                cdc_mac[3], cdc_mac[4], cdc_mac[5]);
        return 0;
    }
    return -1;
}

static int cdc_eth_probe(void) {
    /* EHCI must be initialized first, then we scan for CDC devices */
    return cdc_eth_init();
}

nic_driver_t cdc_eth_nic = {
    .name = "cdc-eth",
    .probe = cdc_eth_probe,
    .send  = cdc_eth_send,
    .recv  = cdc_eth_recv,
    .get_mac = cdc_eth_get_mac,
};
