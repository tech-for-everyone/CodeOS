#include "usb.h"
#include "usb_ehci.h"
#include "usb_uhci.h"
#include "usb_xhci.h"
#include "usb_hub.h"
#include "usb_msc.h"
#include "usb_bt.h"
#include "usb_acm.h"
#include "usb_audio.h"
#include "usb_uvc.h"
#include "usb_gamepad.h"
#include "usb_printer.h"
#include "usb_rndis.h"
#include "usb_mtp.h"
#include "usb_wacom.h"
#include "usb_dfu.h"
#include "../kernel/kprintf.h"

static int usb_initialized;

void usb_init(void) {
    int r;
    usb_initialized = 0;

    r = uhci_init();
    if (r == 0)
        kprintf("usb: UHCI host controller initialized\n");

    r = ehci_init();
    if (r == 0) {
        usb_initialized = 1;
        kprintf("usb: EHCI host controller initialized\n");
    }

    r = xhci_init();
    if (r == 0) {
        usb_initialized = 1;
        kprintf("usb: xHCI host controller initialized\n");
    }

    /* Probe USB hubs */
    r = usb_hub_init();
    if (r == 0)
        kprintf("usb: USB hub(s) detected\n");

    /* Probe USB mass storage */
    r = usb_msc_init();
    if (r == 0)
        kprintf("usb: USB mass storage detected\n");

    /* Probe USB Bluetooth HCI */
    r = usb_bt_init();
    if (r == 0)
        kprintf("usb: Bluetooth HCI detected\n");

    /* Probe USB CDC ACM (serial) */
    r = usb_acm_init();
    if (r == 0)
        kprintf("usb: CDC ACM serial port(s) detected\n");

    /* Probe USB Audio */
    r = usb_audio_init();
    if (r == 0)
        kprintf("usb: Audio stream(s) detected\n");

    /* Probe USB UVC (webcam) */
    r = usb_uvc_init();
    if (r == 0)
        kprintf("usb: Video camera(s) detected\n");

    /* Probe USB gamepads */
    r = usb_gamepad_init();
    if (r == 0)
        kprintf("usb: Gamepad(s) detected\n");

    /* Probe USB printers */
    r = usb_printer_init();
    if (r == 0)
        kprintf("usb: Printer(s) detected\n");

    /* Probe USB RNDIS */
    r = usb_rndis_init();
    if (r == 0)
        kprintf("usb: RNDIS Ethernet detected\n");

    /* Probe USB MTP */
    r = usb_mtp_init();
    if (r == 0)
        kprintf("usb: MTP device(s) detected\n");

    /* Probe USB Wacom tablets */
    r = usb_wacom_init();
    if (r == 0)
        kprintf("usb: Wacom tablet(s) detected\n");

    /* Probe USB DFU */
    r = usb_dfu_init();
    if (r == 0)
        kprintf("usb: DFU device(s) detected\n");

    if (usb_kbd_available())
        kprintf("usb: USB keyboard detected\n");
}

int usb_available(void) {
    return usb_initialized;
}

/* Unified transfer API — tries EHCI first, then xHCI */
int usb_control_transfer(struct usb_dev *dev, int dir_in,
                         uint8_t bmReqType, uint8_t bRequest,
                         uint16_t wValue, uint16_t wIndex,
                         uint16_t wLength, void *data) {
    /* Check if device belongs to xHCI */
    for (int i = 0; i < xhci_get_num_devs(); i++) {
        if (xhci_get_dev(i) == dev)
            return xhci_control_transfer(dev, dir_in, bmReqType, bRequest,
                                         wValue, wIndex, wLength, data);
    }
    return ehci_control_transfer(dev, dir_in, bmReqType, bRequest,
                                 wValue, wIndex, wLength, data);
}

int usb_bulk_transfer(struct usb_dev *dev, int endpoint,
                      int dir_in, void *data, int len) {
    for (int i = 0; i < xhci_get_num_devs(); i++) {
        if (xhci_get_dev(i) == dev)
            return xhci_bulk_transfer(dev, endpoint, dir_in, data, len);
    }
    return ehci_bulk_transfer(dev, endpoint, dir_in, data, len);
}

void usb_print_info(void) {
    if (!usb_initialized) {
        kprintf("USB: no controller\n");
        return;
    }
    int nd = ehci_get_num_devs();
    int xd = xhci_get_num_devs();
    kprintf("USB: EHCI %d device(s), xHCI %d device(s)\n", nd, xd);
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (d)
            kprintf("  EHCI %d: VID=%04x PID=%04x class=0x%02x\n",
                    i, d->vendor_id, d->product_id, d->class_code);
    }
    for (int i = 0; i < xd; i++) {
        struct usb_dev *d = xhci_get_dev(i);
        if (d)
            kprintf("  xHCI %d: VID=%04x PID=%04x class=0x%02x\n",
                    i, d->vendor_id, d->product_id, d->class_code);
    }
    kprintf("USB-KBD: %s  Mouse: %s\n",
            usb_kbd_available() ? "yes" : "no",
            usb_mouse_available() ? "yes" : "no");
    if (usb_bt_available())    usb_bt_print_info();
    if (usb_acm_available())   usb_acm_print_info();
    if (usb_audio_available()) usb_audio_print_info();
    if (usb_uvc_available())   usb_uvc_print_info();
    if (usb_gamepad_available()) usb_gamepad_print_info();
    if (usb_printer_available()) usb_printer_print_info();
    if (usb_rndis_available()) usb_rndis_print_info();
    if (usb_mtp_available())   usb_mtp_print_info();
    if (usb_wacom_available()) usb_wacom_print_info();
    if (usb_dfu_available())   usb_dfu_print_info();
}

int usb_kbd_available(void) {
    if (uhci_kbd_available())
        return 1;
    if (ehci_kbd_available())
        return 1;
    return xhci_kbd_available();
}

int usb_kbd_poll(void) {
    if (uhci_kbd_available())
        return uhci_kbd_poll();
    if (ehci_kbd_available())
        return ehci_kbd_poll();
    return xhci_kbd_poll();
}

int usb_touch_available(void) {
    if (uhci_touch_available())
        return 1;
    if (ehci_touch_available())
        return 1;
    return xhci_touch_available();
}

int usb_touch_poll(int *x, int *y, int *btn) {
    if (uhci_touch_available())
        return uhci_touch_poll(x, y, btn);
    if (ehci_touch_available())
        return ehci_touch_poll(x, y, btn);
    return xhci_touch_poll(x, y, btn);
}

int usb_mouse_available(void) {
    if (uhci_mouse_available())
        return 1;
    if (ehci_mouse_available())
        return 1;
    return xhci_mouse_available();
}

int usb_mouse_poll(int *dx, int *dy, int *buttons, int *wheel) {
    if (uhci_mouse_available())
        return uhci_mouse_poll(dx, dy, buttons, wheel);
    if (ehci_mouse_available())
        return ehci_mouse_poll(dx, dy, buttons, wheel);
    return xhci_mouse_poll(dx, dy, buttons, wheel);
}
