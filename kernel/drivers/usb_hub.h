#ifndef USB_HUB_H
#define USB_HUB_H

#include "types.h"

int  usb_hub_init(void);
int  usb_hub_get_port_count(int hub_addr);
int  usb_hub_get_port_status(int hub_addr, int port);
int  usb_hub_reset_port(int hub_addr, int port);
int  usb_hub_available(void);

#endif
