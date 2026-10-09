#include "usb_gamepad.h"
#include "usb_ehci.h"
#include "hid_parser.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/pmm.h"
#include "../kernel/vmm.h"

static struct gamepad_state gamepads[GAMEPAD_MAX];
static int gamepad_count;
static int gamepad_ok;

static uint8_t gp_report_buf[64] __attribute__((aligned(16)));

static int gamepad_parse_fields(struct gamepad_state *gp, const uint8_t *data, int len) {
    int num_axes = 0;
    int num_btns = 0;
    int num_hats = 0;

    if (len < 4) return -1;

    for (int i = 0; i < len && num_axes < GAMEPAD_AXES; i++) {
        uint8_t b = data[i];
        /* Detect hat switch (value 0-7 or 0-F, centered at 0x08 or 0x0F) */
        if ((b & 0x0F) <= 8 && num_axes >= 3) {
            /* Likely hat switch */
            gp->axes[GAMEPAD_AXIS_HATX] = 0;
            gp->axes[GAMEPAD_AXIS_HATY] = 0;
            switch (b & 0x0F) {
                case 0: gp->axes[GAMEPAD_AXIS_HATY] = -1; break;
                case 1: gp->axes[GAMEPAD_AXIS_HATX] = 1; gp->axes[GAMEPAD_AXIS_HATY] = -1; break;
                case 2: gp->axes[GAMEPAD_AXIS_HATX] = 1; break;
                case 3: gp->axes[GAMEPAD_AXIS_HATX] = 1; gp->axes[GAMEPAD_AXIS_HATY] = 1; break;
                case 4: gp->axes[GAMEPAD_AXIS_HATY] = 1; break;
                case 5: gp->axes[GAMEPAD_AXIS_HATX] = -1; gp->axes[GAMEPAD_AXIS_HATY] = 1; break;
                case 6: gp->axes[GAMEPAD_AXIS_HATX] = -1; break;
                case 7: gp->axes[GAMEPAD_AXIS_HATX] = -1; gp->axes[GAMEPAD_AXIS_HATY] = -1; break;
                default: break;
            }
            num_hats = 1;
            continue;
        }
        /* Axes: signed 8-bit centered at 128 */
        gp->axes[num_axes] = (int32_t)((int8_t)b);
        num_axes++;
    }

    /* Last byte(s) contain buttons as bitmask */
    for (int i = num_axes; i < len; i++) {
        gp->buttons |= ((uint32_t)data[i]) << (num_btns);
        num_btns += 8;
    }

    gp->num_axes = num_axes;
    gp->num_buttons = num_btns;
    gp->hat_count = num_hats;
    return 0;
}

int usb_gamepad_init(void) {
    gamepad_count = 0;
    gamepad_ok = 0;

    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (!d) continue;
        /* HID: class=0x03, subclass=0x01 (boot), protocol=0x00 (none) or 0x01 (keyboard) */
        /* Also check for non-boot gamepad: class=0x03, subclass=0x00 */
        if (d->class_code == 0x03 && d->subclass == 0x00) {
            if (gamepad_count >= GAMEPAD_MAX) break;

            struct gamepad_state *gp = &gamepads[gamepad_count];
            memset(gp, 0, sizeof(*gp));
            gp->dev = d;
            gp->slot = gamepad_count;

            kprintf("usb_gamepad: gamepad at dev %d (VID=%04x PID=%04x)\n",
                    i, d->vendor_id, d->product_id);
            gamepad_count++;
        }
    }

    if (gamepad_count > 0) {
        gamepad_ok = 1;
        kprintf("usb_gamepad: %d controller(s) detected\n", gamepad_count);
    }
    return (gamepad_count > 0) ? 0 : -1;
}

int usb_gamepad_available(void) { return gamepad_ok; }
int usb_gamepad_get_count(void) { return gamepad_count; }

int usb_gamepad_poll(int pad, struct gamepad_state *state) {
    if (pad < 0 || pad >= gamepad_count || !state) return -1;
    struct gamepad_state *gp = &gamepads[pad];

    memset(state, 0, sizeof(*state));
    memcpy(state->axes, gp->axes, sizeof(gp->axes));
    state->buttons = gp->buttons;
    state->num_axes = gp->num_axes;
    state->num_buttons = gp->num_buttons;

    /* Try to read fresh data from USB */
    struct usb_dev *d = gp->dev;
    if (d && d->ep_in_addr) {
        int r = ehci_bulk_transfer(d, d->ep_in_addr & 0x0F,
                                   USB_DIR_IN, gp_report_buf, 64);
        if (r > 0) {
            gp->buttons = 0;
            gamepad_parse_fields(gp, gp_report_buf, r);
            memcpy(state->axes, gp->axes, sizeof(gp->axes));
            state->buttons = gp->buttons;
            state->num_axes = gp->num_axes;
            state->num_buttons = gp->num_buttons;
        }
    }

    return 0;
}

int usb_gamepad_get_axis(int pad, int axis) {
    if (pad < 0 || pad >= gamepad_count) return 0;
    if (axis < 0 || axis >= GAMEPAD_AXES) return 0;
    return gamepads[pad].axes[axis];
}

int usb_gamepad_get_button(int pad, int button) {
    if (pad < 0 || pad >= gamepad_count) return 0;
    if (button < 0 || button >= 32) return 0;
    return (gamepads[pad].buttons >> button) & 1;
}

void usb_gamepad_print_info(void) {
    if (!gamepad_ok) {
        kprintf("usb_gamepad: no controllers\n");
        return;
    }
    kprintf("usb_gamepad: %d controller(s)\n", gamepad_count);
    for (int i = 0; i < gamepad_count; i++) {
        struct gamepad_state *gp = &gamepads[i];
        kprintf("  pad %d: VID=%04x PID=%04x axes=%d btns=%d\n",
                i, gp->dev->vendor_id, gp->dev->product_id,
                gp->num_axes, gp->num_buttons);
    }
}
