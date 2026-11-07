#include "kbd_aula_f87.h"
#include "usb.h"
#include "usb_ehci.h"
#include "usb_uhci.h"
#include "usb_xhci.h"
#include "keyboard.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

static struct aula_f87_state *active_aula = NULL;

static int find_aula_device(struct usb_dev **found, int *bus_type) {
    if (!found || !bus_type) return 0;
    *found = NULL;
    *bus_type = 0;

    int count = ehci_get_num_devs();
    for (int i = 0; i < count; i++) {
        struct usb_dev *dev = ehci_get_dev(i);
        if (dev && aula_f87_device_matches((uint16_t)dev->vendor_id,
                                           (uint16_t)dev->product_id)) {
            *found = dev;
            *bus_type = 2;
            return 1;
        }
    }

    count = xhci_get_num_devs();
    for (int i = 0; i < count; i++) {
        struct usb_dev *dev = xhci_get_dev(i);
        if (dev && aula_f87_device_matches((uint16_t)dev->vendor_id,
                                           (uint16_t)dev->product_id)) {
            *found = dev;
            *bus_type = 3;
            return 1;
        }
    }

    return 0;
}

int aula_f87_probe(void) {
    struct usb_dev *dev;
    int bus_type;
    return find_aula_device(&dev, &bus_type);
}

int aula_f87_init(void *ram_instance) {
    if (!ram_instance) return -1;
    active_aula = NULL;
    struct usb_dev *found;
    int bus_type;
    if (!find_aula_device(&found, &bus_type)) return -1;

    active_aula = (struct aula_f87_state *)ram_instance;
    memset(active_aula, 0, sizeof(*active_aula));

    active_aula->present = 1;
    active_aula->bus_type = bus_type;
    active_aula->dev_address = found->address;
    active_aula->vendor_id = (uint16_t)found->vendor_id;
    active_aula->product_id = (uint16_t)found->product_id;
    active_aula->win_lock = 0;
    active_aula->rgb_mode = AULA_RGB_MODE_UNKNOWN;
    active_aula->rgb_brightness = -1;
    active_aula->rgb_speed = -1;
    active_aula->nkro_active = 0;

    kprintf("aula-f87: AULA F87 detected (VID:PID=%04x:%04x; generic USB HID input)\n",
            active_aula->vendor_id, active_aula->product_id);

    return 0;
}

void aula_f87_cleanup(void) {
    if (active_aula) {
        active_aula->present = 0;
        active_aula = NULL;
    }
}

int aula_f87_is_available(void) {
    return (active_aula != NULL && active_aula->present != 0);
}

int aula_f87_set_rgb_mode(int mode) {
    if (!active_aula) return -1;
    if (mode < 0 || mode > AULA_RGB_MODE_OFF) return -1;
    kprintf("aula-f87: RGB control protocol is not implemented; no hardware change made\n");
    return -1;
}

int aula_f87_get_rgb_mode(void) {
    if (!active_aula) return -1;
    return active_aula->rgb_mode;
}

int aula_f87_set_brightness(int level) {
    if (!active_aula) return -1;
    if (level < 0 || level > 5) return -1;
    kprintf("aula-f87: RGB control protocol is not implemented; no hardware change made\n");
    return -1;
}

int aula_f87_toggle_win_lock(void) {
    if (!active_aula) return -1;
    active_aula->win_lock = !active_aula->win_lock;
    kprintf("aula-f87: Win Lock is now %s\n", active_aula->win_lock ? "ACTIVE (Game Mode)" : "OFF");
    return active_aula->win_lock;
}

int aula_f87_is_win_locked(void) {
    if (!active_aula) return 0;
    return active_aula->win_lock;
}

struct aula_f87_state *aula_f87_get_state(void) {
    return active_aula;
}

void aula_f87_print_info(void) {
    if (!active_aula) {
        kprintf("aula-f87: AULA F87 Keyboard not loaded\n");
        return;
    }
    kprintf("AULA F87 Mechanical Keyboard (87-Key TKL):\n");
    kprintf("  Status:      Active\n");
    kprintf("  VID:PID:     %04x:%04x\n", active_aula->vendor_id, active_aula->product_id);
    kprintf("  Rollover:    generic USB HID (NKRO report parsing unavailable)\n");
    kprintf("  Win Lock:    %s\n", active_aula->win_lock ? "ENABLED (Locked)" : "DISABLED");
    kprintf("  RGB Control: unavailable (vendor HID protocol not implemented)\n");
    kprintf("  Keystrokes:  %llu\n", (unsigned long long)active_aula->total_keystrokes);
}

int aula_f87_poll(void) {
    if (!active_aula || !active_aula->present) return 0;

    int key = usb_kbd_poll();
    if (!key) return 0;

    /* If Win Lock is enabled, suppress Windows / Super key presses */
    if (active_aula->win_lock && (key == KEY_SUPER)) {
        return 0;
    }

    active_aula->total_keystrokes++;
    return key;
}
