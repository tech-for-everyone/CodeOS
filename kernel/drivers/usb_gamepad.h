#ifndef USB_GAMEPAD_H
#define USB_GAMEPAD_H

#include "types.h"

/* USB HID Gamepad/Joystick driver — extends HID parser for game controllers */

#define GAMEPAD_MAX  4
#define GAMEPAD_AXES 8
#define GAMEPAD_BUTTONS 32

struct gamepad_state {
    struct usb_dev *dev;
    int      open;
    int      slot;
    int32_t  axes[GAMEPAD_AXES];       /* X, Y, Z, Rx, Ry, Rz, HatX, HatY */
    uint32_t buttons;                   /* bitmask */
    int      num_axes;
    int      num_buttons;
    int      hat_count;                 /* number of hat switches */
    uint8_t  report_buf[64];
    int      report_size;
};

int  usb_gamepad_init(void);
int  usb_gamepad_available(void);
void usb_gamepad_print_info(void);

int  usb_gamepad_get_count(void);
int  usb_gamepad_poll(int pad, struct gamepad_state *state);
int  usb_gamepad_get_axis(int pad, int axis);
int  usb_gamepad_get_button(int pad, int button);

#define GAMEPAD_AXIS_X      0
#define GAMEPAD_AXIS_Y      1
#define GAMEPAD_AXIS_Z      2
#define GAMEPAD_AXIS_RX     3
#define GAMEPAD_AXIS_RY     4
#define GAMEPAD_AXIS_RZ     5
#define GAMEPAD_AXIS_HATX   6
#define GAMEPAD_AXIS_HATY   7

#define GAMEPAD_BTN_A       0
#define GAMEPAD_BTN_B       1
#define GAMEPAD_BTN_X       2
#define GAMEPAD_BTN_Y       3
#define GAMEPAD_BTN_LB      4
#define GAMEPAD_BTN_RB      5
#define GAMEPAD_BTN_BACK    6
#define GAMEPAD_BTN_START   7
#define GAMEPAD_BTN_HOME    8
#define GAMEPAD_BTN_LS      9
#define GAMEPAD_BTN_RS      10
#define GAMEPAD_BTN_UP      11
#define GAMEPAD_BTN_DOWN    12
#define GAMEPAD_BTN_LEFT    13
#define GAMEPAD_BTN_RIGHT   14

#endif
