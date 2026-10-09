#ifndef USB_PRINTER_H
#define USB_PRINTER_H

#include "types.h"

/* USB Printer Class (IEEE-1284.4) */

#define PRINTER_MAX  4
#define PRINTER_BUF_SIZE 8192

/* Printer class request codes */
#define PRINTER_GET_DEVICE_ID  0x00
#define PRINTER_SET_PORT       0x01
#define PRINTER_GET_PORT       0x02

struct usb_printer {
    struct usb_dev *dev;
    int      ep_in;
    int      ep_out;
    int      ep_int;     /* interrupt IN for status */
    int      open;
    int      slot;
    char     device_id[256];
    int      device_id_len;
    uint8_t  status;     /* printer status byte */
    uint8_t  tx_buf[PRINTER_BUF_SIZE] __attribute__((aligned(16)));
    uint8_t  rx_buf[PRINTER_BUF_SIZE] __attribute__((aligned(16)));
};

int  usb_printer_init(void);
int  usb_printer_available(void);
void usb_printer_print_info(void);

int  usb_printer_get_count(void);
int  usb_printer_open(int idx);
int  usb_printer_close(int idx);
int  usb_printer_write(int idx, const void *data, int len);
int  usb_printer_read(int idx, void *buf, int max_len);
int  usb_printer_get_status(int idx);

const char *usb_printer_get_device_id(int idx);

#endif
