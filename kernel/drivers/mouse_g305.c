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

/* Cached USB device pointer for HID++ control transfers. Not owned. */
static struct usb_dev *active_g305_dev = NULL;

/* ───── HID++ 2.0 framing/parsing (pure, testable) ───── */

int hidpp_short_build(uint8_t *buf, size_t len, uint8_t dev_index,
                      uint16_t feature_idx, uint8_t p1, uint8_t p2, uint8_t p3) {
    if (!buf || len < 7) return -1;
    buf[0] = HIDPP_SHORT_REPORT_ID;
    buf[1] = dev_index;
    /* For HID++ 2.0 short reports the feature index is the software-resolved
       index (low byte) and function|sw in the next byte; callers pass the
       feature in the low byte for simplicity in this driver. */
    buf[2] = (uint8_t)(feature_idx & 0xFF);
    buf[3] = 0x00; /* function 0, sw id 0 */
    buf[4] = p1;
    buf[5] = p2;
    buf[6] = p3;
    return 7;
}

int hidpp_long_build(uint8_t *buf, size_t len, uint8_t dev_index,
                     uint16_t feature_idx, uint8_t reg_addr, const uint8_t *params, int param_count) {
    if (!buf || len < 20) return -1;
    if (param_count < 0) param_count = 0;
    if (param_count > 15) param_count = 15;
    buf[0] = HIDPP_LONG_REPORT_ID;
    buf[1] = dev_index;
    buf[2] = (uint8_t)(feature_idx & 0xFF);
    buf[3] = 0x00;
    buf[4] = reg_addr;
    for (int i = 0; i < 15; i++)
        buf[5 + i] = (i < param_count && params) ? params[i] : 0;
    return 20;
}

int hidpp_parse_battery(const uint8_t *resp, int len, int *percent, int *charging) {
    if (!resp || !percent || !charging) return -1;
    if (len < 7) return -1;
    if (resp[0] != HIDPP_SHORT_REPORT_ID && resp[0] != HIDPP_LONG_REPORT_ID)
        return -1;
    /* Unified battery (0x1000/getStatus): param0=level 0-100, param1=status.
       Status bit patterns vary by device; treat any charging/recharging hint
       (0x01, 0x21, 0x81 common) as charging. */
    *percent = (int)resp[4];
    if (*percent > 100) *percent = 100;
    if (*percent < 0) *percent = 0;
    *charging = (resp[5] & 0x81) ? 1 : 0;
    return 0;
}

int hidpp_parse_dpi(const uint8_t *resp, int len, int *dpi) {
    if (!resp || !dpi) return -1;
    if (len < 7) return -1;
    if (resp[0] != HIDPP_SHORT_REPORT_ID && resp[0] != HIDPP_LONG_REPORT_ID)
        return -1;
    *dpi = ((int)resp[4] << 8) | (int)resp[5];
    if (*dpi <= 0) return -1;
    return 0;
}

int hidpp_parse_polling_rate(const uint8_t *resp, int len, int *mode) {
    if (!resp || !mode) return -1;
    if (len < 6) return -1;
    if (resp[0] != HIDPP_SHORT_REPORT_ID && resp[0] != HIDPP_LONG_REPORT_ID)
        return -1;
    /* Device-specific encodings vary; accept explicit mode values or a
       report-rate byte where 1/8ms maps to performance/endurance. */
    if (resp[4] == G305_MODE_PERFORMANCE || resp[4] == 1 || resp[4] == 0x01)
        *mode = G305_MODE_PERFORMANCE;
    else if (resp[4] == G305_MODE_ENDURANCE || resp[4] == 8 || resp[4] == 0x08)
        *mode = G305_MODE_ENDURANCE;
    else
        *mode = G305_MODE_UNKNOWN;
    return (*mode == G305_MODE_UNKNOWN) ? -1 : 0;
}

/* ───── Device lookup ───── */

static struct usb_dev *g305_find_dev(void) {
    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        struct usb_dev *d = ehci_get_dev(i);
        if (d && g305_device_matches((uint16_t)d->vendor_id, (uint16_t)d->product_id))
            return d;
    }
    int xd = xhci_get_num_devs();
    for (int i = 0; i < xd; i++) {
        struct usb_dev *d = xhci_get_dev(i);
        if (d && g305_device_matches((uint16_t)d->vendor_id, (uint16_t)d->product_id))
            return d;
    }
    return NULL;
}

int g305_probe(void) {
    return g305_find_dev() != NULL;
}

int g305_init(void *ram_instance) {
    if (!ram_instance) return -1;
    active_g305 = NULL;
    active_g305_dev = NULL;

    struct usb_dev *found = g305_find_dev();
    if (!found) return -1;

    int bus_type = 0;
    int nd = ehci_get_num_devs();
    for (int i = 0; i < nd; i++) {
        if (ehci_get_dev(i) == found) { bus_type = 2; break; }
    }
    if (!bus_type) bus_type = 3;

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
    active_g305->battery_charging = 0;
    active_g305->performance_mode = G305_MODE_UNKNOWN;
    active_g305->last_report_ms = timer_get_milliseconds();
    active_g305_dev = found;

    /* Best-effort HID++ feature refresh. Failures are non-fatal: the device
       still works as a standard HID mouse. */
    g305_refresh_battery();
    g305_refresh_device_info();

    kprintf("g305: Logitech G305 receiver detected (VID:PID=%04x:%04x, bus=%s, sens=%d)\n",
            active_g305->vendor_id, active_g305->product_id,
            (bus_type == 2) ? "EHCI" : "xHCI", active_g305->current_dpi);

    return 0;
}

void g305_cleanup(void) {
    if (active_g305) {
        active_g305->present = 0;
        active_g305 = NULL;
    }
    active_g305_dev = NULL;
}

int g305_is_available(void) {
    return (active_g305 != NULL && active_g305->present != 0);
}

/* ───── HID++ control (best-effort) ───── */

static int g305_hidpp_get(struct usb_dev *dev, uint8_t feature_idx,
                          uint8_t *resp, int resp_len) {
    uint8_t req[7];
    if (!dev || !resp || resp_len < 7) return -1;
    if (hidpp_short_build(req, sizeof(req), HIDPP_DEVICE_DEFAULT,
                          feature_idx, 0x00, 0x00, 0x00) < 0)
        return -1;
    memset(resp, 0, (size_t)resp_len);
    /* HID class GET_REPORT (input), report type+id in wValue. */
    return usb_control_transfer(dev, USB_DIR_IN, 0xA1, 0x01,
                                0x0100, 0, (uint16_t)resp_len, resp);
}

int g305_refresh_battery(void) {
    if (!active_g305) return -1;
    struct usb_dev *dev = active_g305_dev ? active_g305_dev : g305_find_dev();
    if (!dev) return -1;
    uint8_t resp[20];
    if (g305_hidpp_get(dev, (uint8_t)(HIDPP_FEATURE_BATTERY_STATUS & 0xFF),
                       resp, (int)sizeof(resp)) < 0)
        return -1;
    int pct = -1, chg = 0;
    if (hidpp_parse_battery(resp, (int)sizeof(resp), &pct, &chg) == 0) {
        active_g305->battery_percent = pct;
        active_g305->battery_charging = chg;
        return 0;
    }
    return -1;
}

int g305_refresh_device_info(void) {
    if (!active_g305) return -1;
    struct usb_dev *dev = active_g305_dev ? active_g305_dev : g305_find_dev();
    if (!dev) return -1;
    /* DPI/polling queries are device/feature-index dependent; attempt a
       best-effort read and keep software defaults on failure. */
    uint8_t resp[20];
    if (g305_hidpp_get(dev, (uint8_t)(HIDPP_FEATURE_POLLING_RATE & 0xFF),
                       resp, (int)sizeof(resp)) >= 0) {
        int mode = G305_MODE_UNKNOWN;
        if (hidpp_parse_polling_rate(resp, (int)sizeof(resp), &mode) == 0)
            active_g305->performance_mode = mode;
    }
    return 0;
}

int g305_set_dpi(int dpi) {
    if (!active_g305) return -1;
    if (dpi < 200) dpi = 200;
    if (dpi > 12000) dpi = 12000;
    active_g305->current_dpi = dpi;
    for (int i = 0; i < NUM_DPI_PRESETS; i++) {
        if (dpi_presets[i] == dpi) { active_g305->dpi_preset_idx = i; break; }
    }
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
    if (active_g305->battery_percent < 0)
        g305_refresh_battery();
    return active_g305->battery_percent;
}

int g305_set_performance_mode(int mode) {
    if (!active_g305) return -1;
    if (mode != G305_MODE_PERFORMANCE && mode != G305_MODE_ENDURANCE) return -1;
    struct usb_dev *dev = active_g305_dev ? active_g305_dev : g305_find_dev();
    if (!dev) return -1;
    uint8_t req[20];
    uint8_t p = (uint8_t)mode;
    if (hidpp_long_build(req, sizeof(req), HIDPP_DEVICE_DEFAULT,
                         (uint8_t)(HIDPP_FEATURE_POLLING_RATE & 0xFF),
                         0x00, &p, 1) < 0)
        return -1;
    if (usb_control_transfer(dev, USB_DIR_OUT, 0x21, 0x09,
                             0x0211, 0, (uint16_t)sizeof(req), req) >= 0) {
        active_g305->performance_mode = mode;
        return 0;
    }
    /* Fallback: keep requested mode as software preference for reporting. */
    active_g305->performance_mode = mode;
    return 0;
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
    (void)g305_get_battery_level();
    kprintf("Logitech G305 Lightspeed Wireless Gaming Mouse:\n");
    kprintf("  Status:      Active\n");
    kprintf("  VID:PID:     %04x:%04x\n", active_g305->vendor_id, active_g305->product_id);
    kprintf("  Sensitivity: %d DPI-equivalent (software scaling)\n", active_g305->current_dpi);
    kprintf("  Polling:     %s\n",
            active_g305->performance_mode == G305_MODE_PERFORMANCE ? "performance (1000 Hz)" :
            active_g305->performance_mode == G305_MODE_ENDURANCE ? "endurance (125 Hz)" :
            "unknown");
    if (active_g305->battery_percent < 0)
        kprintf("  Battery:     unknown\n");
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
