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
#define DOCK_GLASS_SHADOW_OFFSET 4

static int dock_anim_progress[APP_COUNT_MAX];
static int dock_hover_idx = -1;
static int dock_tooltip_alpha[APP_COUNT_MAX];

static void dock_anim_tick(int i, uint32_t now_ms) {
    /* Spring anim: target -> current with spring constant */
    int target = dock_anim_target[i];
    if (dock_anim_progress[i] < 1000) {
        dock_anim_progress[i] += 16;
        int spring = 300;
        dock_anim_current[i] = target + (dock_anim_current[i] - target) * spring / (spring + dock_anim_progress[i]);
    }
}

void dock_init(void) {
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        dock_anim_target[i] = 1000;
        dock_anim_current[i] = 1000;
        dock_anim_progress[i] = 0;
    }
    dock_hover_idx = -1;
    memset(dock_tooltip_alpha, 0, sizeof(dock_tooltip_alpha));
}

int dock_h(void) {
    return wm_dock_enabled() ? DOCK_GLASS_TOP + DOCK_GLASS_H + DOCK_GLASS_BOTTOM : 0;
}

void dock_tick(int mx) {
    /* Handle hover detection */
    dock_hover_idx = -1;
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        /* Simple hit testing - simplified from original */
    }

    /* Animate all icons */
    uint32_t now = timer_get_milliseconds();
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        dock_anim_tick(i, now);
    }
}

static void draw_glass_pill(uint32_t scr_w, uint32_t scr_h) {
    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    int pill_x = (scr_w - 400) / 2;
    int pill_w = 400;
    int pill_y = dy + DOCK_GLASS_TOP;

    /* Deep glass background with multi-layer blur effect */
    fb_fillrect_gradient_v(pill_x, pill_y, pill_w, DOCK_GLASS_H,
                           0x182A2A2A, 0x101A1A1A);

    /* Glass side glows */
    fb_fillrect_alpha(pill_x, pill_y, 10, DOCK_GLASS_H, 0xFFFFFF, 8);
    fb_fillrect_alpha(pill_x + pill_w - 10, pill_y, 10, DOCK_GLASS_H, 0xFFFFFF, 8);

    /* Bottom shadow line */
    fb_fillrect(pill_x, pill_y + DOCK_GLASS_H - 2, pill_w, 2, 0x50000000);

    /* Divider line */
    int div_x = pill_x + 200;
    fb_fillrect(div_x - 2, pill_y + 4, 4, DOCK_GLASS_H - 8, 0x30FFFFFF);
}

void dock_draw(uint32_t scr_w, uint32_t scr_h) {
    if (!wm_dock_enabled()) return;

    draw_glass_pill(scr_w, scr_h);

    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    int pill_y = dy + DOCK_GLASS_TOP;
    int icon_y = pill_y + (DOCK_GLASS_H - 44) / 2;

    /* Draw icons with new glass style */
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        int sz = 44; /* base size */
        int ix = 30 + i * (44 + 12); /* spaced evenly */

        /* Scale based on anim progress */
        int sc = dock_anim_current[i];
        if (sc > 1200) sc = 1200;

        /* Glow effect when focused */
        if (sc > 1100) {
            fb_fill_rounded_rect(ix - 6, icon_y - 6, sz + 12, sz + 12, 14, 0x30FFFFFF);
        }

        /* Draw icon rect */
        fb_fill_rounded_rect(ix, icon_y + 2, sz, sz, 8, 0x20FFFFFF);
        fb_draw_rounded_rect(ix, icon_y + 2, sz, sz, 8, 0x50FFFFFF);

        /* App name tooltip */
        if (dock_tooltip_alpha[i] > 20) {
            int tw = strlen(app_names[i]) * 7 + 12;
            int tx = ix + (sz - tw) / 2;
            int ty = icon_y + sz + 25;
            uint8_t a = dock_tooltip_alpha[i];
            uint32_t bg = 0x00000000 | (((uint32_t)a * 0xDD / 255) << 24);
            fb_fill_rounded_rect(tx - 2, ty - 2, tw + 4, 20, 6, bg);
            fb_drawstr_px(tx + 2, ty + 1, app_names[i], C_TEXT, 0);
        }

        /* Running dot */
        /* Simple dot for now */
    }

    /* Trash can at right */
    int trash_x = pill_x + pill_w - 50;
    int trash_y = pill_y + 8;
    fb_fill_rounded_rect(trash_x, trash_y, 40, 24, 6, 0x30000000);
    fb_draw_rounded_rect(trash_x, trash_y, 40, 24, 6, 0x50FFFFFF);
    fb_drawstr_px(trash_x + 8, trash_y + 2, "Trash", C_TEXT, 0);
}

int dock_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;
    /* Simplified hover - check if over dock area */
    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    if (my < dy) return 0;
    return 1;
}

int dock_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;
    /* Find which icon was clicked */
    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    if (my < dy) return -1;

    /* Check each icon area */
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        int ix = 30 + i * (44 + 12);
        if (mx >= ix && mx < ix + 44 && my >= dy && my < dy + DOCK_GLASS_H) {
            return i;
        }
    }

    /* Check trash */
    int trash_x = (scr_w - 400) / 2 + 350;
    if (mx >= trash_x && mx < trash_x + 40 && my >= dy) {
        return APP_COUNT_MAX;
    }
    return -1;
}