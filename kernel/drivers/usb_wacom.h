#ifndef USB_WACOM_H
#define USB_WACOM_H

#include "types.h"

/* USB Wacom / Digitizer Tablet driver (HID usage page 0x0D) */

#define WACOM_MAX  4

struct wacom_state {
    struct usb_dev *dev;
    int      open;
    int      slot;
    int32_t  x;              /* absolute X coordinate */
    int32_t  y;              /* absolute Y coordinate */
    int32_t  pressure;       /* pen pressure */
    int32_t  tilt_x;         /* tilt X (-90 to 90) */
    int32_t  tilt_y;         /* tilt Y (-90 to 90) */
    int32_t  distance;       /* distance from surface */
    int32_t  rotation;       /* rotation angle */
    int     pen_down;        /* 1=pen touching surface */
    int     btn1;            /* pen button 1 */
    int     btn2;            /* pen button 2 */
    int     eraser;          /* 1=eraser end in use */
    int32_t  max_x;          /* tablet max X */
    int32_t  max_y;          /* tablet max Y */
    int32_t  max_pressure;   /* max pressure levels */
};

int  usb_wacom_init(void);
int  usb_wacom_available(void);
void usb_wacom_print_info(void);

int  usb_wacom_get_count(void);
int  usb_wacom_poll(int tablet, struct wacom_state *state);
int  usb_wacom_get_x(int tablet);
int  usb_wacom_get_y(int tablet);
int  usb_wacom_get_pressure(int tablet);
int  usb_wacom_is_pen_down(int tablet);

#endif
