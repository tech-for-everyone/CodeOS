/* lvgl_wm.c — LVGL Window Manager, placement implementation.
 * Drop-in replacement for the SDL3-based WM. Window placement, focus and
 * hit-testing mirror the old behavior; each window is an LVGL object. */

#include "lvgl_wm.h"
#include "lvgl_port.h"

#include <stddef.h>
#include "string.h"
#include "lvgl/lvgl.h"
#include "kprintf.h"

#define WM_MARGIN 8
#define WM_START_X 24
#define WM_START_Y 24
#define WM_CASCADE_STEP 36

static void cascade_advance(lvgl_wm_t *wm) {
    if (!wm) return;
    wm->cascade_x += wm->cascade_step;
    wm->cascade_y += wm->cascade_step;
    if (wm->cascade_x > wm->screen_w / 2) {
        wm->cascade_x = WM_START_X;
        wm->cascade_y = WM_START_Y;
    }
}

static void window_del_obj(lvgl_wm_window_t *win) {
    if (win->obj) {
        lv_obj_del(win->obj);
        win->obj = 0;
    }
}

static void clamp_rect(const lvgl_wm_t *wm, lvgl_wm_rect_t *rect) {
    int max_w;
    int max_h;
    if (!wm || !rect) return;

    max_w = wm->screen_w - WM_MARGIN * 2;
    max_h = wm->screen_h - WM_MARGIN * 2;
    if (max_w < 1) max_w = 1;
    if (max_h < 1) max_h = 1;
    if (rect->w < 1) rect->w = 1;
    if (rect->h < 1) rect->h = 1;
    if (rect->w > max_w) rect->w = max_w;
    if (rect->h > max_h) rect->h = max_h;
    if (rect->x < 0) rect->x = 0;
    if (rect->y < 0) rect->y = 0;
    if (rect->x + rect->w > wm->screen_w)
        rect->x = wm->screen_w - rect->w;
    if (rect->y + rect->h > wm->screen_h)
        rect->y = wm->screen_h - rect->h;
    if (rect->x < 0) rect->x = 0;
    if (rect->y < 0) rect->y = 0;
}

void lvgl_wm_init(lvgl_wm_t *wm, int screen_w, int screen_h, int titlebar_h) {
    if (!wm) return;
    memset(wm, 0, sizeof(*wm));
    wm->screen_w = screen_w > 0 ? screen_w : 1280;
    wm->screen_h = screen_h > 0 ? screen_h : 800;
    wm->titlebar_h = titlebar_h > 0 ? titlebar_h : LVGL_WM_TITLEBAR_H;
    wm->cascade_step = wm->titlebar_h + WM_MARGIN;
    lvgl_wm_reset(wm);

    lvgl_port_init();

    if (!wm->screen) {
        wm->screen = lv_obj_create(lv_scr_act());
        lv_obj_set_size(wm->screen, wm->screen_w, wm->screen_h);
        lv_obj_set_pos(wm->screen, 0, 0);
    }
}

void lvgl_wm_reset(lvgl_wm_t *wm) {
    int i;
    if (!wm) return;
    for (i = 0; i < wm->count; i++)
        window_del_obj(&wm->windows[i]);
    wm->count = 0;
    wm->next_id = 1;
    wm->focused_id = 0;
    wm->cascade_x = WM_START_X;
    wm->cascade_y = WM_START_Y;
    for (i = 0; i < LVGL_WM_MAX_WINDOWS; i++) {
        wm->windows[i].id = 0;
        wm->windows[i].obj = 0;
        wm->windows[i].visible = 0;
        wm->windows[i].focused = 0;
        wm->windows[i].rect.x = 0;
        wm->windows[i].rect.y = 0;
        wm->windows[i].rect.w = 0;
        wm->windows[i].rect.h = 0;
        wm->windows[i].title[0] = 0;
    }
}

void lvgl_wm_quit(lvgl_wm_t *wm) {
    if (wm) lvgl_wm_reset(wm);
}

int lvgl_wm_create_window(lvgl_wm_t *wm, const char *title, int w, int h,
                          lvgl_wm_rect_t *out_rect) {
    lvgl_wm_window_t *win;
    int i;
    if (!wm || wm->count >= LVGL_WM_MAX_WINDOWS) return 0;

    win = &wm->windows[wm->count];
    win->id = wm->next_id++;
    win->visible = 1;
    win->focused = 0;

    if (title) {
        for (i = 0; title[i] && i < LVGL_WM_TITLE_MAX; i++)
            win->title[i] = title[i];
        win->title[i] = 0;
    } else {
        win->title[0] = 0;
    }

    if (w <= 0) w = 400;
    if (h <= 0) h = 300;
    win->rect.w = w;
    win->rect.h = h;
    win->rect.x = wm->cascade_x;
    win->rect.y = wm->cascade_y;
    clamp_rect(wm, &win->rect);

    if (wm->screen) {
        win->obj = lv_obj_create(wm->screen);
        if (win->obj) {
            lv_obj_set_size(win->obj, win->rect.w, win->rect.h);
            lv_obj_set_pos(win->obj, win->rect.x, win->rect.y);
        }
    }

    cascade_advance(wm);
    wm->count++;

    if (out_rect) *out_rect = win->rect;
    return win->id;
}

void lvgl_wm_destroy_window(lvgl_wm_t *wm, int id) {
    int i;
    if (!wm || id <= 0) return;
    for (i = 0; i < wm->count; i++) {
        if (wm->windows[i].id == id) {
            window_del_obj(&wm->windows[i]);
            for (; i + 1 < wm->count; i++)
                wm->windows[i] = wm->windows[i + 1];
            wm->windows[wm->count - 1].id = 0;
            wm->windows[wm->count - 1].obj = 0;
            wm->windows[wm->count - 1].visible = 0;
            wm->windows[wm->count - 1].focused = 0;
            wm->count--;
            if (wm->focused_id == id) {
                wm->focused_id = wm->count ? wm->windows[wm->count - 1].id : 0;
                if (wm->focused_id)
                    wm->windows[wm->count - 1].focused = 1;
            }
            return;
        }
    }
}

lvgl_wm_window_t *lvgl_wm_find_window(lvgl_wm_t *wm, int id) {
    int i;
    if (!wm || id <= 0) return 0;
    for (i = 0; i < wm->count; i++) {
        if (wm->windows[i].id == id) return &wm->windows[i];
    }
    return 0;
}

int lvgl_wm_hit_test(lvgl_wm_t *wm, int x, int y) {
    int i;
    if (!wm) return 0;
    for (i = wm->count - 1; i >= 0; i--) {
        const lvgl_wm_window_t *win = &wm->windows[i];
        if (!win->visible) continue;
        if (x >= win->rect.x && x < win->rect.x + win->rect.w &&
            y >= win->rect.y && y < win->rect.y + win->rect.h)
            return win->id;
    }
    return 0;
}

void lvgl_wm_sync_geometry(lvgl_wm_t *wm, int id, int x, int y, int w, int h) {
    lvgl_wm_window_t *win;
    if (!wm) return;
    win = lvgl_wm_find_window(wm, id);
    if (!win) return;
    win->rect.x = x;
    win->rect.y = y;
    win->rect.w = w;
    win->rect.h = h;
    clamp_rect(wm, &win->rect);
    if (win->obj) {
        lv_obj_set_pos(win->obj, win->rect.x, win->rect.y);
        lv_obj_set_size(win->obj, win->rect.w, win->rect.h);
    }
}

int lvgl_wm_set_focus(lvgl_wm_t *wm, int id) {
    int i, prev, focused_index = -1;
    if (!wm) return 0;
    prev = wm->focused_id;
    for (i = 0; i < wm->count; i++) {
        wm->windows[i].focused = (wm->windows[i].id == id) ? 1 : 0;
        if (wm->windows[i].id == id) focused_index = i;
    }
    wm->focused_id = (lvgl_wm_find_window(wm, id) != 0) ? id : 0;
    if (focused_index >= 0 && focused_index + 1 < wm->count) {
        lvgl_wm_window_t focused = wm->windows[focused_index];
        for (i = focused_index; i + 1 < wm->count; i++)
            wm->windows[i] = wm->windows[i + 1];
        wm->windows[wm->count - 1] = focused;
        if (focused.obj) lv_obj_move_foreground(focused.obj);
    }
    return prev;
}

int lvgl_wm_window_count(lvgl_wm_t *wm) {
    return wm ? wm->count : 0;
}

lvgl_wm_window_t *lvgl_wm_window_at(lvgl_wm_t *wm, int index) {
    if (!wm || index < 0 || index >= wm->count) return 0;
    return &wm->windows[index];
}

int lvgl_wm_focused_id(lvgl_wm_t *wm) {
    return wm ? wm->focused_id : 0;
}