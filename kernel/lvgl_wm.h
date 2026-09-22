#ifndef LVGL_WM_H
#define LVGL_WM_H

#include <stdint.h>
typedef struct _lv_obj_t lv_obj_t;

/* LVGL Window Manager for CodeOS — replaces the SDL3-based WM.
 *
 * Placement-only window manager: assigns cascade positions, tracks window
 * rectangles and focus, and answers hit-tests. Every tracked window is a
 * real LVGL object on the LVGL screen, so the WM is LVGL-backed end to end.
 * The Qt desktop renders the surface; LVGL supplies the window model.
 */

#define LVGL_WM_MAX_WINDOWS 32
#define LVGL_WM_TITLEBAR_H 30
#define LVGL_WM_TITLE_MAX 127

typedef struct _lv_obj_t lv_obj_t;

typedef struct {
    int x, y, w, h;
} lvgl_wm_rect_t;

typedef struct lvgl_wm_window {
    int id;
    char title[LVGL_WM_TITLE_MAX + 1];
    lvgl_wm_rect_t rect;
    lv_obj_t *obj;
    int visible;
    int focused;
} lvgl_wm_window_t;

typedef struct lvgl_wm {
    int screen_w;
    int screen_h;
    int titlebar_h;
    int cascade_x;
    int cascade_y;
    int cascade_step;
    int count;
    int next_id;
    int focused_id;
    lv_obj_t *screen;
    lvgl_wm_window_t windows[LVGL_WM_MAX_WINDOWS];
} lvgl_wm_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the WM for a screen size. titlebar_h is used for the
 * cascade step so each new window clears the previous one's title bar.
 * Also initializes the LVGL port (display + input drivers). */
void lvgl_wm_init(lvgl_wm_t *wm, int screen_w, int screen_h, int titlebar_h);

/* Forget all tracked windows (safe on an initialized wm). */
void lvgl_wm_quit(lvgl_wm_t *wm);
void lvgl_wm_reset(lvgl_wm_t *wm);

/* Create a window with the given size, place it (cascade), and write the
 * chosen position into out_rect. Returns a wm id (0 on failure). */
int lvgl_wm_create_window(lvgl_wm_t *wm, const char *title, int w, int h,
                          lvgl_wm_rect_t *out_rect);

/* Remove a tracked window (by id returned from lvgl_wm_create_window). */
void lvgl_wm_destroy_window(lvgl_wm_t *wm, int id);

/* Find a window record by id (null if not found). */
lvgl_wm_window_t *lvgl_wm_find_window(lvgl_wm_t *wm, int id);

/* Return the id of the topmost window containing (x, y), 0 if none. */
int lvgl_wm_hit_test(lvgl_wm_t *wm, int x, int y);

/* Keep the WM's copy of a window geometry in sync (e.g. after a drag). */
void lvgl_wm_sync_geometry(lvgl_wm_t *wm, int id, int x, int y, int w, int h);

/* Raise focus to a window; returns its previous focused id. */
int lvgl_wm_set_focus(lvgl_wm_t *wm, int id);

/* Iteration */
int lvgl_wm_window_count(lvgl_wm_t *wm);
lvgl_wm_window_t *lvgl_wm_window_at(lvgl_wm_t *wm, int index);

/* Return the id of the currently focused window, 0 if none. */
int lvgl_wm_focused_id(lvgl_wm_t *wm);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_WM_H */