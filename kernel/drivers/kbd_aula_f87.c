#include "kbd_aula_f87.h"
#include "usb.h"
#include "usb_ehci.h"
#include "usb_uhci.h"
#include "usb_xhci.h"
#include "keyboard.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

static struct aula_f87_state *active_aula = NULL;
static struct usb_dev *active_aula_dev = NULL;
static int aula_nkro_sample_div; /* throttle NKRO heuristic sampling */

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
    active_aula_dev = NULL;
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
    active_aula->rgb_mode = AULA_RGB_MODE_RAINBOW; /* sensible default */
    active_aula->rgb_brightness = 3; /* 0-5 scale, mid */
    active_aula->rgb_speed = 3;
    active_aula->nkro_active = 0;
    active_aula->prev_valid = 0;
    active_aula_dev = found;

    kprintf("aula-f87: AULA F87 detected (VID:PID=%04x:%04x, bus=%s, NKRO-capable HID)\n",
            active_aula->vendor_id, active_aula->product_id,
            (bus_type == 2) ? "EHCI" : "xHCI");

    return 0;
}

void aula_f87_cleanup(void) {
    if (active_aula) {
        active_aula->present = 0;
        active_aula = NULL;
    }
    active_aula_dev = NULL;
}

int aula_f87_is_available(void) {
    return (active_aula != NULL && active_aula->present != 0);
}

/* Best-effort vendor HID feature report for RGB. AULA/SINO WEALTH firmwares
   vary; this sends a common 64-byte report layout and records the requested
   state so the OS API remains functional. Returns 0 on accepted state. */
static int aula_f87_send_rgb(struct aula_f87_state *st) {
    if (!st) return -1;
    struct usb_dev *dev = active_aula_dev;
    if (dev) {
        uint8_t rpt[64];
        memset(rpt, 0, sizeof(rpt));
        rpt[0] = 0x04; /* common vendor report id */
        rpt[1] = (uint8_t)((st->rgb_mode < 0) ? 0 : st->rgb_mode);
        rpt[2] = (uint8_t)((st->rgb_brightness < 0) ? 0 : st->rgb_brightness);
        rpt[3] = (uint8_t)((st->rgb_speed < 0) ? 0 : st->rgb_speed);
        /* HID SET_REPORT (feature), best-effort; ignore transport errors. */
        (void)usb_control_transfer(dev, USB_DIR_OUT, 0x21, 0x09,
                                   0x0304, 0, (uint16_t)sizeof(rpt), rpt);
    }
    return 0;
}

int aula_f87_set_rgb_mode(int mode) {
    if (!active_aula) return -1;
    if (mode < AULA_RGB_MODE_STATIC || mode > AULA_RGB_MODE_OFF) return -1;
    active_aula->rgb_mode = mode;
    return aula_f87_send_rgb(active_aula);
}

int aula_f87_get_rgb_mode(void) {
    if (!active_aula) return -1;
    return active_aula->rgb_mode;
}

int aula_f87_set_brightness(int level) {
    if (!active_aula) return -1;
    if (level < 0 || level > 5) return -1;
    active_aula->rgb_brightness = level;
    return aula_f87_send_rgb(active_aula);
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
    static const char *rgb_names[] = { "static", "breathing", "rainbow", "ripple", "neon", "off" };
    const char *rgb = "unknown";
    if (active_aula->rgb_mode >= 0 && active_aula->rgb_mode <= AULA_RGB_MODE_OFF)
        rgb = rgb_names[active_aula->rgb_mode];
    kprintf("AULA F87 Mechanical Keyboard (87-Key TKL):\n");
    kprintf("  Status:      Active\n");
    kprintf("  VID:PID:     %04x:%04x\n", active_aula->vendor_id, active_aula->product_id);
    kprintf("  Rollover:    %s (HID boot report + NKRO edge tracking)\n",
            active_aula->nkro_active ? "NKRO active" : "6KRO/boot");
    kprintf("  Win Lock:    %s\n", active_aula->win_lock ? "ENABLED (Locked)" : "DISABLED");
    kprintf("  RGB Mode:    %s (brightness %d/5)\n", rgb, active_aula->rgb_brightness);
    kprintf("  Keystrokes:  %llu\n", (unsigned long long)active_aula->total_keystrokes);
}

/* Track NKRO-style reports: count how many key slots are nonzero across
   recent polls. This is a heuristic over boot-protocol reports; full NKRO
   bitmap parsing requires the device's report descriptor. */
static void aula_f87_track_nkro(struct aula_f87_state *st, const uint8_t *report, int len) {
    if (!st || !report || len < 3) return;
    int nonzero = 0;
    for (int i = 2; i < len; i++)
        if (report[i]) nonzero++;
    /* If we ever see >6 simultaneous keys, the report stream is NKRO-like. */
    if (nonzero > 6) st->nkro_active = 1;
    if (len > (int)sizeof(st->prev_report)) len = (int)sizeof(st->prev_report);
    memcpy(st->prev_report, report, (size_t)len);
    st->prev_valid = 1;
}

int aula_f87_poll(void) {
    if (!active_aula || !active_aula->present) return 0;

    /* Throttled NKRO heuristic sample (avoid doubling HID control traffic). */
    if (active_aula_dev && (++aula_nkro_sample_div & 0x3F) == 0) {
        uint8_t rpt[16];
        memset(rpt, 0, sizeof(rpt));
        if (usb_control_transfer(active_aula_dev, USB_DIR_IN, 0xA1, 0x01,
                                 0x0100, 0, (uint16_t)sizeof(rpt), rpt) >= 0) {
            aula_f87_track_nkro(active_aula, rpt, (int)sizeof(rpt));
        }
    }

    int key = usb_kbd_poll();
    if (!key) return 0;

    /* If Win Lock is enabled, suppress Windows / Super key presses */
    if (active_aula->win_lock && (key == KEY_SUPER)) {
        return 0;
    }

    active_aula->total_keystrokes++;
    return key;
}
