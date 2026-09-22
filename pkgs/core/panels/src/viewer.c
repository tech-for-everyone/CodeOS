#include "viewer.h"
#include "desktop.h"
#include "img.h"
#include "pixelman.h"
#include "windows.h"
#include "string.h"
#include "fb.h"

#define VIEWER_W 680
#define VIEWER_H 520

static int viewer_opened;
static int viewer_win;
static int scene_idx;

int viewer_open(void) {
    if (viewer_opened) return 1;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int wx = (sw - VIEWER_W) / 2;
    int wy = (sh - VIEWER_H) / 3;
    viewer_win = desktop_new_window(wx, wy, VIEWER_W, VIEWER_H, "Image Viewer", 0xFFFFFFFF, 0xFF1a1a2e);
    if (viewer_win < 0) return 0;
    viewer_opened = 1;
    scene_idx = 0;
    desktop_redraw();
    return 1;
}

int viewer_open_with(const char *name) {
    (void)name;
    return viewer_open();
}

int viewer_is_open(void) {
    return viewer_opened;
}

void viewer_close(void) {
    viewer_opened = 0;
}

void viewer_draw(void) {
    if (!viewer_opened) return;
    window_t *w = desktop_get_window(viewer_win);
    if (!w || !w->visible) return;

    int cx = w->x + 4;
    int cy = w->y + 22;
    int cw = w->w - 8;
    int ch = w->h - 26;
    if (cw < 20 || ch < 20) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    pm_rect_t clip = {0, 0, (int)fb_getwidth(), (int)fb_getheight()};

    /* Draw the scene into the window client area */
    if (scene_idx == 0)
        img_render_sunset(buf + cy * (stride/4) + cx, stride, cw, ch);
    else
        img_render_night(buf + cy * (stride/4) + cx, stride, cw, ch);

    /* Toolbar at bottom with navigation */
    int tb_y = cy + ch - 32;
    pm_composite_rect(buf, stride, clip, cx, tb_y, cw, 32, 0xFF222244);
    pm_composite_rect(buf, stride, clip, cx, tb_y - 1, cw, 1, 0xFF444466);

    /* Scene name label */
    const char *scene_name = (scene_idx == 0) ? "Sunset Mountains" : "Night Sky";
    fb_drawstr_px(cx + 10, tb_y + 8, scene_name, C_TEXT, 0xFF222244);

    /* Prev / Next buttons */
    int btn_x = cx + cw - 80;
    pm_composite_fill_rounded_rect(buf, stride, clip, btn_x, tb_y + 4, 30, 24, 4, 0xFF444466);
    fb_drawstr_px(btn_x + 9, tb_y + 6, "<", C_TEXT, 0xFF444466);
    pm_composite_fill_rounded_rect(buf, stride, clip, btn_x + 36, tb_y + 4, 30, 24, 4, 0xFF444466);
    fb_drawstr_px(btn_x + 45, tb_y + 6, ">", C_TEXT, 0xFF444466);

    /* Info text */
    char info[32];
    snprintf(info, sizeof(info), "%dx%d", cw, ch);
    fb_drawstr_px(cx + cw - 180, tb_y + 8, info, C_OVERLAY0, 0xFF222244);
}

int viewer_click(int mx, int my) {
    if (!viewer_opened) return 0;
    window_t *w = desktop_get_window(viewer_win);
    if (!w || !w->visible) return 0;

    int cx = w->x + 4;
    int cy = w->y + 22;
    int cw = w->w - 8;
    int ch = w->h - 26;

    /* Toolbar buttons */
    int tb_y = cy + ch - 32;
    int btn_x = cx + cw - 80;

    /* Prev */
    if (mx >= btn_x && mx < btn_x + 30 && my >= tb_y + 4 && my < tb_y + 28) {
        scene_idx = (scene_idx == 0) ? 1 : 0;
        desktop_redraw();
        return 1;
    }
    /* Next */
    if (mx >= btn_x + 36 && mx < btn_x + 66 && my >= tb_y + 4 && my < tb_y + 28) {
        scene_idx = (scene_idx == 0) ? 1 : 0;
        desktop_redraw();
        return 1;
    }

    return 0;
}
