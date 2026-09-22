#include "about.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"

#define ABOUT_W 420
#define ABOUT_H 340

static int about_opened;
static int about_win;

int about_open(void) {
    if (about_opened) return 1;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int wx = (sw - ABOUT_W) / 2;
    int wy = (sh - ABOUT_H) / 3;
    about_win = desktop_new_window(wx, wy, ABOUT_W, ABOUT_H, "About CodeOS", 0xFFFFFFFF, C_GLASS_DARK);
    if (about_win < 0) return 0;
    about_opened = 1;
    desktop_redraw();
    return 1;
}

int about_is_open(void) {
    return about_opened;
}

void about_close(void) {
    about_opened = 0;
}

void about_draw(void) {
    if (!about_opened) return;
    window_t *w = desktop_get_window(about_win);
    if (!w || !w->visible) return;

    int cx = w->x + 4;
    int cy = w->y + 4;
    int cw = w->w - 8;
    int ch = w->h - 8;

    /* Glass card background with subtle gradient */
    fb_fillrect_gradient_v(cx, cy, cw, ch,
                           0x302A2A2A, 0x201A1A1A);

    /* Top accent bar */
    fb_fillrect(cx, cy, cw, 2, 0x400066FF);

    /* CodeOS text */
    fb_drawstr_px(cx + 12, cy + 12, "CodeOS", C_BLUE, 0);
    fb_drawstr_px(cx + 12, cy + 30, "Hobby Operating System", C_TEXT, 0);

    /* Version line */
    char ver[64];
    snprintf(ver, sizeof(ver), "Version: %s %s", KERNEL_NAME, KERNEL_VERSION);
    fb_drawstr_px(cx + 12, cy + 50, ver, C_SUBTEXT0, 0);

    /* Separator */
    fb_fillrect(cx + 12, cy + 74, cw - 24, 1, 0x30FFFFFF);

    /* Description */
    fb_drawstr_px(cx + 12, cy + 80,
        "A hobby OS with liquid-glass GUI panels",
        C_SUBTEXT0, 0);
    fb_drawstr_px(cx + 12, cy + 98,
        "Built with Qt6 and custom kernel",
        C_SUBTEXT0, 0);

    /* Close button */
    int bx = cx + cw - 24;
    int by = cy + 12;
    fb_fill_rounded_rect(bx, by, 14, 14, 4, 0x40000000);
    fb_drawstr_px(bx + 2, by + 1, "×", 0xE00000, 0);

    /* OK button */
    int ok_x = cx + cw / 2 - 30;
    int ok_y = cy + ch - 36;
    fb_fill_rounded_rect(ok_x, ok_y, 60, 22, 6, 0x300066FF);
    fb_drawstr_px(ok_x + 10, ok_y + 3, "OK", C_WHITE, 0);
}