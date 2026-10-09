/* CodeOS Desktop - Window Manager Implementation
 * Modern window manager with animations and compositor */

#include "codeos_wm.h"
#include "codeos_desktop.h"
#include "codeos_theme.h"
#include <stdint.h>
#include <string.h>

#define MAX_WINDOWS 64
#define MIN_WINDOW_W 200
#define MIN_WINDOW_H 150
#define WINDOW_SHADOW_SIZE 8
#define WINDOW_CORNER_RADIUS 12

static wm_window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int focused_id = -1;
static int next_window_id = 1;

/* Drag state */
static int drag_window_id = -1;
static int drag_offset_x, drag_offset_y;
static int dragging = 0;

/* Resize state */
static int resize_window_id = -1;
static int resize_edge = 0;
static int resize_start_x, resize_start_y;
static int resize_start_w, resize_start_h;

int wm_init(void) {
    window_count = 0;
    focused_id = -1;
    next_window_id = 1;
    memset(windows, 0, sizeof(windows));
    return 1;
}

void wm_shutdown(void) {
    window_count = 0;
    focused_id = -1;
}

int wm_create_window(int x, int y, int w, int h, const char *title, uint32_t bg) {
    if (window_count >= MAX_WINDOWS) return -1;
    if (w < MIN_WINDOW_W) w = MIN_WINDOW_W;
    if (h < MIN_WINDOW_H) h = MIN_WINDOW_H;

    wm_window_t *win = &windows[window_count];
    memset(win, 0, sizeof(wm_window_t));

    win->id = next_window_id++;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->prev_x = x;
    win->prev_y = y;
    win->prev_w = w;
    win->prev_h = h;
    win->state = WIN_STATE_NORMAL;
    win->focused = 0;
    win->has_shadow = 1;
    win->has_blur = 0;
    win->bg_color = bg;
    win->border_color = 0x585B70;
    win->dirty = 1;

    if (title) {
        strncpy(win->title, title, sizeof(win->title) - 1);
        win->title[sizeof(win->title) - 1] = 0;
    }

    /* Start open animation */
    win->anim_type = WIN_ANIM_OPEN;
    win->anim_progress = 0;
    win->anim_duration_ms = ANIM_WINDOW_OPEN;

    window_count++;
    focused_id = win->id;
    win->focused = 1;

    return win->id;
}

void wm_destroy_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            /* Start close animation */
            windows[i].anim_type = WIN_ANIM_CLOSE;
            windows[i].anim_progress = 0;
            windows[i].anim_duration_ms = ANIM_WINDOW_CLOSE;
            windows[i].state = WIN_STATE_CLOSED;
            return;
        }
    }
}

void wm_set_window_title(int id, const char *title) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id && title) {
            strncpy(windows[i].title, title, sizeof(windows[i].title) - 1);
            windows[i].title[sizeof(windows[i].title) - 1] = 0;
            windows[i].dirty = 1;
            return;
        }
    }
}

void wm_move_window(int id, int x, int y) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            wm_window_t *win = &windows[i];
            win->prev_x = win->x;
            win->prev_y = win->y;
            win->x = x;
            win->y = y;
            win->anim_type = WIN_ANIM_MOVE;
            win->anim_progress = 0;
            win->anim_duration_ms = 200;
            win->dirty = 1;
            return;
        }
    }
}

void wm_resize_window(int id, int w, int h) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            wm_window_t *win = &windows[i];
            if (w < MIN_WINDOW_W) w = MIN_WINDOW_W;
            if (h < MIN_WINDOW_H) h = MIN_WINDOW_H;
            win->prev_w = win->w;
            win->prev_h = win->h;
            win->w = w;
            win->h = h;
            win->anim_type = WIN_ANIM_RESIZE;
            win->anim_progress = 0;
            win->anim_duration_ms = 200;
            win->dirty = 1;
            return;
        }
    }
}

void wm_minimize_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            wm_window_t *win = &windows[i];
            win->prev_x = win->x;
            win->prev_y = win->y;
            win->prev_w = win->w;
            win->prev_h = win->h;
            win->state = WIN_STATE_MINIMIZED;
            win->anim_type = WIN_ANIM_MINIMIZE;
            win->anim_progress = 0;
            win->anim_duration_ms = ANIM_WINDOW_MINIMIZE;
            win->dirty = 1;
            if (focused_id == id) {
                focused_id = -1;
                /* Focus next window */
                for (int j = window_count - 1; j >= 0; j--) {
                    if (windows[j].id != id && windows[j].state == WIN_STATE_NORMAL) {
                        focused_id = windows[j].id;
                        windows[j].focused = 1;
                        break;
                    }
                }
            }
            return;
        }
    }
}

void wm_maximize_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            wm_window_t *win = &windows[i];
            if (win->state == WIN_STATE_MAXIMIZED) {
                wm_restore_window(id);
                return;
            }
            win->prev_x = win->x;
            win->prev_y = win->y;
            win->prev_w = win->w;
            win->prev_h = win->h;
            /* TODO: get screen dimensions */
            win->x = 0;
            win->y = 32; /* Below menubar */
            win->w = 1920;
            win->h = 1080 - 32 - 80; /* Screen - menubar - dock */
            win->state = WIN_STATE_MAXIMIZED;
            win->anim_type = WIN_ANIM_MAXIMIZE;
            win->anim_progress = 0;
            win->anim_duration_ms = 250;
            win->dirty = 1;
            return;
        }
    }
}

void wm_restore_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            wm_window_t *win = &windows[i];
            win->x = win->prev_x;
            win->y = win->prev_y;
            win->w = win->prev_w;
            win->h = win->prev_h;
            win->state = WIN_STATE_NORMAL;
            win->anim_type = WIN_ANIM_RESTORE;
            win->anim_progress = 0;
            win->anim_duration_ms = 250;
            win->dirty = 1;
            return;
        }
    }
}

void wm_focus_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].focused = 1;
            windows[i].dirty = 1;
        } else if (windows[i].focused) {
            windows[i].focused = 0;
            windows[i].dirty = 1;
        }
    }
    focused_id = id;
}

void wm_unfocus_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].focused = 0;
            windows[i].dirty = 1;
            if (focused_id == id) focused_id = -1;
            return;
        }
    }
}

int wm_window_count(void) { return window_count; }

wm_window_t *wm_get_window(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) return &windows[i];
    }
    return 0;
}

int wm_focused_window(void) { return focused_id; }

int wm_hit_test(int x, int y) {
    /* Check windows in reverse order (top first) */
    for (int i = window_count - 1; i >= 0; i--) {
        wm_window_t *win = &windows[i];
        if (win->state == WIN_STATE_MINIMIZED || win->state == WIN_STATE_CLOSED) continue;
        if (x >= win->x && x < win->x + win->w && y >= win->y && y < win->y + win->h) {
            return win->id;
        }
    }
    return -1;
}

void wm_start_animation(int id, win_anim_type_t type, int duration_ms) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].anim_type = type;
            windows[i].anim_progress = 0;
            windows[i].anim_duration_ms = duration_ms;
            return;
        }
    }
}

void wm_update_animations(uint64_t now_ms) {
    for (int i = 0; i < window_count; i++) {
        wm_window_t *win = &windows[i];
        if (win->anim_type == WIN_ANIM_NONE) continue;
        if (win->anim_duration_ms <= 0) {
            win->anim_type = WIN_ANIM_NONE;
            win->anim_progress = 100;
            continue;
        }
        /* Simple linear progress */
        if (win->anim_progress < 100) {
            win->anim_progress += 100 / (win->anim_duration_ms / 16);
            if (win->anim_progress > 100) win->anim_progress = 100;
        }
        if (win->anim_progress >= 100) {
            win->anim_type = WIN_ANIM_NONE;
            /* Clean up closed windows */
            if (win->state == WIN_STATE_CLOSED) {
                /* Remove from list */
                for (int j = i; j < window_count - 1; j++) {
                    windows[j] = windows[j + 1];
                }
                window_count--;
                i--;
            }
        }
        win->dirty = 1;
    }
}

int wm_animation_active(int id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            return windows[i].anim_type != WIN_ANIM_NONE;
        }
    }
    return 0;
}

void wm_set_window_shadow(int id, int enable) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].has_shadow = enable;
            windows[i].dirty = 1;
            return;
        }
    }
}

void wm_set_window_blur(int id, int enable) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].has_blur = enable;
            windows[i].dirty = 1;
            return;
        }
    }
}

/* Drag operations */
void wm_begin_drag(int id, int mx, int my) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            drag_window_id = id;
            drag_offset_x = mx - windows[i].x;
            drag_offset_y = my - windows[i].y;
            dragging = 1;
            wm_focus_window(id);
            return;
        }
    }
}

void wm_update_drag(int mx, int my) {
    if (drag_window_id < 0) return;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == drag_window_id) {
            windows[i].x = mx - drag_offset_x;
            windows[i].y = my - drag_offset_y;
            windows[i].dirty = 1;
            return;
        }
    }
}

void wm_end_drag(void) {
    drag_window_id = -1;
    dragging = 0;
}

void wm_begin_resize(int id, int mx, int my, int edge) {
    resize_window_id = id;
    resize_edge = edge;
    resize_start_x = mx;
    resize_start_y = my;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            resize_start_w = windows[i].w;
            resize_start_h = windows[i].h;
            return;
        }
    }
}

void wm_update_resize(int mx, int my) {
    if (resize_window_id < 0) return;
    int dx = mx - resize_start_x;
    int dy = my - resize_start_y;

    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == resize_window_id) {
            wm_window_t *win = &windows[i];
            if (resize_edge & 1) { /* Right */
                win->w = resize_start_w + dx;
            }
            if (resize_edge & 2) { /* Bottom */
                win->h = resize_start_h + dy;
            }
            if (resize_edge & 4) { /* Left */
                win->x = win->prev_x + dx;
                win->w = resize_start_w - dx;
            }
            if (resize_edge & 8) { /* Top */
                win->y = win->prev_y + dy;
                win->h = resize_start_h - dy;
            }
            if (win->w < MIN_WINDOW_W) win->w = MIN_WINDOW_W;
            if (win->h < MIN_WINDOW_H) win->h = MIN_WINDOW_H;
            win->dirty = 1;
            return;
        }
    }
}

void wm_end_resize(void) {
    resize_window_id = -1;
    resize_edge = 0;
}
