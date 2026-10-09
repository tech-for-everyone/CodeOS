#include "usb_wacom.h"
#include "usb_ehci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct wacom_state wacom_devs[WACOM_MAX];
static int wacom_count;
static int wacom_ok;

static uint8_t wacom_buf[64] __attribute__((aligned(16)));

static void wacom_parse_hid(struct wacom_state *w, const uint8_t *data, int len) {
    if (len < 6) return;

    /* Wacom digitizer report format (typical):
     * Byte 0: tip switch (bit 0), barrel switch (bit 1), eraser (bit 2), invert (bit 3)
     * Byte 1-2: X (13-bit, 0-8191)
     * Byte 3-4: Y (13-bit, 0-8191)
     * Byte 5: pressure (8-bit, 0-255)
     * Byte 6: tilt X (8-bit, -90 to 90)
     * Byte 7: tilt Y (8-bit, -90 to 90)
     */

    uint8_t flags = data[0];
    w->pen_down = (flags & 0x01) ? 1 : 0;
    w->btn1 = (flags & 0x02) ? 1 : 0;
    w->btn2 = (flags & 0x04) ? 1 : 0;
    w->eraser = (flags & 0x08) ? 1 : 0;

    if (len >= 3) {
        w->x = ((uint32_t)data[1] << 5) | ((data[2] >> 3) & 0x1F);
        w->y = ((uint32_t)(data[2] & 0x07) << 8) | data[3];
        if (len >= 5) {
            w->x = ((uint32_t)data[1] << 8) | data[2];
            w->y = ((uint32_t)data[3] << 8) | data[4];
        }
    }

    if (len >= 6)
        w->pressure = data[5];
    if (len >= 7)
        w->tilt_x = (int8_t)data[6];
    if (len >= 8)
        w->tilt_y = (int8_t)data[7];
}

static void wacom_detect_tablet(struct wacom_state *w) {
    /* Try to get HID descriptor to determine max coordinates.
     * For now, set defaults based on common Wacom tablets. */
    w->max_x = 32767;      /* 15-bit range typical */
    w->max_y = 32767;
    w->max_pressure = 2047; /* 11-bit typical */
}

int usb_wacom_init(void) {
    wacom_count = 0;
    wacom_ok = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* Wacom digitizer: class=0x03, subclass=0x01, protocol=0x02 (pen) */
        if (d->class_code == 0x03 && d->subclass == 0x01 && d->protocol == 0x02) {
            if (wacom_count >= WACOM_MAX) break;

            struct wacom_state *w = &wacom_devs[wacom_count];
            memset(w, 0, sizeof(*w));
            w->dev = d;
            w->slot = wacom_count;
            wacom_detect_tablet(w);

            kprintf("usb_wacom: tablet at dev %d (VID=%04x PID=%04x) max=%dx%d\n",
                    i, d->vendor_id, d->product_id, w->max_x, w->max_y);
            wacom_count++;
        }
    }

    if (wacom_count > 0) {
        wacom_ok = 1;
        kprintf("usb_wacom: %d tablet(s) detected\n", wacom_count);
    }
    return (wacom_count > 0) ? 0 : -1;
}

int usb_wacom_available(void) { return wacom_ok; }
int usb_wacom_get_count(void) { return wacom_count; }

int usb_wacom_poll(int tablet, struct wacom_state *state) {
    if (tablet < 0 || tablet >= wacom_count || !state) return -1;
    struct wacom_state *w = &wacom_devs[tablet];

    /* Try to read fresh data */
    struct usb_dev *d = w->dev;
    if (d && d->ep_in_addr) {
        int r = ehci_bulk_transfer(d, d->ep_in_addr & 0x0F,
                                   USB_DIR_IN, wacom_buf, 64);
        if (r > 0) {
            wacom_parse_hid(w, wacom_buf, r);
        }
    }

    memcpy(state, w, sizeof(*state));
    return 0;
}

int usb_wacom_get_x(int tablet) {
    if (tablet < 0 || tablet >= wacom_count) return 0;
    return wacom_devs[tablet].x;
}

int usb_wacom_get_y(int tablet) {
    if (tablet < 0 || tablet >= wacom_count) return 0;
    return wacom_devs[tablet].y;
}

int usb_wacom_get_pressure(int tablet) {
    if (tablet < 0 || tablet >= wacom_count) return 0;
    return wacom_devs[tablet].pressure;
}

int usb_wacom_is_pen_down(int tablet) {
    if (tablet < 0 || tablet >= wacom_count) return 0;
    return wacom_devs[tablet].pen_down;
}

void usb_wacom_print_info(void) {
    if (!wacom_ok) {
        kprintf("usb_wacom: no tablets\n");
        return;
    }
    kprintf("usb_wacom: %d tablet(s)\n", wacom_count);
    for (int i = 0; i < wacom_count; i++) {
        struct wacom_state *w = &wacom_devs[i];
        kprintf("  tablet %d: VID=%04x PID=%04x %dx%d pressure=%d\n",
                i, w->dev->vendor_id, w->dev->product_id,
                w->max_x, w->max_y, w->max_pressure);
    }
}
