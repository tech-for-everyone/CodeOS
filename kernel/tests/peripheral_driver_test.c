#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

int main(void) {
    struct g305_state mouse;
    struct aula_f87_state keyboard;
    int dx, dy, buttons, wheel;

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
    assert(mouse.battery_percent == -1);
    assert(g305_get_performance_mode() == G305_MODE_UNKNOWN);
    assert(g305_set_performance_mode(G305_MODE_PERFORMANCE) < 0);
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
    assert(keyboard.rgb_mode == AULA_RGB_MODE_UNKNOWN);
    assert(keyboard.rgb_brightness == -1);
    assert(!keyboard.nkro_active);
    assert(aula_f87_set_rgb_mode(AULA_RGB_MODE_RAINBOW) < 0);
    assert(aula_f87_set_brightness(3) < 0);
    assert(aula_f87_toggle_win_lock() == 1);
    next_key = KEY_SUPER;
    assert(aula_f87_poll() == 0);
    next_key = 'a';
    assert(aula_f87_poll() == 'a');
    aula_f87_cleanup();
    assert(!aula_f87_is_available());

    puts("Peripheral drivers verified against mocked USB devices.");
    return 0;
}
