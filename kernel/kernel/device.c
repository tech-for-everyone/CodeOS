/* device.c — the phones and tablets the Zircon kernel knows how to drive.
 *
 * The table below is the device support: each entry is a real device class
 * with the panel it has, the logical screen its interface is laid out for,
 * and the input it offers.  The compositor asks the kernel which one it is
 * running as (SYSCALL_GET_INFO, command 4) and lays out accordingly.
 *
 * Selection, in order:
 *
 *   1. "zircon.device=<name>" on the kernel command line.  An explicit name
 *      always wins, so a device can be *emulated*: booting with
 *      "zircon.device=tablet-1024x768" gives the tablet layout on any
 *      machine, which is how the layouts get tested without the hardware.
 *   2. A profile whose panel matches the display exactly.
 *   3. A generic profile built from the geometry: a narrow portrait panel is
 *      a phone, anything wider is a tablet.
 */
#include "device.h"
#include "kprintf.h"
#include "string.h"

/* The supported devices.
 *
 * The logical size is what the interface is drawn for; the panel is what the
 * device actually has.  They differ on the 1080x2400 phone (a 360x800
 * logical screen at 3x) and on the 800x1280 tablet (400x640 logical at 2x).
 * A profile whose scale is 1 is used as-is.
 */
static const zircon_device_t kDevices[] = {
    { "phone-360x800",    360,  800,  360,  800, ZIRCON_DEVICE_PHONE,  ZIRCON_DEVICE_TOUCH, 1 },
    { "phone-412x915",    412,  915,  412,  915, ZIRCON_DEVICE_PHONE,  ZIRCON_DEVICE_TOUCH, 1 },
    { "phone-1080x2340", 1080, 2340,  360,  780, ZIRCON_DEVICE_PHONE,  ZIRCON_DEVICE_TOUCH, 3 },
    { "phone-1080x2400", 1080, 2400,  360,  800, ZIRCON_DEVICE_PHONE,  ZIRCON_DEVICE_TOUCH, 3 },
    { "tablet-800x1280",  800, 1280,  800, 1280, ZIRCON_DEVICE_TABLET, ZIRCON_DEVICE_TOUCH | ZIRCON_DEVICE_POINTER | ZIRCON_DEVICE_KEYBOARD, 1 },
    { "tablet-1024x768", 1024,  768, 1024,  768, ZIRCON_DEVICE_TABLET, ZIRCON_DEVICE_TOUCH | ZIRCON_DEVICE_POINTER | ZIRCON_DEVICE_KEYBOARD, 1 },
    { "tablet-1200x1920",1200, 1920, 1200, 1920, ZIRCON_DEVICE_TABLET, ZIRCON_DEVICE_TOUCH | ZIRCON_DEVICE_POINTER | ZIRCON_DEVICE_KEYBOARD, 1 },
    { "tablet-1366x768", 1366,  768, 1366,  768, ZIRCON_DEVICE_TABLET, ZIRCON_DEVICE_TOUCH | ZIRCON_DEVICE_POINTER | ZIRCON_DEVICE_KEYBOARD, 1 },
    { "tablet-1600x2560",1600, 2560,  800, 1280, ZIRCON_DEVICE_TABLET, ZIRCON_DEVICE_TOUCH | ZIRCON_DEVICE_POINTER | ZIRCON_DEVICE_KEYBOARD, 2 },
};

#define DEVICE_COUNT ((int)(sizeof(kDevices) / sizeof(kDevices[0])))

static zircon_device_t g_device;
static int g_device_ready;

int zircon_device_count(void) {
    return DEVICE_COUNT;
}

const zircon_device_t *zircon_device_at(int idx) {
    if (idx < 0 || idx >= DEVICE_COUNT) return 0;
    return &kDevices[idx];
}

const char *zircon_device_form_name(uint8_t form) {
    if (form == ZIRCON_DEVICE_TABLET) return "tablet";
    if (form == ZIRCON_DEVICE_DESKTOP) return "desktop";
    return "phone";
}

/* Build a profile for a panel that matches nothing in the table.
 *
 * The form factor is decided by the geometry, and it is a guess: a narrow
 * portrait panel is a phone, a screen of tablet size is a tablet, and
 * anything bigger is a desktop.  A device that is not in the table but has a
 * known name can still ask for what it is.
 */
static void device_generic(uint32_t panel_w, uint32_t panel_h) {
    memset(&g_device, 0, sizeof(g_device));
    g_device.panel_w = (uint16_t)(panel_w > 0xFFFF ? 0xFFFF : panel_w);
    g_device.panel_h = (uint16_t)(panel_h > 0xFFFF ? 0xFFFF : panel_h);
    g_device.ui_w = g_device.panel_w;
    g_device.ui_h = g_device.panel_h;
    g_device.scale = 1;
    g_device.caps = ZIRCON_DEVICE_TOUCH;
    if (panel_h > panel_w && panel_w < 600) {
        g_device.form = ZIRCON_DEVICE_PHONE;
        strncpy_safe(g_device.name, "phone-generic", sizeof(g_device.name));
    } else if (panel_w <= 1600 && panel_h <= 1200) {
        g_device.form = ZIRCON_DEVICE_TABLET;
        strncpy_safe(g_device.name, "tablet-generic", sizeof(g_device.name));
    } else {
        g_device.form = ZIRCON_DEVICE_DESKTOP;
        strncpy_safe(g_device.name, "desktop-generic", sizeof(g_device.name));
    }
}

/* Pull "zircon.device=<name>" out of the kernel command line. */
static const char *device_from_cmdline(const char *cmdline) {
    if (!cmdline) return 0;
    const char *key = "zircon.device=";
    const char *p = cmdline;
    while ((p = strstr(p, key)) != 0) {
        /* The name has to start the command line or follow a space, or
         * "notzircon.device=" would match. */
        if (p == cmdline || p[-1] == ' ' || p[-1] == '\t') {
            p += strlen(key);
            const char *end = p;
            while (*end && *end != ' ' && *end != '\t') end++;
            static char name[24];
            size_t n = (size_t)(end - p);
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, p, n);
            name[n] = 0;
            if (name[0]) return name;
        }
        p += strlen(key);
    }
    return 0;
}

/* The scale has to fit the panel.
 *
 * A 3x phone interface (360x800 logical) needs a 1080x2400 panel, and a
 * 1280x800 panel cannot show it.  The scale is clamped down until the
 * logical screen fits, and if even 1:1 does not fit, the logical screen is
 * cropped to the panel -- which is the honest result of asking for a device
 * bigger than the display.
 */
static void device_clamp_to_panel(uint32_t panel_w, uint32_t panel_h) {
    if (!panel_w || !panel_h) return;
    while (g_device.scale > 1 &&
           (panel_w / g_device.scale < g_device.ui_w || panel_h / g_device.scale < g_device.ui_h))
        g_device.scale--;
    uint32_t max_w = panel_w / g_device.scale;
    uint32_t max_h = panel_h / g_device.scale;
    if (max_w && g_device.ui_w > max_w) g_device.ui_w = (uint16_t)max_w;
    if (max_h && g_device.ui_h > max_h) g_device.ui_h = (uint16_t)max_h;
}

static void device_report(void) {
    kprintf("device: %s (%s, %ux%u panel, %ux%u logical, %ux scale)\n",
            g_device.name, zircon_device_form_name(g_device.form),
            g_device.panel_w, g_device.panel_h,
            g_device.ui_w, g_device.ui_h, g_device.scale);
}

void zircon_device_init(const char *cmdline, uint32_t panel_w, uint32_t panel_h) {
    g_device_ready = 1;

    const char *want = device_from_cmdline(cmdline);
    if (want) {
        for (int i = 0; i < DEVICE_COUNT; i++) {
            if (!strcmp(kDevices[i].name, want)) {
                g_device = kDevices[i];
                device_clamp_to_panel(panel_w, panel_h);
                device_report();
                return;
            }
        }
        kprintf("device: %s is not known; using the panel geometry\n", want);
    }

    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (kDevices[i].panel_w == panel_w && kDevices[i].panel_h == panel_h) {
            g_device = kDevices[i];
            device_clamp_to_panel(panel_w, panel_h);
            device_report();
            return;
        }
    }

    device_generic(panel_w, panel_h);
    device_clamp_to_panel(panel_w, panel_h);
    device_report();
}

const zircon_device_t *zircon_device(void) {
    if (!g_device_ready) {
        /* No init yet: a 360x800 phone is a safer default than nothing. */
        device_generic(360, 800);
        g_device_ready = 1;
    }
    return &g_device;
}
