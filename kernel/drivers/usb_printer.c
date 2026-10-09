#include "usb_printer.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct usb_printer printers[PRINTER_MAX];
static int printer_count;
static int printer_ok;

static int printer_get_device_id(struct usb_printer *p) {
    if (!p->dev) return -1;
    uint8_t buf[12];
    memset(buf, 0, sizeof(buf));
    int r = ehci_control_transfer(p->dev, USB_DIR_IN, 0xA0,
                                  PRINTER_GET_DEVICE_ID, 0, 1,
                                  sizeof(buf), buf);
    if (r < 2) return -1;
    int id_len = buf[0] | ((int)buf[1] << 8);
    if (id_len < 2) return -1;
    id_len -= 2; /* subtract the 2-byte length field */
    if (id_len > 254) id_len = 254;

    uint8_t id_buf[256];
    memset(id_buf, 0, sizeof(id_buf));
    r = ehci_control_transfer(p->dev, USB_DIR_IN, 0xA0,
                              PRINTER_GET_DEVICE_ID, 0, 1,
                              id_len + 2, id_buf);
    if (r >= 2) {
        int actual = id_buf[0] | ((int)id_buf[1] << 8);
        actual -= 2;
        if (actual > 254) actual = 254;
        if (actual > 0) {
            memcpy(p->device_id, id_buf + 2, actual);
            p->device_id[actual] = 0;
            p->device_id_len = actual;
            return 0;
        }
    }
    return -1;
}

int usb_printer_init(void) {
    printer_count = 0;
    printer_ok = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* Printer class: 0x07, subclass: 0x01, protocol: 0x01 (bidirectional) */
        if (d->class_code == 0x07 && d->subclass == 0x01) {
            if (printer_count >= PRINTER_MAX) break;

            struct usb_printer *p = &printers[printer_count];
            memset(p, 0, sizeof(*p));
            p->dev = d;
            p->ep_in = d->ep_in_addr & 0x0F;
            p->ep_out = d->ep_out_addr & 0x0F;
            p->slot = printer_count;

            printer_get_device_id(p);

            kprintf("usb_printer: printer at dev %d (VID=%04x PID=%04x) id=\"%s\"\n",
                    i, d->vendor_id, d->product_id,
                    p->device_id[0] ? p->device_id : "(none)");
            printer_count++;
        }
    }

    if (printer_count > 0) {
        printer_ok = 1;
        kprintf("usb_printer: %d printer(s) detected\n", printer_count);
    }
    return (printer_count > 0) ? 0 : -1;
}

int usb_printer_available(void) { return printer_ok; }
int usb_printer_get_count(void) { return printer_count; }

const char *usb_printer_get_device_id(int idx) {
    if (idx < 0 || idx >= printer_count) return NULL;
    return printers[idx].device_id;
}

int usb_printer_open(int idx) {
    if (idx < 0 || idx >= printer_count) return -1;
    printers[idx].open = 1;

    /* Select bidirectional mode */
    uint8_t data[1] = { 0x01 };
    ehci_control_transfer(printers[idx].dev, USB_DIR_OUT, 0x21,
                          PRINTER_SET_PORT, 0x01, 0, 1, data);
    return 0;
}

int usb_printer_close(int idx) {
    if (idx < 0 || idx >= printer_count) return -1;
    printers[idx].open = 0;
    return 0;
}

int usb_printer_write(int idx, const void *data, int len) {
    if (idx < 0 || idx >= printer_count) return -1;
    struct usb_printer *p = &printers[idx];
    if (!p->open || !p->dev) return -1;
    if (len > PRINTER_BUF_SIZE) len = PRINTER_BUF_SIZE;

    memcpy(p->tx_buf, data, len);
    int r = ehci_bulk_transfer(p->dev, p->ep_out, USB_DIR_OUT, p->tx_buf, len);
    return (r >= 0) ? len : -1;
}

int usb_printer_read(int idx, void *buf, int max_len) {
    if (idx < 0 || idx >= printer_count) return -1;
    struct usb_printer *p = &printers[idx];
    if (!p->open || !p->dev) return -1;

    int r = ehci_bulk_transfer(p->dev, p->ep_in, USB_DIR_IN, p->rx_buf,
                               max_len < PRINTER_BUF_SIZE ? max_len : PRINTER_BUF_SIZE);
    if (r > 0) memcpy(buf, p->rx_buf, r);
    return r;
}

int usb_printer_get_status(int idx) {
    if (idx < 0 || idx >= printer_count) return -1;
    struct usb_printer *p = &printers[idx];
    if (!p->dev) return -1;

    /* Get port status via control request */
    uint8_t status = 0;
    ehci_control_transfer(p->dev, USB_DIR_IN, 0xA0,
                          PRINTER_GET_PORT, 0, 1, 1, &status);
    p->status = status;
    return (int)status;
}

void usb_printer_print_info(void) {
    if (!printer_ok) {
        kprintf("usb_printer: no printers\n");
        return;
    }
    kprintf("usb_printer: %d printer(s)\n", printer_count);
    for (int i = 0; i < printer_count; i++) {
        struct usb_printer *p = &printers[i];
        kprintf("  printer %d: %s id=\"%s\"\n",
                i, p->open ? "open" : "closed",
                p->device_id[0] ? p->device_id : "(unknown)");
    }
}
