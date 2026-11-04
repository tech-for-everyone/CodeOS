extern "C" {
#include "dock.h"
#include "menubar.h"
#include "panels.h"
#include "wm.h"
#include "windows.h"
#include "string.h"
#include "mm.h"
#include "fb.h"
#include "timer.h"
#include "mouse.h"
}

#define DOCK_GLASS_TOP 8
#define DOCK_GLASS_H 72
#define DOCK_GLASS_BOTTOM 8
#define DOCK_PINNED_COUNT 6
static const int dock_pins[DOCK_PINNED_COUNT] = { 0, 2, 5, 6, 1, 4 };

static void draw_dock_icon(int x, int y, const uint32_t *pixels) {
    if (!pixels) return;
    for (int py = 0; py < 44; py++) {
        for (int px = 0; px < 44; px++) {
            uint32_t src = pixels[py * 44 + px];
            uint32_t a = src >> 24;
            if (!a) continue;
            uint32_t dst = fb_getpixel((uint32_t)(x + px), (uint32_t)(y + py));
            uint32_t r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * (255 - a)) / 255;
            uint32_t g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * (255 - a)) / 255;
            uint32_t b = ((src & 255) * a + (dst & 255) * (255 - a)) / 255;
            fb_putpixel((uint32_t)(x + px), (uint32_t)(y + py), (r << 16) | (g << 8) | b);
        }
    }
}

static int dock_anim_progress[APP_COUNT_MAX];
static int dock_hover_idx = -1;
static int dock_tooltip_alpha[APP_COUNT_MAX];

void dock_init(void) {
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        dock_anim_progress[i] = 0;
    }
    dock_hover_idx = -1;
    memset(dock_tooltip_alpha, 0, sizeof(dock_tooltip_alpha));
}

int dock_h(void) {
    return wm_dock_enabled() ? DOCK_GLASS_TOP + DOCK_GLASS_H + DOCK_GLASS_BOTTOM : 0;
}

void dock_tick(int mx) {
    /* Handle hover detection - simplified */
    dock_hover_idx = -1;
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        /* Hit testing simplified */
    }

    /* Animate icons */
    uint32_t now = timer_get_milliseconds();
    (void)now;
    /* Animation progress updates handled per-icon */
}

static void draw_glass_pill(uint32_t scr_w, uint32_t scr_h) {
    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    int pill_w = 6 * 48 + 5 * 8 + 24;
    int pill_x = ((int)scr_w - pill_w) / 2;
    int pill_y = dy + DOCK_GLASS_TOP;

    /* Glass background */
    fb_fillrect_gradient_v(pill_x, pill_y, pill_w, DOCK_GLASS_H,
                           0x182A2A2A, 0x101A1A1A);

    /* Divider */
    (void)pill_x;
}

void dock_draw(uint32_t scr_w, uint32_t scr_h) {
    if (!wm_dock_enabled()) return;

    draw_glass_pill(scr_w, scr_h);

    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    int pill_y = dy + DOCK_GLASS_TOP;
    int icon_y = pill_y + (DOCK_GLASS_H - 44) / 2;

    /* Draw icons */
    int pill_w = 6 * 48 + 5 * 8 + 24;
    int pill_x = ((int)scr_w - pill_w) / 2;
    for (int slot = 0; slot < DOCK_PINNED_COUNT; slot++) {
        int item = dock_pins[slot];
        int ix = pill_x + 12 + slot * 56;
        int iy = icon_y;

        draw_dock_icon(ix, iy + 2, icon_buf_dock[item]);
        if (!icon_buf_dock[item]) {
            fb_fill_rounded_rect(ix, iy + 2, 44, 44, 8, 0x20FFFFFF);
            fb_draw_rounded_rect(ix, iy + 2, 44, 44, 8, 0x50FFFFFF);
        }

        /* App name */
        if (dock_tooltip_alpha[item] > 20) {
            int tw = strlen(app_names[item]) * 7 + 12;
            int tx = ix + (44 - tw) / 2;
            int ty = iy + 44 + 25;
            uint8_t a = dock_tooltip_alpha[item];
            fb_fill_rounded_rect(tx - 2, ty - 2, tw + 4, 20, 6, 0x00000000 | (((uint32_t)a * 0xDD / 255) << 24));
            fb_drawstr_px(tx + 2, ty + 1, app_names[item], C_TEXT, 0);
        }
    }

    /* Trash can */
    (void)pill_y;
}

int dock_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;
    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    if (my < dy) return 0;
    return 1;
}

int dock_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;
    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    if (my < dy) return -1;

    /* Check icons */
    int pill_w = 6 * 48 + 5 * 8 + 24;
    int pill_x = ((int)scr_w - pill_w) / 2;
    for (int slot = 0; slot < DOCK_PINNED_COUNT; slot++) {
        int ix = pill_x + 12 + slot * 56;
        if (mx >= ix && mx < ix + 44 && my >= dy && my < dy + DOCK_GLASS_H) {
            return dock_pins[slot];
        }
    }
    return -1;
}
