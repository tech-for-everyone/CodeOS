#ifndef ANDROID_DISPLAY_H
#define ANDROID_DISPLAY_H

#include "types.h"
#include "display_bridge.h"

/* ─── Android display bridge ─── */
/* Sommelier-style display forwarding: Android framebuffer -> CodeOS framebuffer */

typedef struct {
    int  active;
    int  container_id;

    /* Guest Android framebuffer */
    uint32_t guest_width;
    uint32_t guest_height;
    uint32_t guest_bpp;
    uint32_t guest_stride;
    uint8_t *guest_fb;       /* mapped from guest */

    /* Host CodeOS framebuffer */
    uint32_t host_width;
    uint32_t host_height;

    /* Scaling */
    double scale_x;
    double scale_y;
    int    scaled_width;
    int    scaled_height;

    /* Damage tracking */
    uint64_t last_frame_seq;
    int      dirty;

    /* Display bridge integration */
    int      bridge_id;
} android_display_t;

/* ─── Init ─── */
int android_display_init(void);

/* ─── Display bridge lifecycle ─── */
int android_display_create(int container_id);
int android_display_destroy(int container_id);
int android_display_activate(int container_id);
int android_display_deactivate(int container_id);

/* ─── Frame forwarding ─── */
int android_display_blit_frame(int container_id);
int android_display_blit_region(int container_id,
                                 uint32_t x, uint32_t y,
                                 uint32_t w, uint32_t h);

/* ─── Input forwarding ─── */
int android_display_send_key(int container_id, uint32_t key, int down);
int android_display_send_mouse(int container_id, int32_t x, int32_t y,
                               uint32_t buttons);
int android_display_send_wheel(int container_id, int32_t dx, int32_t dy);

/* ─── Display info ─── */
int android_display_set_resolution(int container_id,
                                    uint32_t width, uint32_t height);
int android_display_get_info(int container_id,
                              uint32_t *width, uint32_t *height, uint32_t *bpp);

#endif
