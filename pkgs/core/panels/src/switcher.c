extern "C" {
#include "switcher.h"
#include "wm.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
}

#define SWITCHER_MAX 16

static window_t switcher_windows[SWITCHER_MAX];
static int switcher_count = 0;
static int switcher_selected = 0;

void switcher_init(void) {
    switcher_count = 0;
    switcher_selected = 0;
}

void switcher_activate(void) {
}

void switcher_deactivate(void) {
}

void switcher_cancel(void) {
}

int switcher_active(void) {
    return switcher_selected;
}

void switcher_next(void) {
    switcher_selected = (switcher_selected + 1) % switcher_count;
}

void switcher_prev(void) {
    switcher_selected = (switcher_selected - 1 + switcher_count) % switcher_count;
}

void switcher_draw(uint32_t scr_w, uint32_t scr_h) {
    int w = 400;
    int h = 300;
    int x = (scr_w - w) / 2;
    int y = (scr_h - h) / 2;

    /* Glass window */
    fb_fillrect_gradient_v(x, y, w, h,
                           0x302A2A2A, 0x201A1A1A);

    /* Title */
    fb_drawstr_px(x + 8, y + 10, "App Switcher", C_BLUE, 0);

    /* Divider */
    fb_fillrect(x + 4, y + 32, w - 8, 2, 0x30FFFFFF);

    /* List windows */
    switcher_count = 0;
    for (int i = 0; i < window_count; i++) {
        window_t *w = window_get(i);
        if (w->visible && !w->minimized) {
            strncpy(switcher_windows[switcher_count].title, w->title, 31);
            switcher_windows[switcher_count].title[31] = 0;
            switcher_count++;
            if (switcher_count >= SWITCHER_MAX) break;
        }
    }

    /* Draw windows */
    for (int i = 0; i < switcher_count; i++) {
        int yi = y + 38 + (i * 24);
        if (yi > y + h - 30) break;

        if (i == switcher_selected) {
            fb_fill_rounded_rect(x + 4, yi, w - 8, 22, 6, 0x400066FF);
            fb_drawstr_px(x + 8, yi + 3, switcher_windows[i].title, C_WHITE, 0);
        } else {
            fb_fill_rounded_rect(x + 4, yi, w - 8, 22, 6, 0x30808080);
            fb_drawstr_px(x + 8, yi + 3, switcher_windows[i].title, C_TEXT, 0);
        }
    }

    /* Close */
    fb_fill_rounded_rect(x + w - 24, y + 8, 16, 16, 4, 0x30000000);
    fb_drawstr_px(x + w - 18, y + 8, "×", 0x800000, 0);
}

int switcher_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    int w = 400, h = 300;
    int x = (scr_w - w) / 2;
    int y = (scr_h - h) / 2;

    /* Close button */
    if (mx >= x + w - 24 && mx < x + w - 8 &&
        my >= y + 8 && my < y + 24) {
        return -1;
    }

    /* Select window */
    for (int i = 0; i < switcher_count; i++) {
        int yi = y + 38 + (i * 24);
        if (mx >= x + 8 && mx < x + w - 8 &&
            my >= yi && my < yi + 22) {
            return i;
        }
    }

    return -1;
}