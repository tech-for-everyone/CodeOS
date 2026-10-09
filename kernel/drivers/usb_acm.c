#include "usb_acm.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct acm_port acm_ports[ACM_MAX_PORTS];
static int acm_num_ports;
static int acm_ok;

static uint8_t acm_rx_buf[ACM_RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t acm_tx_buf[ACM_RX_BUF_SIZE] __attribute__((aligned(16)));

static int acm_send_control(struct usb_dev *dev, uint8_t req, uint16_t value,
                            uint16_t index, uint16_t len, void *data) {
    return ehci_control_transfer(dev, USB_DIR_OUT, 0x21, req, value, index, len, data);
}

static void acm_set_line_coding(int port, uint32_t baud, uint8_t bits,
                                uint8_t stop, uint8_t parity) {
    if (port < 0 || port >= acm_num_ports) return;
    struct usb_dev *dev = acm_ports[port].dev;
    if (!dev) return;

    uint8_t lc[7];
    lc[0] = baud & 0xFF;
    lc[1] = (baud >> 8) & 0xFF;
    lc[2] = (baud >> 16) & 0xFF;
    lc[3] = (baud >> 24) & 0xFF;
    lc[4] = stop;
    lc[5] = parity;
    lc[6] = bits;
    acm_send_control(dev, ACM_SET_LINE_CODING, 0, 0, 7, lc);
}

int usb_acm_init(void) {
    acm_num_ports = 0;
    acm_ok = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* CDC ACM: class=0x02, subclass=0x02, protocol=0x01 (AT) */
        if (d->class_code == 0x02 && d->subclass == 0x02 && d->protocol == 0x01) {
            if (acm_num_ports >= ACM_MAX_PORTS) break;

            struct acm_port *p = &acm_ports[acm_num_ports];
            memset(p, 0, sizeof(*p));
            p->dev = d;
            p->ep_in = d->ep_in_addr & 0x0F;
            p->ep_out = d->ep_out_addr & 0x0F;
            p->idx = acm_num_ports;
            p->baud = 115200;
            p->data_bits = 8;
            p->stop_bits = 0;
            p->parity = 0;

            /* Set DTR + RTS */
            uint8_t ctrl[2] = { 0x03, 0x00 };
            acm_send_control(d, ACM_SET_CONTROL_LINE_STATE, 0x03, 0, 0, ctrl);

            /* Set default line coding: 115200 8N1 */
            acm_set_line_coding(acm_num_ports, 115200, 8, 0, 0);

            kprintf("usb_acm: CDC ACM port %d at dev %d (VID=%04x PID=%04x)\n",
                    acm_num_ports, i, d->vendor_id, d->product_id);
            acm_num_ports++;
        }
    }

    if (acm_num_ports > 0) {
        acm_ok = 1;
        kprintf("usb_acm: %d serial port(s) available\n", acm_num_ports);
    }
    return (acm_num_ports > 0) ? 0 : -1;
}

int usb_acm_available(void) { return acm_ok; }
int usb_acm_get_port_count(void) { return acm_num_ports; }

int usb_acm_open(int port) {
    if (port < 0 || port >= acm_num_ports) return -1;
    acm_ports[port].open = 1;
    acm_ports[port].rx_head = 0;
    acm_ports[port].rx_tail = 0;
    acm_ports[port].rx_count = 0;
    return 0;
}

int usb_acm_close(int port) {
    if (port < 0 || port >= acm_num_ports) return -1;
    acm_ports[port].open = 0;
    return 0;
}

int usb_acm_write(int port, const void *data, int len) {
    if (port < 0 || port >= acm_num_ports) return -1;
    if (!acm_ports[port].open || !acm_ports[port].dev) return -1;
    if (len > ACM_RX_BUF_SIZE) len = ACM_RX_BUF_SIZE;

    memcpy(acm_tx_buf, data, len);
    int r = ehci_bulk_transfer(acm_ports[port].dev, acm_ports[port].ep_out,
                               USB_DIR_OUT, acm_tx_buf, len);
    return (r >= 0) ? len : -1;
}

static void acm_drain_rx(int port) {
    struct acm_port *p = &acm_ports[port];
    if (!p->dev) return;

    int r = ehci_bulk_transfer(p->dev, p->ep_in, USB_DIR_IN,
                               acm_rx_buf, ACM_RX_BUF_SIZE);
    if (r > 0) {
        for (int i = 0; i < r; i++) {
            if (p->rx_count < ACM_RX_BUF_SIZE - 1) {
                p->rx_buf[p->rx_head] = acm_rx_buf[i];
                p->rx_head = (p->rx_head + 1) % ACM_RX_BUF_SIZE;
                p->rx_count++;
            }
        }
    }
}

int usb_acm_read_poll(int port) {
    if (port < 0 || port >= acm_num_ports) return -1;
    if (!acm_ports[port].open) return -1;
    acm_drain_rx(port);
    return acm_ports[port].rx_count;
}

int usb_acm_read(int port, void *buf, int max_len) {
    if (port < 0 || port >= acm_num_ports) return -1;
    struct acm_port *p = &acm_ports[port];
    if (!p->open) return -1;

    if (p->rx_count == 0)
        acm_drain_rx(port);

    if (p->rx_count == 0) return 0;

    uint8_t *dst = (uint8_t *)buf;
    int cpy = p->rx_count < max_len ? p->rx_count : max_len;
    for (int i = 0; i < cpy; i++) {
        dst[i] = p->rx_buf[p->rx_tail];
        p->rx_tail = (p->rx_tail + 1) % ACM_RX_BUF_SIZE;
        p->rx_count--;
    }
    return cpy;
}

void usb_acm_set_baud(int port, uint32_t baud) {
    if (port < 0 || port >= acm_num_ports) return;
    acm_ports[port].baud = baud;
    acm_set_line_coding(port, baud, acm_ports[port].data_bits,
                        acm_ports[port].stop_bits, acm_ports[port].parity);
}

void usb_acm_set_line_state(int port, int dtr, int rts) {
    if (port < 0 || port >= acm_num_ports) return;
    if (!acm_ports[port].dev) return;
    uint8_t state = (dtr ? 1 : 0) | (rts ? 2 : 0);
    acm_send_control(acm_ports[port].dev, ACM_SET_CONTROL_LINE_STATE,
                     state, 0, 0, NULL);
}

void usb_acm_print_info(void) {
    if (!acm_ok) {
        kprintf("usb_acm: no serial ports\n");
        return;
    }
    kprintf("usb_acm: %d port(s)\n", acm_num_ports);
    for (int i = 0; i < acm_num_ports; i++) {
        struct acm_port *p = &acm_ports[i];
        kprintf("  port %d: %s baud=%u\n",
                i, p->open ? "open" : "closed", p->baud);
    }
}
