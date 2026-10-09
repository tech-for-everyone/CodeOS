#ifndef USB_UHCI_H
#define USB_UHCI_H

#include "types.h"

/* UHCI register offsets (I/O ports) */
#define UHCI_USBCMD      0x00
#define UHCI_USBSTS      0x02
#define UHCI_USBINTR     0x04
#define UHCI_FRNUM       0x06
#define UHCI_FLBASEADD   0x08
#define UHCI_SOFMOD      0x0C
#define UHCI_PORTSC(n)   (0x10 + (n) * 2)

/* USBCMD bits */
#define UHCI_CMD_RS      (1 << 0)
#define UHCI_CMD_HCRESET (1 << 1)
#define UHCI_CMD_GRESET  (1 << 2)
#define UHCI_CMD_CF      (1 << 6)
#define UHCI_CMD_MAXP    (1 << 7)

/* USBSTS bits */
#define UHCI_STS_USBINT  (1 << 0)
#define UHCI_STS_USBERR  (1 << 1)
#define UHCI_STS_RD      (1 << 2)
#define UHCI_STS_HSE     (1 << 3)
#define UHCI_STS_HCH     (1 << 4)

/* PORTSC bits (ICH9) */
#define UHCI_PORT_CCS    (1 << 0)
#define UHCI_PORT_CSC    (1 << 1)
#define UHCI_PORT_PE     (1 << 2)
#define UHCI_PORT_PEC    (1 << 3)
#define UHCI_PORT_LSDA   (1 << 8)
#define UHCI_PORT_PR     (1 << 9)
#define UHCI_PORT_SUSP   (1 << 12)

/* TD Link pointer */
#define TD_LINK_TERMINATE 0x01
#define TD_LINK_DEPTH     0x04
#define TD_LINK_ADDR(p)   ((uint32_t)(uintptr_t)(p) & ~0x0F)

/* TD Control/Status bits - QEMU UHCI model */
#define TD_CTRL_ACTIVE    (1 << 23)  /* TD Active (set=active, clear=done) */
#define TD_CTRL_STALLED   (1 << 22)  /* Error/TD Stalled */
#define TD_CTRL_DBUFERR   (1 << 21)  /* Data Buffer Error */
#define TD_CTRL_BABBLE    (1 << 20)  /* Babble */
#define TD_CTRL_NAK       (1 << 19)  /* NAK received */
#define TD_CTRL_CRCTO     (1 << 18)  /* CRC/TimeOut */
#define TD_CTRL_SPD       (1 << 29)  /* Short Packet Detect */
#define TD_CTRL_LS        (1 << 26)  /* Low Speed */
#define TD_CTRL_IOC       (1 << 24)  /* Interrupt on Complete */

/* Token bits */
#define TD_TOKEN_DEVADDR(n)  (((n) & 0x7F) << 8)
#define TD_TOKEN_ENDPT(n)    (((n) & 0x0F) << 15)
#define TD_TOKEN_PID(n)      ((n) & 0xFF)
#define TD_TOKEN_TOGGLE(n)   (((n) & 1) << 19)
#define TD_TOKEN_MAXLEN(n)   ((((n) <= 0 ? 0x7FF : ((n) - 1)) & 0x7FF) << 21)

/* PID encodings for token field (bits 12:11) */
#define UHCI_PID_SETUP 0x2D
#define UHCI_PID_OUT   0xE1
#define UHCI_PID_IN    0x69

/* Frame list */
#define UHCI_FRAMELIST_SIZE 1024

/* UHCI Transfer Descriptor (32 bytes, 16-byte aligned) */
struct uhci_td {
    uint32_t link;
    uint32_t ctrl;
    uint32_t token;
    uint32_t buffer;
} __attribute__((aligned(16), packed));

/* UHCI Queue Head (8 bytes, 8-byte aligned) */
struct uhci_qh {
    uint32_t head;
    uint32_t element;
} __attribute__((aligned(8), packed));

/* API */
int uhci_init(void);
int uhci_kbd_available(void);
int uhci_kbd_poll(void);

int uhci_touch_available(void);
int uhci_touch_poll(int *x, int *y, int *btn);

int uhci_mouse_available(void);
int uhci_mouse_poll(int *dx, int *dy, int *buttons, int *wheel);

#endif
