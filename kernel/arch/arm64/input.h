#ifndef ZIRCON_ARM64_INPUT_H
#define ZIRCON_ARM64_INPUT_H

#include "types.h"

/* Logical input events produced by the ARM64 input layer.
 *
 * Both an absolute-pointer device (virtio-tablet, the touch-like path used by
 * the phone UI) and a keyboard are supported.  When no virtio input device is
 * attached the PL011 serial console is still decoded as a keyboard, so the UI
 * remains navigable in a bare `-nographic` boot. */
enum {
    IN_NONE = 0,
    IN_TOUCH_DOWN,
    IN_TOUCH_MOVE,
    IN_TOUCH_UP,
    IN_KEY,
};

/* Keys are >= 0x100 so raw ASCII can share the same field. */
enum {
    IN_KEY_UP = 0x100,
    IN_KEY_DOWN,
    IN_KEY_LEFT,
    IN_KEY_RIGHT,
    IN_KEY_ENTER,
    IN_KEY_BACK,
};

typedef struct {
    int type;   /* enum above */
    int x, y;   /* screen coordinates for touch events */
    int key;    /* IN_KEY_* or a raw ASCII byte */
} input_event_t;

/* Probe virtio input devices and arm the serial key decoder. */
void input_init(void);

/* Non-blocking: returns 1 and fills *out when an event is pending. */
int input_poll(input_event_t *out);

#endif
