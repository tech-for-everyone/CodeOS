#ifndef WM_H
#define WM_H

#include <stdint.h>

#define WM_MAX_WINDOWS      32
#define WM_MAX_WORKSPACES   9
#define WM_ANIM_DURATION    200

enum wm_layout {
    WM_LAYOUT_MASTER_STACK,
    WM_LAYOUT_GRID,
    WM_LAYOUT_MONOCLE,
    WM_LAYOUT_FLOATING,
};

enum wm_anim_type {
    WM_ANIM_NONE,
    WM_ANIM_OPEN,
    WM_ANIM_CLOSE,
    WM_ANIM_MOVE,
    WM_ANIM_WORKSPACE,
    WM_ANIM_FOCUS,
    WM_ANIM_MINIMIZE,
};

typedef struct {
    int active;
    int win_idx;
    enum wm_anim_type type;
    uint64_t start_ms;
    uint64_t duration_ms;
    int progress;
    int x_from, y_from, x_to, y_to;
    int w_from, w_to, h_from, h_to;
} wm_anim_t;

void wm_init(void);
void wm_load_config(const char *path);
void wm_handle_key(int key);
void wm_apply_layout(void);
int  wm_active(void);
int  wm_active_workspace(void);
int  wm_workspace_count(void);
int  wm_border_width(void);
uint32_t wm_border_focus(void);
uint32_t wm_border_normal(void);
int  wm_window_floating(int idx);
void wm_window_set_floating(int idx, int floating);
void wm_window_toggle_floating(int idx);
void wm_anim_start(int win_idx, enum wm_anim_type type, uint64_t duration_ms);
void wm_anim_update(uint64_t now_ms);
int  wm_anim_get_pos(int win_idx, int *x, int *y, int *w, int *h, int *opacity);
int  wm_get_gaps(void);
int  wm_get_master_factor(void);
void wm_set_master_factor(int factor);
enum wm_layout wm_get_layout(int ws);
void wm_set_layout(int ws, int layout);
void wm_cycle_layout(void);
int  wm_dock_enabled(void);
int  wm_menubar_enabled(void);
int  wm_menubar_clock(void);
const char *wm_dock_icons(void);

#endif
