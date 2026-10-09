#ifndef USB_ACM_H
#define USB_ACM_H

#include "types.h"

/* USB CDC ACM (Abstract Control Model) — Virtual Serial Ports */

#define ACM_MAX_PORTS  8
#define ACM_RX_BUF_SIZE 4096

/* ACM request codes (CDC PSTN subclass) */
#define ACM_SET_LINE_CODING      0x20
#define ACM_GET_LINE_CODING      0x21
#define ACM_SET_CONTROL_LINE_STATE 0x22
#define ACM_SEND_BREAK           0x23

struct acm_port {
    struct usb_dev *dev;
    int      ep_in;
    int      ep_out;
    int      open;
    int      idx;
    uint32_t baud;
    uint8_t  data_bits;
    uint8_t  stop_bits;
    uint8_t  parity;
    uint8_t  rx_buf[ACM_RX_BUF_SIZE];
    int      rx_head;
    int      rx_tail;
    int      rx_count;
};

int  usb_acm_init(void);
int  usb_acm_available(void);
void usb_acm_print_info(void);

int  usb_acm_get_port_count(void);
int  usb_acm_open(int port);
int  usb_acm_close(int port);
int  usb_acm_write(int port, const void *data, int len);
int  usb_acm_read(int port, void *buf, int max_len);
int  usb_acm_read_poll(int port);

void usb_acm_set_baud(int port, uint32_t baud);
void usb_acm_set_line_state(int port, int dtr, int rts);

#endif
