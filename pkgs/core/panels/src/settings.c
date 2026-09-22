#include "settings.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"

#define SET_W 360
#define SET_H 320

static int set_opened;
static int set_win;
static int set_page;
static int set_scroll;

int settings_open(void) {
    if (set_opened) return 1;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int sx = (sw - SET_W) / 2;
    int sy = (sh - SET_H) / 3;
    set_win = desktop_new_window(sx, sy, SET_W, SET_H, "Settings", 0xFFFFFFFF, C_GLASS_DARK);
    if (set_win < 0) return 0;
    set_opened = 1;
    set_page = 0;
    set_scroll = 0;
    desktop_redraw();
    return 1;
}

int settings_is_open(void) { return set_opened; }
void settings_close(void) { set_opened = 0; }

void settings_mousemove(int mx, int my) {
    if (!set_opened) return;
    window_t *w = desktop_get_window(set_win);
    if (!w || !w->visible) return;

    int cx = w->x + 4;
    int cy = w->y + 22;
    int new_hover = -1;

    if (mx >= cx && mx < cx + 180) {
        int sy = cy + 8;
        for (int i = 0; i < 3; i++) {
            if (my >= sy && my < sy + 28) {
                new_hover = i;
                break;
            }
            sy += 32;
        }
    }
    if (new_hover != set_scroll) {
        set_scroll = new_hover;
        desktop_redraw();
    }
}

void settings_draw(void) {
    if (!set_opened) return;
    window_t *w = desktop_get_window(set_win);
    if (!w || !w->visible) return;

    int cx = w->x + 4;
    int cy = w->y + 22;
    int cw = w->w - 8;
    int ch = w->h - 26;
    if (cw < 20 || ch < 20) return;

    fb_fillrect(cx, cy, cw, ch, C_GLASS_DARK);

    /* Sidebar */
    fb_fillrect(cx, cy, 160, ch, C_CRUST);
    fb_draw_rounded_rect(cx, cy, 160, ch, 8, C_SURFACE1);

    int sy = cy + 8;
    for (int i = 0; i < 3; i++) {
        int is_sel = (i == set_page);
        fb_fill_rounded_rect(cx + 6, sy, 32, 28, 6,
            is_sel ? C_SKY : C_CRUST);
        fb_drawstr_px(cx + 20, sy + 4, (i == 0 ? "General" : i == 1 ? "Network" : "About"),
            is_sel ? C_SKY : C_TEXT);
        sy += 36;
    }

    /* Content area */
    int lx = cx + 168;
    int lw = cw - 168;
    int ly = cy + 8;
    int lh = ch - 16;

    fb_fillrect(lx - 4, ly - 4, lw + 8, lh + 8, C_GLASS_DARK);
    fb_draw_rounded_rect(lx - 4, ly - 4, lw + 8, lh + 8, 6, C_SURFACE1);

    switch (set_page) {
        case 0:
            fb_drawstr_px(lx + 12, ly + 8, "General Settings", C_TEXT, C_GLASS_DARK);
            fb_drawstr_px(lx + 12, ly + 30, "Theme: Light / Dark", C_DIM, 0);
            fb_drawstr_px(lx + 12, ly + 55, "Font Size: 12 / 14 / 16", C_DIM, 0);
            break;
        case 1:
            fb_drawstr_px(lx + 12, ly + 8, "Network Settings", C_TEXT, C_GLASS_DARK);
            fb_drawstr_px(lx + 12, ly + 30, "Interface: eth0", C_DIM, 0);
            fb_drawstr_px(lx + 12, ly + 55, "Speed: 100 Mbps", C_DIM, 0);
            break;
        case 2:
            fb_drawstr_px(lx + 12, ly + 8, "About CodeOS", C_TEXT, C_GLASS_DARK);
            fb_drawstr_px(lx + 12, ly + 30, "Version 1.0.0", C_DIM, 0);
            fb_drawstr_px(lx + 12, ly + 55, "Jengine: Integrated", C_DIM, 0);
            break;
    }
}

int settings_click(int mx, int my) {
    if (!set_opened) return 0;
    window_t *w = desktop_get_window(set_win);
    if (!w || !w->visible) return 0;

    int cx = w->x + 4;
    int cy = w->y + 22;

    /* Sidebar clicks */
    if (mx >= cx && mx < cx + 160) {
        int sy = cy + 8;
        for (int i = 0; i < 3; i++) {
            if (my >= sy && my < sy + 28) {
                set_page = i;
                desktop_redraw();
                return 1;
            }
            sy += 32;
        }
        return 1;
    }

    /* Close on content click or outside */
    settings_close();
    return 1;
}