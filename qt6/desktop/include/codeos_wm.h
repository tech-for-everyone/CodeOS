/* CodeOS Desktop - Window Manager
 * Manages windows with animations and compositor effects */

#ifndef CODEOS_WM_H
#define CODEOS_WM_H

#include <stdint.h>

/* Window states */
typedef enum {
    WIN_STATE_NORMAL = 0,
    WIN_STATE_MINIMIZED,
    WIN_STATE_MAXIMIZED,
    WIN_STATE_FULLSCREEN,
    WIN_STATE_CLOSED
} win_state_t;

/* Animation types */
typedef enum {
    WIN_ANIM_NONE = 0,
    WIN_ANIM_OPEN,
    WIN_ANIM_CLOSE,
    WIN_ANIM_MINIMIZE,
    WIN_ANIM_MAXIMIZE,
    WIN_ANIM_RESTORE,
    WIN_ANIM_MOVE,
    WIN_ANIM_RESIZE
} win_anim_type_t;

/* Window structure */
typedef struct {
    int id;
    int x, y, w, h;
    int prev_x, prev_y, prev_w, prev_h;
    char title[128];
    win_state_t state;
    int focused;
    int has_shadow;
    int has_blur;
    uint32_t bg_color;
    uint32_t border_color;

    /* Animation state */
    win_anim_type_t anim_type;
    int anim_progress;    /* 0-100 */
    int anim_duration_ms;
    uint64_t anim_start_ms;

    /* Content */
    void *buffer;
    int dirty;
} wm_window_t;

/* Window manager API */
int  wm_init(void);
void wm_shutdown(void);

/* Window operations */
int  wm_create_window(int x, int y, int w, int h, const char *title, uint32_t bg);
void wm_destroy_window(int id);
void wm_set_window_title(int id, const char *title);
void wm_move_window(int id, int x, int y);
void wm_resize_window(int id, int w, int h);
void wm_minimize_window(int id);
void wm_maximize_window(int id);
void wm_restore_window(int id);
void wm_focus_window(int id);
void wm_unfocus_window(int id);

/* Window queries */
int  wm_window_count(void);
wm_window_t *wm_get_window(int id);
int  wm_focused_window(void);
int  wm_hit_test(int x, int y);

/* Animation */
void wm_start_animation(int id, win_anim_type_t type, int duration_ms);
void wm_update_animations(uint64_t now_ms);
int  wm_animation_active(int id);

/* Compositor effects */
void wm_set_window_shadow(int id, int enable);
void wm_set_window_blur(int id, int enable);

/* Drag/resize */
void wm_begin_drag(int id, int mx, int my);
void wm_update_drag(int mx, int my);
void wm_end_drag(void);
void wm_begin_resize(int id, int mx, int my, int edge);
void wm_update_resize(int mx, int my);
void wm_end_resize(void);

#endif /* CODEOS_WM_H */
