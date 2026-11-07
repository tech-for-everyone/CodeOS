#include "mouse_g305.h"
#include "usb.h"
#include "usb_ehci.h"
#include "usb_uhci.h"
#include "usb_xhci.h"
#include "timer.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

static const int dpi_presets[] = { 400, 800, 1600, 3200 };
#define NUM_DPI_PRESETS ((int)(sizeof(dpi_presets) / sizeof(dpi_presets[0])))

static struct g305_state *active_g305 = NULL;

int g305_probe(void) {
    /* Only the known G305 receiver is accepted; other Logitech receivers
     * must not be reported as a G305. */
    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (d && g305_device_matches((uint16_t)d->vendor_id,
                                     (uint16_t)d->product_id))
            return 1;
    }

    int xd = xhci_get_num_devs();
    for (int i = 0; i < xd; i++) {
        struct usb_dev *d = xhci_get_dev(i);
        if (d && g305_device_matches((uint16_t)d->vendor_id,
                                     (uint16_t)d->product_id))
            return 1;
    }

    return 0;
}

int g305_init(void *ram_instance) {
    if (!ram_instance) return -1;
    active_g305 = NULL;
    struct usb_dev *found = NULL;
    int bus_type = 0;
    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (d && g305_device_matches((uint16_t)d->vendor_id,
                                     (uint16_t)d->product_id)) {
            found = d;
            bus_type = 2;
            break;
        }
    }

    if (!found) {
        int xd = xhci_get_num_devs();
        for (int i = 0; i < xd; i++) {
            struct usb_dev *d = xhci_get_dev(i);
            if (d && g305_device_matches((uint16_t)d->vendor_id,
                                         (uint16_t)d->product_id)) {
                found = d;
                bus_type = 3;
                break;
            }
        }
    }

    if (!found) return -1;

    active_g305 = (struct g305_state *)ram_instance;
    memset(active_g305, 0, sizeof(*active_g305));
    active_g305->present = 1;
    active_g305->bus_type = bus_type;
    active_g305->dev_address = found->address;
    active_g305->vendor_id = (uint16_t)found->vendor_id;
    active_g305->product_id = (uint16_t)found->product_id;
    active_g305->dpi_preset_idx = 2;
    active_g305->current_dpi = dpi_presets[active_g305->dpi_preset_idx];
    active_g305->battery_percent = -1;
    active_g305->performance_mode = G305_MODE_UNKNOWN;
    active_g305->last_report_ms = timer_get_milliseconds();

    kprintf("g305: Logitech G305 receiver detected (PID=0x%04x, software sensitivity=%d DPI-equivalent)\n",
            active_g305->product_id, active_g305->current_dpi);

    return 0;
}

void g305_cleanup(void) {
    if (active_g305) {
        active_g305->present = 0;
        active_g305 = NULL;
    }
}

int g305_is_available(void) {
    return (active_g305 != NULL && active_g305->present != 0);
}

int g305_set_dpi(int dpi) {
    if (!active_g305) return -1;
    if (dpi < 200) dpi = 200;
    if (dpi > 12000) dpi = 12000;
    active_g305->current_dpi = dpi;
    kprintf("g305: software pointer sensitivity set to %d DPI-equivalent\n", dpi);
    return 0;
}

int g305_get_dpi(void) {
    if (!active_g305) return 0;
    return active_g305->current_dpi;
}

int g305_cycle_dpi(void) {
    if (!active_g305) return -1;
    active_g305->dpi_preset_idx = (active_g305->dpi_preset_idx + 1) % NUM_DPI_PRESETS;
    active_g305->current_dpi = dpi_presets[active_g305->dpi_preset_idx];
    kprintf("g305: DPI preset cycled -> %d DPI\n", active_g305->current_dpi);
    return active_g305->current_dpi;
}

int g305_get_battery_level(void) {
    if (!active_g305) return -1;
    return active_g305->battery_percent;
}

int g305_set_performance_mode(int mode) {
    if (!active_g305) return -1;
    if (mode != G305_MODE_PERFORMANCE && mode != G305_MODE_ENDURANCE) return -1;
    kprintf("g305: polling-rate control requires HID++ support; no hardware change made\n");
    return -1;
}

int g305_get_performance_mode(void) {
    if (!active_g305) return G305_MODE_UNKNOWN;
    return active_g305->performance_mode;
}

struct g305_state *g305_get_state(void) {
    return active_g305;
}

void g305_print_info(void) {
    if (!active_g305) {
        kprintf("g305: Logitech G305 not loaded\n");
        return;
    }
    kprintf("Logitech G305 Lightspeed Wireless Gaming Mouse:\n");
    kprintf("  Status:      Active\n");
    kprintf("  VID:PID:     %04x:%04x\n", active_g305->vendor_id, active_g305->product_id);
    kprintf("  Sensitivity: %d DPI-equivalent (software scaling)\n", active_g305->current_dpi);
    kprintf("  Polling:     %s\n", active_g305->performance_mode == G305_MODE_UNKNOWN
            ? "unknown (HID++ control unavailable)"
            : (active_g305->performance_mode == G305_MODE_PERFORMANCE
               ? "performance" : "endurance"));
    if (active_g305->battery_percent < 0)
        kprintf("  Battery:     unknown (battery feature unavailable)\n");
    else
        kprintf("  Battery:     %d%% (%s)\n", active_g305->battery_percent,
                active_g305->battery_charging ? "Charging" : "Discharging");
    kprintf("  Reports:     %d\n", active_g305->total_reports);
}

int g305_poll(int *dx, int *dy, int *buttons, int *wheel) {
    if (!active_g305 || !active_g305->present) return 0;
    if (!dx || !dy || !buttons || !wheel) return -1;

    int raw_dx = 0, raw_dy = 0, raw_btn = 0, raw_whl = 0;
    int got = usb_mouse_poll(&raw_dx, &raw_dy, &raw_btn, &raw_whl);
    if (!got) return 0;

    /* Check for DPI cycle button press edge */
    if ((raw_btn & G305_BTN_DPI_SWITCH) && !(active_g305->last_buttons & G305_BTN_DPI_SWITCH)) {
        g305_cycle_dpi();
    }

    /* Scale movement by DPI ratio (normalized around 800 DPI) */
    int scale = active_g305->current_dpi;
    if (scale <= 0) scale = 800;
    *dx = (raw_dx * scale) / 800;
    *dy = (raw_dy * scale) / 800;
    *buttons = raw_btn & 0x1F; /* standard 5 buttons */
    *wheel = raw_whl;

    active_g305->last_buttons = raw_btn;
    active_g305->total_reports++;
    active_g305->last_report_ms = timer_get_milliseconds();

    return 1;
}
