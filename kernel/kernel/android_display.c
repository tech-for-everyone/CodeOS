#include "android_display.h"
#include "android_container.h"
#include "string.h"
#include "kernel/kprintf.h"
#include "arch/x86_64/fb.h"

static android_display_t displays[ANDROID_MAX_CONTAINERS];
static int display_count = 0;

/* ─── Init ─── */

int android_display_init(void) {
    memset(displays, 0, sizeof(displays));
    display_count = 0;
    kprintf("[android-display] initialized\n");
    return 0;
}

/* ─── Display bridge lifecycle ─── */

int android_display_create(int container_id) {
    if (display_count >= ANDROID_MAX_CONTAINERS) return -1;

    android_display_t *d = &displays[display_count];
    memset(d, 0, sizeof(*d));

    d->active = 0;
    d->container_id = container_id;
    d->guest_width = 1080;
    d->guest_height = 1920;
    d->guest_bpp = 32;
    d->guest_stride = 1080 * 4;
    d->host_width = fb_getwidth();
    d->host_height = fb_getheight();
    d->dirty = 1;

    /* Calculate scaling */
    d->scale_x = (double)d->host_width / (double)d->guest_width;
    d->scale_y = (double)d->host_height / (double)d->guest_height;
    d->scaled_width = d->host_width;
    d->scaled_height = d->host_height;

    /* Create display bridge */
    char name[32];
    snprintf(name, sizeof(name), "android-%d", container_id);
    d->bridge_id = display_bridge_create(name, 0);

    display_count++;
    kprintf("[android-display] created for container %d (%ux%u -> %ux%u)\n",
            container_id, d->guest_width, d->guest_height,
            d->host_width, d->host_height);
    return 0;
}

int android_display_destroy(int container_id) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id) {
            display_bridge_destroy(displays[i].bridge_id);
            memmove(&displays[i], &displays[i + 1],
                    (display_count - i - 1) * sizeof(android_display_t));
            display_count--;
            return 0;
        }
    }
    return -1;
}

int android_display_activate(int container_id) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id) {
            displays[i].active = 1;
            displays[i].dirty = 1;
            display_bridge_activate(displays[i].bridge_id);
            return 0;
        }
    }
    return -1;
}

int android_display_deactivate(int container_id) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id) {
            displays[i].active = 0;
            display_bridge_deactivate(displays[i].bridge_id);
            return 0;
        }
    }
    return -1;
}

/* ─── Frame forwarding ─── */

int android_display_blit_frame(int container_id) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id && displays[i].active) {
            android_display_t *d = &displays[i];

            if (!d->dirty) return 0;

            /* Blit via display bridge */
            display_bridge_blit_full(d->bridge_id);

            d->dirty = 0;
            return 0;
        }
    }
    return -1;
}

int android_display_blit_region(int container_id,
                                 uint32_t x, uint32_t y,
                                 uint32_t w, uint32_t h) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id && displays[i].active) {
            display_bridge_blit_region(displays[i].bridge_id, x, y, w, h);
            return 0;
        }
    }
    return -1;
}

/* ─── Input forwarding ─── */

int android_display_send_key(int container_id, uint32_t key, int down) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id && displays[i].active) {
            display_bridge_send_key(displays[i].bridge_id, key, down);
            return 0;
        }
    }
    return -1;
}

int android_display_send_mouse(int container_id, int32_t x, int32_t y,
                               uint32_t buttons) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id && displays[i].active) {
            /* Scale coordinates from host to guest */
            android_display_t *d = &displays[i];
            int32_t gx = (int32_t)((double)x / d->scale_x);
            int32_t gy = (int32_t)((double)y / d->scale_y);
            display_bridge_send_mouse(displays[i].bridge_id, gx, gy, buttons);
            return 0;
        }
    }
    return -1;
}

int android_display_send_wheel(int container_id, int32_t dx, int32_t dy) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id && displays[i].active) {
            display_bridge_send_wheel(displays[i].bridge_id, dx, dy);
            return 0;
        }
    }
    return -1;
}

/* ─── Display info ─── */

int android_display_set_resolution(int container_id,
                                    uint32_t width, uint32_t height) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id) {
            android_display_t *d = &displays[i];
            d->guest_width = width;
            d->guest_height = height;
            d->guest_stride = width * 4;

            /* Recalculate scaling */
            d->scale_x = (double)d->host_width / (double)width;
            d->scale_y = (double)d->host_height / (double)height;
            d->dirty = 1;

            kprintf("[android-display] resolution changed: %ux%u\n", width, height);
            return 0;
        }
    }
    return -1;
}

int android_display_get_info(int container_id,
                              uint32_t *width, uint32_t *height, uint32_t *bpp) {
    for (int i = 0; i < ANDROID_MAX_CONTAINERS; i++) {
        if (displays[i].container_id == container_id) {
            if (width) *width = displays[i].guest_width;
            if (height) *height = displays[i].guest_height;
            if (bpp) *bpp = displays[i].guest_bpp;
            return 0;
        }
    }
    return -1;
}
