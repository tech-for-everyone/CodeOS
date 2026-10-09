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
    int pill_x = (scr_w - 400) / 2;
    int pill_w = 400;
    int pill_y = dy + DOCK_GLASS_TOP;

    /* Glass background */
    fb_fillrect_gradient_v(pill_x, pill_y, pill_w, DOCK_GLASS_H,
                           0x182A2A2A, 0x101A1A1A);

    /* Divider */
    int div_x = pill_x + 200;
    fb_fillrect(div_x - 2, pill_y + 4, 4, DOCK_GLASS_H - 8, 0x30FFFFFF);
}

void dock_draw(uint32_t scr_w, uint32_t scr_h) {
    if (!wm_dock_enabled()) return;

    draw_glass_pill(scr_w, scr_h);

    int dy = scr_h - DOCK_GLASS_TOP - DOCK_GLASS_H - DOCK_GLASS_BOTTOM;
    int pill_y = dy + DOCK_GLASS_TOP;
    int icon_y = pill_y + (DOCK_GLASS_H - 44) / 2;

    /* Draw icons */
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        int ix = 30 + i * (44 + 12);
        int iy = icon_y;

        /* Simple icon rect */
        fb_fill_rounded_rect(ix, iy + 2, 44, 44, 8, 0x20FFFFFF);
        fb_draw_rounded_rect(ix, iy + 2, 44, 44, 8, 0x50FFFFFF);

        /* App name */
        if (dock_tooltip_alpha[i] > 20) {
            int tw = strlen(app_names[i]) * 7 + 12;
            int tx = ix + (44 - tw) / 2;
            int ty = iy + 44 + 25;
            uint8_t a = dock_tooltip_alpha[i];
            fb_fill_rounded_rect(tx - 2, ty - 2, tw + 4, 20, 6, 0x00000000 | (((uint32_t)a * 0xDD / 255) << 24));
            fb_drawstr_px(tx + 2, ty + 1, app_names[i], C_TEXT, 0);
        }
    }

    /* Trash can */
    int trash_x = (scr_w - 400) / 2 + 350;
    int trash_y = pill_y + 8;
    fb_fill_rounded_rect(trash_x, trash_y, 40, 24, 6, 0x30000000);
    fb_draw_rounded_rect(trash_x, trash_y, 40, 24, 6, 0x50FFFFFF);
    fb_drawstr_px(trash_x + 8, trash_y + 2, "Trash", C_TEXT, 0);
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