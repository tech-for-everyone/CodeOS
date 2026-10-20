/* device_test.c — host test for the kernel's device table and selection.
 *
 * The device support is a table of phones and tablets plus the rules for
 * picking one: an explicit name on the command line wins, then a panel that
 * matches a profile, then a generic profile from the geometry.  None of that
 * needs a device to test, so this compiles the *real*
 * kernel/kernel/device.c (with only the console stubbed out) and drives it.
 *
 * Build and run:
 *   cc -std=c11 -Wall -Wextra -I kernel/tests/host -I kernel/kernel \
 *      -o /tmp/device-test kernel/tests/device_test.c kernel/kernel/device.c
 *   /tmp/device-test
 */
#include <stdio.h>
#include <string.h>

#include "device.h"

/* The two kernel functions device.c uses that the host does not have.
 * Both are trivial, and stubbing them is the only thing that stands
 * between the real device.c and a host test. */
void kprintf(const char *fmt, ...) { (void)fmt; }

int strncpy_safe(char *dst, const char *src, size_t dstsize) {
    if (!dst || !src || dstsize == 0) return -1;
    size_t i;
    for (i = 0; i < dstsize - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    return src[i] ? -1 : 0;
}

static int failures;

#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("ok  : %s\n", what);                                        \
        } else {                                                               \
            printf("FAIL: %s\n", what);                                        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int named(const char *name) {
    for (int i = 0; i < zircon_device_count(); i++) {
        const zircon_device_t *d = zircon_device_at(i);
        if (d && !strcmp(d->name, name)) return i;
    }
    return -1;
}

int main(void) {
    /* The table itself: a few phones and a few tablets, each with a panel
     * and a logical screen, and the scale that relates them. */
    CHECK(zircon_device_count() >= 6, "the table has several devices");
    CHECK(named("phone-412x915") >= 0, "a phone is in the table");
    CHECK(named("tablet-1024x768") >= 0, "a tablet is in the table");

    int ip = named("phone-1080x2400");
    CHECK(ip >= 0, "a 1080x2400 phone is in the table");
    if (ip >= 0) {
        const zircon_device_t *d = zircon_device_at(ip);
        CHECK(d->panel_w == 1080 && d->panel_h == 2400, "the phone's panel is 1080x2400");
        CHECK(d->ui_w == 360 && d->ui_h == 800, "the phone's logical screen is 360x800");
        CHECK(d->scale == 3, "the phone's scale is 3");
        CHECK(d->form == ZIRCON_DEVICE_PHONE, "the phone is a phone");
        CHECK((d->caps & ZIRCON_DEVICE_TOUCH) != 0, "the phone has touch");
        CHECK((d->caps & ZIRCON_DEVICE_POINTER) == 0, "the phone has no pointer");
    }

    int it = named("tablet-1024x768");
    if (it >= 0) {
        const zircon_device_t *d = zircon_device_at(it);
        CHECK(d->form == ZIRCON_DEVICE_TABLET, "the tablet is a tablet");
        CHECK((d->caps & ZIRCON_DEVICE_POINTER) != 0, "the tablet has a pointer");
        CHECK((d->caps & ZIRCON_DEVICE_KEYBOARD) != 0, "the tablet has a keyboard");
    }

    /* An explicit name on the command line wins over everything else. */
    zircon_device_init("loglevel=4 zircon.device=tablet-1024x768 zircon.init=/sbin/zircon_init",
                       1280, 800);
    const zircon_device_t *d = zircon_device();
    CHECK(!strcmp(d->name, "tablet-1024x768"), "the named device is selected");
    CHECK(d->form == ZIRCON_DEVICE_TABLET, "the named device is a tablet");

    /* A name that is not in the table falls back to the geometry, and says
     * so rather than pretending. */
    zircon_device_init("zircon.device=phone-from-the-future", 1280, 800);
    d = zircon_device();
    CHECK(strcmp(d->name, "phone-from-the-future") != 0, "an unknown name is not used");
    CHECK(d->form == ZIRCON_DEVICE_TABLET, "a wide panel is a tablet");
    CHECK(d->caps & ZIRCON_DEVICE_TOUCH, "a generic device has touch");

    /* A panel that matches a profile selects it. */
    zircon_device_init("", 1080, 2400);
    d = zircon_device();
    CHECK(!strcmp(d->name, "phone-1080x2400"), "a matching panel selects the profile");

    /* The generic fallback: a narrow portrait panel is a phone, anything
     * else is a tablet. */
    zircon_device_init("", 480, 900);
    d = zircon_device();
    CHECK(d->form == ZIRCON_DEVICE_PHONE, "a narrow portrait panel is a phone");
    CHECK(d->scale == 1, "a generic device is 1:1");
    CHECK(d->ui_w == 480 && d->ui_h == 900, "a generic device's screen is its panel");

    zircon_device_init("", 1366, 768);
    d = zircon_device();
    CHECK(d->form == ZIRCON_DEVICE_TABLET, "a tablet-sized panel is a tablet");

    zircon_device_init("", 1920, 1080);
    d = zircon_device();
    CHECK(d->form == ZIRCON_DEVICE_DESKTOP, "a big panel is a desktop");

    /* The scale has to fit the panel.  A 3x phone interface needs a
     * 1080x2400 panel; on a 1280x800 one the scale is clamped and the
     * logical screen is cropped. */
    zircon_device_init("zircon.device=phone-1080x2400", 1280, 800);
    d = zircon_device();
    CHECK(d->scale * d->ui_w <= 1280, "the logical width fits the panel");
    CHECK(d->scale * d->ui_h <= 800, "the logical height fits the panel");
    CHECK(d->ui_h == 800, "the logical screen is cropped to the panel");

    /* A panel that fits keeps the profile's scale. */
    zircon_device_init("zircon.device=phone-1080x2400", 1080, 2400);
    d = zircon_device();
    CHECK(d->scale == 3, "the 3x scale survives on a 1080x2400 panel");
    CHECK(d->ui_w == 360 && d->ui_h == 800, "the logical screen is 360x800");

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all device checks passed\n");
    return 0;
}
