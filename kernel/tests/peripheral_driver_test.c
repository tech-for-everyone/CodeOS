#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Minimal USB direction constants for the host test. */
#ifndef USB_DIR_OUT
#define USB_DIR_OUT 0
#endif
#ifndef USB_DIR_IN
#define USB_DIR_IN  1
#endif

#include "kbd_aula_f87.h"
#include "keyboard.h"
#include "mouse_g305.h"
#include "usb_ehci.h"

static struct usb_dev ehci_devices[4];
static struct usb_dev xhci_devices[4];
static int ehci_count;
static int xhci_count;
static int mouse_polls;
static int next_key;
static int control_transfers;

int ehci_get_num_devs(void) { return ehci_count; }
struct usb_dev *ehci_get_dev(int index) {
    return index >= 0 && index < ehci_count ? &ehci_devices[index] : NULL;
}
int xhci_get_num_devs(void) { return xhci_count; }
struct usb_dev *xhci_get_dev(int index) {
    return index >= 0 && index < xhci_count ? &xhci_devices[index] : NULL;
}
uint64_t timer_get_milliseconds(void) { return 1234; }
void kprintf(const char *format, ...) { (void)format; }

int usb_mouse_poll(int *dx, int *dy, int *buttons, int *wheel) {
    mouse_polls++;
    *dx = 5;
    *dy = -3;
    *buttons = G305_BTN_LEFT | G305_BTN_DPI_SWITCH;
    *wheel = 1;
    return 1;
}

int usb_kbd_poll(void) { return next_key; }

/* HID++ control transfers are best-effort; fail by default so tests exercise
   graceful fallback paths without requiring a real device. */
int usb_control_transfer(struct usb_dev *dev, int dir_in,
                         uint8_t bmReqType, uint8_t bRequest,
                         uint16_t wValue, uint16_t wIndex,
                         uint16_t wLength, void *data) {
    (void)dev; (void)dir_in; (void)bmReqType; (void)bRequest;
    (void)wValue; (void)wIndex; (void)wLength; (void)data;
    control_transfers++;
    return -1;
}

static void test_hidpp_framing(void) {
    uint8_t buf[20];
    memset(buf, 0, sizeof(buf));
    assert(hidpp_short_build(buf, sizeof(buf), HIDPP_DEVICE_DEFAULT,
                             HIDPP_FEATURE_BATTERY_STATUS, 1, 2, 3) == 7);
    assert(buf[0] == HIDPP_SHORT_REPORT_ID);
    assert(buf[1] == HIDPP_DEVICE_DEFAULT);
    assert(buf[2] == (HIDPP_FEATURE_BATTERY_STATUS & 0xFF));
    assert(buf[4] == 1 && buf[5] == 2 && buf[6] == 3);

    memset(buf, 0, sizeof(buf));
    uint8_t p = 0xAB;
    assert(hidpp_long_build(buf, sizeof(buf), HIDPP_DEVICE_DEFAULT,
                            HIDPP_FEATURE_POLLING_RATE, 0x10, &p, 1) == 20);
    assert(buf[0] == HIDPP_LONG_REPORT_ID);
    assert(buf[4] == 0x10);
    assert(buf[5] == 0xAB);

    /* Bounds */
    assert(hidpp_short_build(buf, 6, 1, 0, 0, 0, 0) == -1);
    assert(hidpp_long_build(buf, 19, 1, 0, 0, NULL, 0) == -1);
}

static void test_hidpp_parsing(void) {
    uint8_t resp[8];
    int pct, chg, dpi, mode;

    memset(resp, 0, sizeof(resp));
    resp[0] = HIDPP_SHORT_REPORT_ID;
    resp[4] = 73;
    resp[5] = 0x81; /* charging hint */
    assert(hidpp_parse_battery(resp, sizeof(resp), &pct, &chg) == 0);
    assert(pct == 73 && chg == 1);

    resp[5] = 0;
    assert(hidpp_parse_battery(resp, sizeof(resp), &pct, &chg) == 0);
    assert(chg == 0);

    memset(resp, 0, sizeof(resp));
    resp[0] = HIDPP_SHORT_REPORT_ID;
    resp[4] = 0x06; resp[5] = 0x40; /* 1600 */
    assert(hidpp_parse_dpi(resp, sizeof(resp), &dpi) == 0);
    assert(dpi == 1600);

    memset(resp, 0, sizeof(resp));
    resp[0] = HIDPP_SHORT_REPORT_ID;
    resp[4] = G305_MODE_PERFORMANCE;
    assert(hidpp_parse_polling_rate(resp, sizeof(resp), &mode) == 0);
    assert(mode == G305_MODE_PERFORMANCE);

    /* Invalid */
    assert(hidpp_parse_battery(NULL, 8, &pct, &chg) == -1);
    assert(hidpp_parse_dpi(resp, 4, &dpi) == -1);
}

int main(void) {
    struct g305_state mouse;
    struct aula_f87_state keyboard;
    int dx, dy, buttons, wheel;

    test_hidpp_framing();
    test_hidpp_parsing();

    ehci_count = 1;
    ehci_devices[0].vendor_id = LOGITECH_VID;
    ehci_devices[0].product_id = LOGITECH_PID_LIGHTSPEED_RECEIVER;
    assert(!g305_probe());
    assert(g305_init(&mouse) < 0);
    assert(!g305_is_available());

    xhci_count = 1;
    xhci_devices[0].vendor_id = LOGITECH_VID;
    xhci_devices[0].product_id = LOGITECH_PID_G305_RECEIVER;
    xhci_devices[0].address = 9;
    assert(g305_probe());
    assert(g305_init(&mouse) == 0);
    assert(g305_is_available());
    assert(mouse.bus_type == 3 && mouse.dev_address == 9);
    /* HID++ transfers fail in test harness; battery stays unknown. */
    assert(mouse.battery_percent == -1);
    assert(g305_get_performance_mode() == G305_MODE_UNKNOWN ||
           g305_get_performance_mode() == G305_MODE_ENDURANCE ||
           g305_get_performance_mode() == G305_MODE_PERFORMANCE);
    /* Driver accepts performance mode as a software/requested preference. */
    assert(g305_set_performance_mode(G305_MODE_PERFORMANCE) == 0);
    assert(g305_get_performance_mode() == G305_MODE_PERFORMANCE);
    assert(g305_poll(NULL, &dy, &buttons, &wheel) < 0);
    assert(mouse_polls == 0);
    assert(g305_poll(&dx, &dy, &buttons, &wheel) == 1);
    assert(dx == 20 && dy == -12);
    assert(buttons == G305_BTN_LEFT && wheel == 1);
    assert(mouse.current_dpi == 3200);
    g305_cleanup();
    assert(!g305_is_available());

    ehci_count = 0;
    xhci_count = 0;
    assert(!aula_f87_probe());
    assert(aula_f87_init(&keyboard) < 0);
    ehci_count = 1;
    ehci_devices[0].vendor_id = AULA_VID_258A;
    ehci_devices[0].product_id = AULA_PID_F87_PRO;
    ehci_devices[0].address = 4;
    assert(aula_f87_probe());
    assert(aula_f87_init(&keyboard) == 0);
    assert(aula_f87_is_available());
    assert(keyboard.bus_type == 2 && keyboard.dev_address == 4);
    /* Improved driver sets sensible RGB defaults and accepts control requests. */
    assert(keyboard.rgb_mode >= 0);
    assert(keyboard.rgb_brightness >= 0);
    assert(!keyboard.nkro_active);
    assert(aula_f87_set_rgb_mode(AULA_RGB_MODE_RAINBOW) == 0);
    assert(aula_f87_get_rgb_mode() == AULA_RGB_MODE_RAINBOW);
    assert(aula_f87_set_brightness(3) == 0);
    assert(keyboard.rgb_brightness == 3);
    assert(aula_f87_toggle_win_lock() == 1);
    next_key = KEY_SUPER;
    assert(aula_f87_poll() == 0);
    next_key = 'a';
    assert(aula_f87_poll() == 'a');
    assert(keyboard.total_keystrokes >= 1);
    aula_f87_cleanup();
    assert(!aula_f87_is_available());

    puts("Peripheral drivers verified (G305 HID++ + AULA RGB/NKRO API, mocked USB).");
    return 0;
}
