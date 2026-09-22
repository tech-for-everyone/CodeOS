#ifndef USB_EHCI_H
#define USB_EHCI_H

#include "types.h"

#define USB_DIR_OUT 0
#define USB_DIR_IN  1

#define USB_REQ_GET_DESCRIPTOR    6
#define USB_REQ_SET_ADDRESS       5
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_SET_IDLE          10
#define USB_REQ_SET_PROTOCOL      11

#define USB_DESC_DEVICE  1
#define USB_DESC_CONFIG  2
#define USB_DESC_STRING  3
#define USB_DESC_IFACE   4
#define USB_DESC_ENDPOINT 5

struct usb_dev {
    int  address;
    int  speed;
    int  max_packet;
    int  vendor_id;
    int  product_id;
    int  class_code;
    int  subclass;
    int  protocol;
    int  num_endpoints;
    uint8_t ep_in_addr;
    uint8_t ep_out_addr;
    uint8_t ep_in_attr;
    uint8_t ep_out_attr;
    int  ep_in_maxp;
    int  ep_out_maxp;
    int  is_hid;
    int  hid_subclass;
    int  hid_protocol;
};

int  ehci_init(void);
int  ehci_control_transfer(struct usb_dev *dev, int dir_in,
                           uint8_t bmReqType, uint8_t bRequest,
                           uint16_t wValue, uint16_t wIndex,
                           uint16_t wLength, void *data);
int  ehci_bulk_transfer(struct usb_dev *dev, int endpoint,
                        int dir_in, void *data, int len);
int  ehci_get_num_devs(void);
struct usb_dev *ehci_get_dev(int idx);

int  ehci_kbd_available(void);
int  ehci_kbd_poll(void);

int  ehci_touch_available(void);
int  ehci_touch_poll(int *x, int *y, int *btn);

int  ehci_mouse_available(void);
int  ehci_mouse_poll(int *dx, int *dy, int *buttons, int *wheel);

#endif
