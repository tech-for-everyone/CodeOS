#include "windows.h"
#include "desktop.h"
#include "kprintf.h"
#include "string.h"
#include "../arch/x86_64/fb.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/mouse.h"

/* ── Module: Sys Info ── */

static int sys_info_opened;
static int sys_info_win;
static int sys_info_bg;

static void sys_info_run(void) {
    sys_info_opened = 1;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int ww = 360, wh = 200;
    int wx = (sw - ww) / 2;
    int wy = (sh - wh) / 2;
    sys_info_win = desktop_new_window(wx, wy, ww, wh, "Sys Info", C_TEXT, C_BASE);
    sys_info_bg = C_BASE;
}

static void sys_info_draw_handler(int cx, int cy, int cw, int ch, uint32_t bg) {
    (void)cw; (void)ch;
    fb_draw_shadow_layered(cx, cy, 352, 152, CARD_RADIUS, SHADOW_ALPHA, SHADOW_OFFSET, 3);
    ui_draw_card(cx, cy, 352, 152, C_SURFACE0);
    fb_drawstr_px(cx + (16), cy + (16), "CodeOS Kernel", C_SKY, bg);
    fb_drawstr_px(cx + (16), cy + (40), "Version 1.0", C_TEXT, bg);
    fb_drawline(cx + 16, cy + 58, cx + 336, cy + 58, C_SURFACE2);
    fb_drawstr_px(cx + (16), cy + (68), "Arch: x86_64", C_SUBTEXT0, bg);
    fb_drawstr_px(cx + (16), cy + (92), "Sys Info Demo", C_OVERLAY0, bg);
    fb_drawstr_px(cx + (16), cy + (116), "ESC to close", C_SUBTEXT1, bg);
}

static void sys_info_key_handler(int k) {
    if (!sys_info_opened) return;
    if ((k == 27)) {
        desktop_close_window(sys_info_win);
    }
}

/* ── Public API (declare in panels.h + add to panels.cpp launch_app) ── */

int sys_info_open(void) {
    if (sys_info_opened) return 1;
    sys_info_run();
    desktop_redraw();
    return 1;
}

int sys_info_is_open(void) { return sys_info_opened; }

void sys_info_close(void) { sys_info_opened = 0; }

void sys_info_draw(void) {
    if (!sys_info_opened) return;
    window_t *w = desktop_get_window(sys_info_win);
    if (!w || !w->visible) return;
    int cx = w->x + 4;
    int cy = w->y + 22;
    int cw = w->w - 8;
    int ch = w->h - 26;
    if (cw < 20 || ch < 20) return;
    sys_info_draw_handler(cx, cy, cw, ch, sys_info_bg);
}

void sys_info_key(int k) {
    if (!sys_info_opened) return;
    sys_info_key_handler(k);
}
