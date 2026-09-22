#ifndef LVGL_PANELS_H
#define LVGL_PANELS_H

#include <stdint.h>

typedef struct _lv_obj_t lv_obj_t;

/* ─────────────────────────────────────────────────────────────────────
 * Panels - Tiling DE for CodeOS (Cosmic-inspired)
 * ───────────────────────────────────────────────────────────────────── */

#define PANELS_MAX_WORKSPACES    9
#define PANELS_MAX_WINDOWS       64
#define PANELS_MAX_CONTAINERS    32
#define PANELS_BAR_H             32
#define PANELS_DOCK_H            64
#define PANELS_GAP               8
#define PANELS_BORDER_W          2

/* Split direction */
typedef enum {
    PANELS_SPLIT_HORIZONTAL = 0,
    PANELS_SPLIT_VERTICAL   = 1
} panels_split_t;

/* Container types */
typedef enum {
    PANELS_CONTAINER_LEAF   = 0,
    PANELS_CONTAINER_SPLIT  = 1
} panels_container_type_t;

/* Window states */
typedef enum {
    PANELS_WIN_NORMAL = 0,
    PANELS_WIN_FLOATING = 1,
    PANELS_WIN_FULLSCREEN = 2
} panels_win_state_t;

/* Forward declarations */
struct panels_container;
struct panels_window;
struct panels_workspace;
struct panels_de;

/* Window structure */
typedef struct panels_window {
    int id;
    char title[128];
    lv_obj_t *lvgl_obj;
    lv_obj_t *decor_obj;
    lv_obj_t *title_label;
    panels_win_state_t state;
    struct panels_workspace *workspace;
    struct panels_container *container;
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    int focused;
    int urgent;
} panels_window_t;

/* Container structure (binary tree for splits) */
typedef struct panels_container {
    panels_container_type_t type;
    panels_split_t split_dir;
    float split_ratio;
    struct panels_container *parent;
    struct panels_container *child_a;
    struct panels_container *child_b;
    panels_window_t *window;
    struct panels_workspace *workspace;
    int focused;
} panels_container_t;

/* Workspace structure */
typedef struct panels_workspace {
    int id;
    char name[32];
    panels_container_t *root;
    panels_container_t *focused_container;
    int active;
} panels_workspace_t;

/* DE main structure */
typedef struct panels_de {
    panels_workspace_t workspaces[PANELS_MAX_WORKSPACES];
    int current_workspace;
    int workspace_count;
    
    lv_obj_t *bar;
    lv_obj_t *workspace_buttons[PANELS_MAX_WORKSPACES];
    lv_obj_t *clock_label;
    lv_obj_t *launcher_button;
    lv_obj_t *systray;
    
    lv_obj_t *dock;
    lv_obj_t *dock_items[PANELS_MAX_WINDOWS];
    int dock_count;
    int dock_pinned[PANELS_MAX_WINDOWS];
    int dock_pinned_count;
    
    int launcher_open;
    lv_obj_t *launcher_scr;
    lv_obj_t *launcher_panel;
    lv_obj_t *launcher_search;
    lv_obj_t *launcher_grid;
    
    lv_obj_t *toast_container;
    
    /* Launcher callback (for Qt compatibility) */
    void (*launcher_cb)(int index, void *userdata);
    void *launcher_userdata;
    const char **launcher_names;
    int launcher_name_count;
    
    int mod_pressed;
    int shift_pressed;
    int ctrl_pressed;
    int alt_pressed;
    
    panels_window_t windows[PANELS_MAX_WINDOWS];
    int window_count;
    int next_window_id;
    
    panels_container_t containers[PANELS_MAX_CONTAINERS];
    int container_count;
    int next_container_id;
    
    panels_window_t *focused_window;
    panels_container_t *focused_container;
    panels_workspace_t *focused_workspace;
    
    int bar_created;
    int dock_created;
    
    int screen_w, screen_h;
} panels_de_t;

/* ─────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────── */

void panels_de_init(panels_de_t *de, int screen_w, int screen_h);
void panels_de_create_ui(panels_de_t *de);
void panels_de_quit(panels_de_t *de);
void panels_de_pump(panels_de_t *de);  /* Call each frame */

/* Window management */
panels_window_t *panels_de_create_window(panels_de_t *de, const char *title, int w, int h);
void panels_de_destroy_window(panels_de_t *de, panels_window_t *win);
void panels_de_focus_window(panels_de_t *de, panels_window_t *win);
void panels_de_set_window_state(panels_de_t *de, panels_window_t *win, panels_win_state_t state);
void panels_de_toggle_floating(panels_de_t *de, panels_window_t *win);
void panels_de_toggle_fullscreen(panels_de_t *de, panels_window_t *win);

/* Tiling operations */
void panels_de_split_container(panels_de_t *de, panels_container_t *c, panels_split_t dir, float ratio);
void panels_de_resize_split(panels_de_t *de, panels_container_t *c, float ratio);
void panels_de_move_window_to_container(panels_de_t *de, panels_window_t *win, panels_container_t *dest);
void panels_de_balance_windows(panels_de_t *de, panels_workspace_t *ws);

/* Workspace management */
void panels_de_switch_workspace(panels_de_t *de, int index);
void panels_de_move_window_to_workspace(panels_de_t *de, panels_window_t *win, int ws_idx);

/* Dock operations */
void panels_de_dock_add_pinned(panels_de_t *de, const char *app_id, const char *name, const char *icon_name);
void panels_de_dock_remove_pinned(panels_de_t *de, const char *app_id);
void panels_de_dock_update_running(panels_de_t *de, panels_window_t *win, int running);

/* Input handling */
void panels_de_handle_key(panels_de_t *de, int key, int pressed);
void panels_de_handle_mouse(panels_de_t *de, int x, int y, int buttons);

/* Launcher */
void panels_de_toggle_launcher(panels_de_t *de);
void panels_de_close_launcher(panels_de_t *de);

/* Notifications */
void panels_de_show_toast(panels_de_t *de, const char *title, const char *message, int timeout_ms);

/* Accessors */
panels_workspace_t *panels_de_get_workspace(panels_de_t *de, int index);
panels_workspace_t *panels_de_get_current_workspace(panels_de_t *de);
int panels_de_get_workspace_index(panels_de_t *de, panels_workspace_t *ws);

/* Global instance getter */
panels_de_t *panels_de_get_instance(void);

/* Launcher with callback support (Qt compatibility) */
int panels_de_open_launcher(panels_de_t *de, const char *const *names, int count,
                             void (*cb)(int index, void *userdata), void *userdata);
void panels_de_close_launcher_with_data(panels_de_t *de);

/* WM compatibility layer (replaces lvgl_wm) */
int panels_wm_init(panels_de_t *de, int screen_w, int screen_h);
void panels_wm_quit(panels_de_t *de);
int panels_wm_create_window(panels_de_t *de, const char *title);
void panels_wm_destroy_window(panels_de_t *de, int win_id);

#endif