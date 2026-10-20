/* device.h — the phones and tablets the Zircon kernel knows how to drive.
 *
 * A device profile says three things: how big the panel is, how big the
 * *logical* UI is (the size the interface is actually laid out for), and
 * what the device can do (touch, pointer, keyboard).  The ratio between the
 * two is the scale: a 1080x2400 phone has a 360x800 logical screen, so
 * everything the interface draws is multiplied by three on the way to the
 * panel.  That is the whole reason a profile is not just a resolution.
 *
 * The kernel picks the profile at boot (see zircon_device_init) and hands it
 * to userspace through SYSCALL_GET_INFO, so the compositor can lay out for
 * the device it is running on instead of assuming a phone.
 */
#ifndef ZIRCON_DEVICE_H
#define ZIRCON_DEVICE_H

#include <stdint.h>

/* Form factors. */
#define ZIRCON_DEVICE_PHONE  0
#define ZIRCON_DEVICE_TABLET 1
#define ZIRCON_DEVICE_DESKTOP 2

/* Input capabilities. */
#define ZIRCON_DEVICE_TOUCH    0x01
#define ZIRCON_DEVICE_POINTER  0x02
#define ZIRCON_DEVICE_KEYBOARD 0x04

/* How many devices the table holds. */
#define ZIRCON_DEVICE_MAX 16

typedef struct {
    char     name[24];   /* "phone-412x915" */
    uint16_t panel_w;    /* the panel, in pixels */
    uint16_t panel_h;
    uint16_t ui_w;       /* the logical screen the interface is laid out for */
    uint16_t ui_h;
    uint8_t  form;       /* ZIRCON_DEVICE_PHONE / _TABLET */
    uint8_t  caps;       /* ZIRCON_DEVICE_TOUCH | _POINTER | _KEYBOARD */
    uint8_t  scale;      /* panel pixels per logical pixel */
} zircon_device_t;

/* Pick the device.
 *
 * `cmdline` is the kernel command line and may carry "zircon.device=<name>";
 * an explicit name always wins, so a device can be emulated ("zircon.device=
 * tablet-1024x768") and the layouts can be tried without the hardware.  With
 * no name, a panel that matches a known profile selects that profile, and
 * anything else gets a generic profile built from the geometry.
 */
void zircon_device_init(const char *cmdline, uint32_t panel_w, uint32_t panel_h);

/* The selected device.  Never NULL: with no match it is a generic profile
 * derived from the panel geometry. */
const zircon_device_t *zircon_device(void);

/* "phone" / "tablet", for the boot log. */
const char *zircon_device_form_name(uint8_t form);

/* The profile table, for the host test and for the boot log. */
int zircon_device_count(void);
const zircon_device_t *zircon_device_at(int idx);

#endif /* ZIRCON_DEVICE_H */
