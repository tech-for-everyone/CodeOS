/* SDL3 Window Manager for CodeOS
 * Placement-only window manager: assigns cascade positions, tracks
 * window rectangles and focus, and answers hit-tests. Uses SDL3 types
 * and header-only math so no libSDL3 link is required in the kernel. */

#ifndef SDL3_WM_H
#define SDL3_WM_H

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL3_WM_MAX_WINDOWS 32
#define SDL3_WM_TITLEBAR_H 30
#define SDL3_WM_TITLE_MAX 127

typedef struct sdl3_wm_window {
    int id;
    char title[SDL3_WM_TITLE_MAX + 1];
    SDL_Rect rect;
    bool visible;
    bool focused;
} sdl3_wm_window_t;

typedef struct sdl3_wm {
    int screen_w;
    int screen_h;
    int titlebar_h;
    int cascade_x;
    int cascade_y;
    int cascade_step;
    int count;
    int next_id;
    int focused_id;
    sdl3_wm_window_t windows[SDL3_WM_MAX_WINDOWS];
} sdl3_wm_t;

/* Initialize the WM for a screen size. titlebar_h is used for the
 * cascade step so each new window clears the previous one's title bar. */
void sdl3_wm_init(sdl3_wm_t *wm, int screen_w, int screen_h, int titlebar_h);

/* Forget all tracked windows (safe on an initialized wm). */
void sdl3_wm_quit(sdl3_wm_t *wm);
void sdl3_wm_reset(sdl3_wm_t *wm);

/* Create a window with the given size, place it (cascade), and write the
 * chosen position into out_rect. Returns a wm id (0 on failure). */
int sdl3_wm_create_window(sdl3_wm_t *wm, const char *title, int w, int h,
                          SDL_Rect *out_rect);

/* Remove a tracked window (by id returned from sdl3_wm_create_window). */
void sdl3_wm_destroy_window(sdl3_wm_t *wm, int id);

/* Find a window record by id (null if not found). */
sdl3_wm_window_t *sdl3_wm_find_window(sdl3_wm_t *wm, int id);

/* Return the id of the topmost window containing (x, y), 0 if none. */
int sdl3_wm_hit_test(sdl3_wm_t *wm, int x, int y);

/* Keep the WM's copy of a window geometry in sync (e.g. after a drag). */
void sdl3_wm_sync_geometry(sdl3_wm_t *wm, int id, int x, int y, int w, int h);

/* Raise focus to a window; returns its previous focused id. */
int sdl3_wm_set_focus(sdl3_wm_t *wm, int id);

/* Iteration */
int sdl3_wm_window_count(sdl3_wm_t *wm);
sdl3_wm_window_t *sdl3_wm_window_at(sdl3_wm_t *wm, int index);

#ifdef __cplusplus
}
#endif

#endif /* SDL3_WM_H */
