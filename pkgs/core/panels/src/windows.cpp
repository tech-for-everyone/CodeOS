extern "C" {
#include "windows.h"
#include "wm.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
#include "mouse.h"
#include "input.h"
#include "keyboard.h"
}

#define MAX_WINDOWS 32
#define FRAME_H 28
#define TITLE_BAR_H 24

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int focused_idx = -1;

static int find_window(const char *title) {
    for (int i = 0; i < window_count; i++) {
        if (strcmp(windows[i].title, title) == 0) return i;
    }
    return -1;
}

int window_new(int x, int y, int w, int h, const char *title, uint32_t fg, uint32_t bg) {
    if (window_count >= MAX_WINDOWS) return -1;
    window_t *w = &windows[window_count];
    w->x = x;
    w->y = y;
    w->w = w;
    w->h = h;
    strncpy(w->title, title, 31);
    w->title[31] = 0;
    w->has_close = 1;
    w->close_x = x + w - 20;
    w->close_y = y + 4;
    w->visible = 1;
    w->minimized = 0;
    window_count++;
    return window_count - 1;
}

void window_close(int idx) {
    if (idx < 0 || idx >= window_count) return;
    window_t *w = &windows[idx];
    w->visible = 0;
}

void window_set_title(int idx, const char *title) {
    if (idx < 0 || idx >= window_count) return;
    strncpy(windows[idx].title, title, 31);
    windows[idx].title[31] = 0;
}

int window_count(void) { return window_count; }

window_t *window_get(int idx) {
    if (idx < 0 || idx >= window_count) return NULL;
    return &windows[idx];
}

int window_focused(void) { return focused_idx; }

void window_focus_next(void) {
    if (window_count == 0) return;
    focused_idx = (focused_idx + 1) % window_count;
}

void window_focus_prev(void) {
    if (window_count == 0) return;
    focused_idx = (focused_idx - 1 + window_count) % window_count;
}

void window_set_focused(int idx) {
    if (idx >= 0 && idx < window_count) {
        focused_idx = idx;
    }
}

void window_show(int idx, int show) {
    if (idx < 0 || idx >= window_count) return;
    windows[idx].visible = show;
}

static void draw_frame_title_bar(window_t *w, uint32_t scr_w, uint32_t fg, uint32_t bg) {
    int y = w->y;
    /* Glass title bar */
    fb_fillrect_gradient_v(w->x, y, w->w, TITLE_BAR_H,
                           0x302A2A2A, 0x201A1A1A);

    /* Title text */
    fb_drawstr_px(w->x + 8, y + 6, w->title, fg, 0);

    /* Close button */
    int cx = w->x + w->w - 18;
    int cy = y + 4;
    fb_fill_rounded_rect(cx, cy, 14, 14, 4, 0x40000000);
    fb_drawstr_px(cx + 2, cy + 1, "×", 0xE00000, 0);

    /* Minimize button */
    int mx = cx - 18;
    fb_fill_rounded_rect(mx, cy, 14, 14, 4, 0x40000000);
    fb_drawstr_px(mx + 2, cy + 1, "_", 0x800080, 0);
}

void windows_init(void) {
    window_count = 0;
    focused_idx = -1;
}

void window_draw_all(uint32_t scr_w, uint32_t scr_h) {
    for (int i = 0; i < window_count; i++) {
        window_t *w = &windows[i];
        if (!w->visible || w->minimized) continue;

        /* Draw frame */
        draw_frame_title_bar(w, scr_w, C_BLUE, C_GLASS_DARK);

        /* Content area */
        int content_y = w->y + TITLE_BAR_H;
        int content_h = w->h - TITLE_BAR_H;
        if (content_h > 0 && content_y < scr_h) {
            fb_fillrect(w->x + 4, content_y, w->w - 8, content_h, 0x20000000);
        }
    }
}