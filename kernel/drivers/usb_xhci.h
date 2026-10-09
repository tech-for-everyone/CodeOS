#ifndef USB_XHCI_H
#define USB_XHCI_H

#include "types.h"

struct usb_dev;

int  xhci_init(void);
int  xhci_control_transfer(struct usb_dev *dev, int dir_in,
                           uint8_t bmReqType, uint8_t bRequest,
                           uint16_t wValue, uint16_t wIndex,
                           uint16_t wLength, void *data);
int  xhci_bulk_transfer(struct usb_dev *dev, int endpoint,
                        int dir_in, void *data, int len);
int  xhci_get_num_devs(void);
struct usb_dev *xhci_get_dev(int idx);

int  xhci_kbd_available(void);
int  xhci_kbd_poll(void);
int  xhci_touch_available(void);
int  xhci_touch_poll(int *x, int *y, int *btn);
int  xhci_mouse_available(void);
int  xhci_mouse_poll(int *dx, int *dy, int *buttons, int *wheel);

#endif
