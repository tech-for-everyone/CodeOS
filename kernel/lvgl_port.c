/* lvgl_port.c — LVGL hardware/OS port for CodeOS.
 *
 * Display  : 32bpp framebuffer (direct pixel copy; lv_color_t byte layout
 *             matches fb's 0x00RRGGBB memory format).
 * Input    : PS/2 keyboard + mouse via the kernel drivers. Input stays
 *             disabled unless lvgl_port_set_input_enabled(1) is called so
 *             LVGL never steals events from the Qt desktop.
 * Tick     : kernel timer (LV_TICK_CUSTOM in lv_conf.h).
 * Memory   : kernel heap (LV_MEM_CUSTOM in lv_conf.h).
 */

#include "lvgl_port.h"
#include "fb.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "string.h"
#include "mm.h"
#include "input.h"
#include "user_wm.h"

#include "lvgl/lvgl.h"

static lv_disp_draw_buf_t lvgl_draw_buf;
static lv_disp_t *lvgl_disp;
static lv_indev_t *lvgl_pointer;
static lv_indev_t *lvgl_keypad;
static int lvgl_ready;
static int lvgl_input_enabled;

/* Small static fallback if the heap can't satisfy the draw buffers. */
#define LVGL_FALLBACK_PX (1280 * 16)  /* 16 rows split into 2x8 rows (~80KB fallback) */
static lv_color_t lvgl_fallback_buf[LVGL_FALLBACK_PX];

static void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                          lv_color_t *color_p) {
    int x1 = area->x1;
    int y1 = area->y1;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    uint32_t *fbuf = fb_get_active_buffer();
    int stride_words = fb_get_pitch() / 4;

    if (fbuf && stride_words > 0) {
        for (int y = 0; y < h; y++) {
            memcpy(fbuf + (y1 + y) * stride_words + x1,
                   color_p + (size_t)y * w, (size_t)w * sizeof(lv_color_t));
        }
    }
    lv_disp_flush_ready(disp_drv);
}

static void lvgl_pointer_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    if (lvgl_input_enabled)
        input_poll(); /* drain PS/2/USB so mouse state + key buffer advance */
    data->point.x = mouse_get_x();
    data->point.y = mouse_get_y();
    if (lvgl_input_enabled)
        data->state = mouse_get_buttons() & MOUSE_LEFT
                          ? LV_INDEV_STATE_PRESSED
                          : LV_INDEV_STATE_RELEASED;
    else
        data->state = LV_INDEV_STATE_RELEASED;

    /* Android app windows: route pointer events + compose canvases. */
    user_wm_input_pointer(data->point.x, data->point.y,
                          data->state == LV_INDEV_STATE_PRESSED);
    user_wm_tick();
}

static uint32_t lvgl_keymap(int key) {
    switch (key) {
    case KEY_UP:    return LV_KEY_UP;
    case KEY_DOWN:  return LV_KEY_DOWN;
    case KEY_LEFT:  return LV_KEY_LEFT;
    case KEY_RIGHT: return LV_KEY_RIGHT;
    case KEY_HOME:  return LV_KEY_HOME;
    case KEY_END:   return LV_KEY_END;
    case KEY_DEL:   return LV_KEY_DEL;
    case '\r':      return LV_KEY_ENTER;
    default:        return (uint32_t)key;
    }
}

static void lvgl_keypad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;
    if (!lvgl_input_enabled)
        return;
    input_poll(); /* feed key_buf (and input.c queue, which Qt ignores here) */
    key_event_t ev;
    if (keyboard_get_event(&ev)) {
        data->key = lvgl_keymap(ev.keycode);
        data->state = ev.pressed ? LV_INDEV_STATE_PRESSED
                                 : LV_INDEV_STATE_RELEASED;
        /* Android app windows: mirror key events if an app window is active. */
        user_wm_input_key((uint8_t)data->key, ev.pressed ? 1 : 0);
    }
    user_wm_tick();
}

void lvgl_port_set_input_enabled(int enabled) {
    lvgl_input_enabled = enabled;
    if (enabled)
        keyboard_flush();
    else
        input_flush_events();
}

int lvgl_port_input_enabled(void) {
    return lvgl_input_enabled;
}

void *lvgl_port_get_pointer(void) {
    return lvgl_pointer;
}

void *lvgl_port_get_keypad(void) {
    return lvgl_keypad;
}

int lvgl_port_ready(void) {
    return lvgl_ready;
}

void lvgl_port_init(void) {
    if (lvgl_ready)
        return;
    lv_init();

    int w = (int)fb_getwidth();
    int h = (int)fb_getheight();
    if (w <= 0) w = 1280;
    if (h <= 0) h = 800;

    /* Use static fallback draw buffers (kernel perm-allocator can't handle
     * large allocations). Two small buffers for partial flushing. */
    size_t buf_size_px = LVGL_FALLBACK_PX / 2;
    lv_color_t *buf1 = lvgl_fallback_buf;
    lv_color_t *buf2 = lvgl_fallback_buf + buf_size_px;
    lv_disp_draw_buf_init(&lvgl_draw_buf, buf1, buf2, buf_size_px);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = (lv_coord_t)w;
    disp_drv.ver_res = (lv_coord_t)h;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &lvgl_draw_buf;
    lvgl_disp = lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t ptr_drv;
    lv_indev_drv_init(&ptr_drv);
    ptr_drv.type = LV_INDEV_TYPE_POINTER;
    ptr_drv.read_cb = lvgl_pointer_read_cb;
    lvgl_pointer = lv_indev_drv_register(&ptr_drv);

    static lv_indev_drv_t kbd_drv;
    lv_indev_drv_init(&kbd_drv);
    kbd_drv.type = LV_INDEV_TYPE_KEYPAD;
    kbd_drv.read_cb = lvgl_keypad_read_cb;
    lvgl_keypad = lv_indev_drv_register(&kbd_drv);

    lvgl_ready = 1;
    (void)lvgl_disp;
}
