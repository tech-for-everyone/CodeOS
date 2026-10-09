/* SDL3 Window Manager - placement implementation.
 * Header-only SDL3 usage: no libSDL3.a dependency. */

#include "sdl3_wm.h"
#include <string.h>

#define WM_MARGIN 8
#define WM_START_X 24
#define WM_START_Y 24
#define WM_CASCADE_STEP 36

static void cascade_advance(sdl3_wm_t *wm) {
    if (!wm) return;
    wm->cascade_x += wm->cascade_step;
    wm->cascade_y += wm->cascade_step;
    if (wm->cascade_x > wm->screen_w / 2) {
        wm->cascade_x = WM_START_X;
        wm->cascade_y += wm->cascade_step;
    }
    if (wm->cascade_y > wm->screen_h - WM_START_Y - 96) {
        wm->cascade_y = WM_START_Y;
        wm->cascade_x = WM_START_X;
    }
}

void sdl3_wm_init(sdl3_wm_t *wm, int screen_w, int screen_h, int titlebar_h) {
    if (!wm) return;
    wm->screen_w = screen_w > 0 ? screen_w : 1280;
    wm->screen_h = screen_h > 0 ? screen_h : 800;
    wm->titlebar_h = titlebar_h > 0 ? titlebar_h : SDL3_WM_TITLEBAR_H;
    wm->cascade_step = wm->titlebar_h + WM_MARGIN;
    sdl3_wm_reset(wm);
}

void sdl3_wm_reset(sdl3_wm_t *wm) {
    int i;
    if (!wm) return;
    wm->count = 0;
    wm->next_id = 1;
    wm->focused_id = 0;
    wm->cascade_x = WM_START_X;
    wm->cascade_y = WM_START_Y;
    for (i = 0; i < SDL3_WM_MAX_WINDOWS; i++) {
        wm->windows[i].id = 0;
        wm->windows[i].visible = false;
        wm->windows[i].focused = false;
        wm->windows[i].rect.x = 0;
        wm->windows[i].rect.y = 0;
        wm->windows[i].rect.w = 0;
        wm->windows[i].rect.h = 0;
        wm->windows[i].title[0] = 0;
    }
}

void sdl3_wm_quit(sdl3_wm_t *wm) {
    if (wm) sdl3_wm_reset(wm);
}

int sdl3_wm_create_window(sdl3_wm_t *wm, const char *title, int w, int h,
                          SDL_Rect *out_rect) {
    sdl3_wm_window_t *win;
    int i;
    if (!wm || wm->count >= SDL3_WM_MAX_WINDOWS) return 0;

    win = &wm->windows[wm->count];
    win->id = wm->next_id++;
    win->visible = true;
    win->focused = false;

    if (title) {
        for (i = 0; title[i] && i < SDL3_WM_TITLE_MAX; i++)
            win->title[i] = title[i];
        win->title[i] = 0;
    } else {
        win->title[0] = 0;
    }

    if (w <= 0) w = 400;
    if (h <= 0) h = 300;
    if (w > wm->screen_w - WM_MARGIN * 2) w = wm->screen_w - WM_MARGIN * 2;
    if (h > wm->screen_h - WM_MARGIN * 2) h = wm->screen_h - WM_MARGIN * 2;

    win->rect.w = w;
    win->rect.h = h;
    win->rect.x = wm->cascade_x;
    win->rect.y = wm->cascade_y;

    if (win->rect.x + w > wm->screen_w - WM_MARGIN)
        win->rect.x = wm->screen_w - w - WM_MARGIN;
    if (win->rect.y + h > wm->screen_h - WM_MARGIN)
        win->rect.y = wm->screen_h - h - WM_MARGIN;
    if (win->rect.x < 0) win->rect.x = 0;
    if (win->rect.y < 0) win->rect.y = 0;

    cascade_advance(wm);
    wm->count++;

    if (out_rect) *out_rect = win->rect;
    return win->id;
}

void sdl3_wm_destroy_window(sdl3_wm_t *wm, int id) {
    int i;
    if (!wm || id <= 0) return;
    for (i = 0; i < wm->count; i++) {
        if (wm->windows[i].id == id) {
            wm->windows[i] = wm->windows[wm->count - 1];
            wm->windows[wm->count - 1].id = 0;
            wm->windows[wm->count - 1].visible = false;
            wm->count--;
            if (wm->focused_id == id) wm->focused_id = 0;
            return;
        }
    }
}

sdl3_wm_window_t *sdl3_wm_find_window(sdl3_wm_t *wm, int id) {
    int i;
    if (!wm || id <= 0) return NULL;
    for (i = 0; i < wm->count; i++) {
        if (wm->windows[i].id == id) return &wm->windows[i];
    }
    return NULL;
}

int sdl3_wm_hit_test(sdl3_wm_t *wm, int x, int y) {
    int i;
    if (!wm) return 0;
    for (i = wm->count - 1; i >= 0; i--) {
        const sdl3_wm_window_t *win = &wm->windows[i];
        if (!win->visible) continue;
        if (x >= win->rect.x && x < win->rect.x + win->rect.w &&
            y >= win->rect.y && y < win->rect.y + win->rect.h)
            return win->id;
    }
    return 0;
}

void sdl3_wm_sync_geometry(sdl3_wm_t *wm, int id, int x, int y, int w, int h) {
    sdl3_wm_window_t *win;
    if (!wm) return;
    win = sdl3_wm_find_window(wm, id);
    if (!win) return;
    win->rect.x = x;
    win->rect.y = y;
    win->rect.w = w;
    win->rect.h = h;
}

int sdl3_wm_set_focus(sdl3_wm_t *wm, int id) {
    int i, prev;
    if (!wm) return 0;
    prev = wm->focused_id;
    for (i = 0; i < wm->count; i++) {
        wm->windows[i].focused = (wm->windows[i].id == id) ? true : false;
    }
    wm->focused_id = (sdl3_wm_find_window(wm, id) != NULL) ? id : 0;
    return prev;
}

int sdl3_wm_window_count(sdl3_wm_t *wm) {
    return wm ? wm->count : 0;
}

sdl3_wm_window_t *sdl3_wm_window_at(sdl3_wm_t *wm, int index) {
    if (!wm || index < 0 || index >= wm->count) return NULL;
    return &wm->windows[index];
}
