#include "lvgl_panels.h"
#include "lvgl/lvgl.h"
#include "lvgl_port.h"
#include "kprintf.h"
#include "string.h"
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────
 * Static state
 * ───────────────────────────────────────────────────────────────────── */

static panels_de_t *g_de = NULL;

/* Toast contexts */
typedef struct {
    panels_de_t *de;
    lv_obj_t *toast;
    int timeout_ms;
    uint32_t start_time;
} toast_ctx_t;

static toast_ctx_t toast_contexts[PANELS_MAX_WINDOWS];

/* App catalogue for the launcher */
#define DEFAULT_APP_COUNT 8

typedef struct {
    const char *name;
    const char *icon_char;
    uint32_t color;
    const char *cmd;
} app_entry_t;

static const app_entry_t default_app_catalog[DEFAULT_APP_COUNT] = {
    {"Terminal",    "\xef\x81\x92", 0xFF34C759, "terminal"},
    {"File Manager", "\xef\x81\xbb", 0xFF007AFF, "files"},
    {"Settings",     "\xef\x80\x93", 0xFF8E8E93, "settings"},
    {"Web",          "\xef\x80\x88", 0xFFFF2D55, "web"},
    {"Editor",       "\xef\x81\x84", 0xFF5E5CE6, "editor"},
    {"Mail",         "\xef\x80\x93", 0xFFFF9500, "mail"},
    {"Calendar",     "\xef\x81\xb7", 0xFFFF9500, "calendar"},
    {"Calculator",   "\xef\x81\xa9", 0xFFFF9500, "calc"},
};

/* ─────────────────────────────────────────────────────────────────────
 * Container management
 * ───────────────────────────────────────────────────────────────────── */

static void panels_container_reset(panels_container_t *c) {
    memset(c, 0, sizeof(panels_container_t));
    c->split_ratio = 0.5f;
}

static panels_container_t *panels_container_alloc(panels_de_t *de) {
    if (de->container_count >= PANELS_MAX_CONTAINERS) return NULL;
    panels_container_t *c = &de->containers[de->container_count++];
    panels_container_reset(c);
    c->workspace = panels_de_get_current_workspace(de);
    return c;
}

static panels_container_t *panels_container_create_leaf(panels_de_t *de, panels_window_t *win) {
    panels_container_t *c = panels_container_alloc(de);
    if (!c) return NULL;
    c->type = PANELS_CONTAINER_LEAF;
    c->window = win;
    win->container = c;
    return c;
}

static panels_container_t *panels_container_create_split(panels_de_t *de, panels_window_t *win_a, panels_window_t *win_b, panels_split_t dir) {
    panels_container_t *c = panels_container_alloc(de);
    if (!c) return NULL;
    c->type = PANELS_CONTAINER_SPLIT;
    c->split_dir = dir;
    c->split_ratio = 0.5f;
    
    panels_container_t *leaf_a = panels_container_create_leaf(de, win_a);
    panels_container_t *leaf_b = panels_container_create_leaf(de, win_b);
    
    if (!leaf_a || !leaf_b) return NULL;
    
    c->child_a = leaf_a;
    c->child_b = leaf_b;
    leaf_a->parent = c;
    leaf_b->parent = c;
    
    return c;
}

static int panels_container_count_windows(panels_container_t *c) {
    (void)c;
    return 0;
}

static void panels_container_find_focus(panels_container_t *c, panels_container_t **out) {
    (void)c; (void)out;
    /* Not currently used - kept for future navigation support */
}

static void panels_container_set_focus(panels_container_t *c, int focused) {
    if (!c) return;
    c->focused = focused;
    if (c->type == PANELS_CONTAINER_SPLIT) {
        panels_container_set_focus(c->child_a, focused);
        panels_container_set_focus(c->child_b, focused);
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * Layout engine
 * ───────────────────────────────────────────────────────────────────── */

static void panels_de_layout_container(panels_de_t *de, panels_container_t *c, int x, int y, int w, int h);

static void panels_de_layout_leaf(panels_de_t *de, panels_container_t *c, int x, int y, int w, int h) {
    if (c->type != PANELS_CONTAINER_LEAF || !c->window) return;
    
    panels_window_t *win = c->window;
    
    if (win->state == PANELS_WIN_FULLSCREEN) {
        lv_obj_set_pos(win->lvgl_obj, 0, PANELS_BAR_H);
        lv_obj_set_size(win->lvgl_obj, de->screen_w, de->screen_h - PANELS_BAR_H);
        if (win->decor_obj) lv_obj_add_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    
    if (win->state == PANELS_WIN_FLOATING) {
        lv_obj_set_pos(win->lvgl_obj, win->x, win->y);
        lv_obj_set_size(win->lvgl_obj, win->w, win->h);
        return;
    }
    
    /* Tiled window: apply gap */
    int gap = PANELS_GAP;
    int ix = x + gap/2;
    int iy = y + PANELS_BAR_H + gap/2;
    int iw = w - gap;
    int ih = h - PANELS_BAR_H - PANELS_DOCK_H - gap;
    
    if (iw < 100) iw = 100;
    if (ih < 60) ih = 60;
    
    lv_obj_set_pos(win->lvgl_obj, ix, iy);
    lv_obj_set_size(win->lvgl_obj, iw, ih);
    
    if (win->decor_obj) {
        lv_obj_set_pos(win->decor_obj, ix, iy - 24);
        lv_obj_set_size(win->decor_obj, iw, 24);
        if (c->focused) {
            lv_obj_clear_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(win->decor_obj, lv_color_hex(0xFF2D2D30), 0);
        } else {
            lv_obj_clear_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(win->decor_obj, lv_color_hex(0xFF1E1E20), 0);
        }
    }
}

static void panels_de_layout_split(panels_de_t *de, panels_container_t *c, int x, int y, int w, int h) {
    if (c->type != PANELS_CONTAINER_SPLIT) return;
    if (c->split_dir == PANELS_SPLIT_HORIZONTAL) {
        int split_x = x + (int)(w * c->split_ratio);
        panels_de_layout_container(de, c->child_a, x, y, split_x - x, h);
        panels_de_layout_container(de, c->child_b, split_x, y, w - (split_x - x), h);
    } else {
        int split_y = y + (int)(h * c->split_ratio);
        panels_de_layout_container(de, c->child_a, x, y, w, split_y - y);
        panels_de_layout_container(de, c->child_b, x, split_y, w, h - (split_y - y));
    }
}

static void panels_de_layout_container(panels_de_t *de, panels_container_t *c, int x, int y, int w, int h) {
    if (!c) return;
    if (c->type == PANELS_CONTAINER_LEAF) {
        panels_de_layout_leaf(de, c, x, y, w, h);
    } else {
        panels_de_layout_split(de, c, x, y, w, h);
    }
}

static void panels_de_arrange_workspace(panels_de_t *de, panels_workspace_t *ws) {
    if (!ws || !ws->root) return;
    panels_de_layout_container(de, ws->root, 0, 0, de->screen_w, de->screen_h);
}

/* ─────────────────────────────────────────────────────────────────────
 * Focus management
 */

static void panels_de_focus_container(panels_de_t *de, panels_container_t *c) {
    if (!c) return;
    
    /* Clear old focus */
    panels_container_set_focus(de->focused_container, 0);
    
    de->focused_container = c;
    panels_container_set_focus(c, 1);
    
    if (c->type == PANELS_CONTAINER_LEAF && c->window) {
        de->focused_window = c->window;
        if (c->window->decor_obj) {
            lv_obj_clear_flag(c->window->decor_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(c->window->decor_obj, lv_color_hex(0xFF2D2D30), 0);
        }
    }
}

static void panels_de_focus_direction(panels_de_t *de, panels_split_t dir) {
    if (!de->focused_container) return;
    
    panels_container_t *c = de->focused_container;
    
    /* Walk up to find parent split */
    while (c && c->parent && c->parent->split_dir != dir) {
        c = c->parent;
    }
    
    if (!c || !c->parent) {
        /* Try to find a split with matching direction */
        return;
    }
    
    panels_container_t *parent = c->parent;
    panels_container_t *target = (c == parent->child_a) ? parent->child_b : parent->child_a;
    if (!target) return;
    
    /* Find deepest leaf in target */
    while (target->type == PANELS_CONTAINER_SPLIT) {
        if (target->child_a->focused) target = target->child_a;
        else target = target->child_b;
    }
    
    panels_de_focus_container(de, target);
    panels_de_arrange_workspace(de, de->focused_container ? de->focused_container->workspace : NULL);
}

/* ─────────────────────────────────────────────────────────────────────
 * Window management
 */

static void panels_de_update_dock(panels_de_t *de) {
    (void)de;
}

panels_window_t *panels_de_create_window(panels_de_t *de, const char *title, int w, int h) {
    /* Create bar/dock if not yet created */
    if (!de->bar) {
        panels_de_create_ui(de);
    }

    panels_window_t *win = NULL;
    for (int i = 0; i < PANELS_MAX_WINDOWS; i++) {
        if (de->windows[i].id == 0) {
            win = &de->windows[i];
            break;
        }
    }
    if (!win) return NULL;
    
    memset(win, 0, sizeof(panels_window_t));
    win->id = ++de->next_window_id;
    if (title) strncpy_safe(win->title, title, sizeof(win->title)-1);
    win->state = PANELS_WIN_NORMAL;
    win->x = 100; win->y = 100;
    win->w = w; win->h = h;
    
    /* Create LVGL objects on the screen */
    lv_obj_t *scr = lv_scr_act();
    
    /* Window content */
    win->lvgl_obj = lv_obj_create(scr);
    lv_obj_set_size(win->lvgl_obj, w, h);
    lv_obj_set_pos(win->lvgl_obj, win->x, win->y);
    lv_obj_set_style_radius(win->lvgl_obj, 4, 0);
    lv_obj_set_style_bg_color(win->lvgl_obj, lv_color_hex(0xFF252526), 0);
    lv_obj_set_style_border_width(win->lvgl_obj, 0, 0);
    
    lv_obj_t *label = lv_label_create(win->lvgl_obj);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);
    
    /* Decoration */
    win->decor_obj = lv_obj_create(scr);
    lv_obj_set_size(win->decor_obj, w, 24);
    lv_obj_set_style_radius(win->decor_obj, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(win->decor_obj, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(win->decor_obj, lv_color_hex(0xFF1E1E20), 0);
    lv_obj_set_style_border_width(win->decor_obj, 0, 0);
    lv_obj_set_style_pad_all(win->decor_obj, 8, 0);
    win->title_label = lv_label_create(win->decor_obj);
    lv_label_set_text(win->title_label, title);
    lv_obj_set_style_text_color(win->title_label, lv_color_hex(0xFFFFFFFF), 0);
    lv_obj_align(win->title_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
    
    /* Assign to workspace */
    panels_workspace_t *ws = panels_de_get_current_workspace(de);
    win->workspace = ws;
    
    /* Add to tiling tree */
    if (!ws->root) {
        ws->root = panels_container_create_leaf(de, win);
    } else {
        panels_container_t *focus = ws->focused_container;
        if (!focus) focus = ws->root;
        panels_container_t *new_split = panels_container_create_split(de, focus->window, win, PANELS_SPLIT_HORIZONTAL);
        if (new_split) {
            if (new_split->parent == NULL) {
                ws->root = new_split;
            }
        }
    }
    
    panels_de_focus_window(de, win);
    panels_de_arrange_workspace(de, ws);
    panels_de_update_dock(de);
    
    return win;
}

void panels_de_focus_window(panels_de_t *de, panels_window_t *win) {
    if (!win) return;
    
    panels_container_set_focus(de->focused_container, 0);
    
    de->focused_window = win;
    if (win->container) {
        panels_de_focus_container(de, win->container);
    }
}

void panels_de_destroy_window(panels_de_t *de, panels_window_t *win) {
    if (!win || !win->id) return;
    
    lv_obj_del(win->lvgl_obj);
    if (win->decor_obj) lv_obj_del(win->decor_obj);
    
    /* Remove from container */
    panels_container_t *c = win->container;
    if (c) {
        if (c->type == PANELS_CONTAINER_LEAF) {
            panels_container_t *parent = c->parent;
            if (parent) {
                panels_container_t *sibling = (parent->child_a == c) ? parent->child_b : parent->child_a;
                if (sibling) {
                    if (parent->parent) {
                        if (parent->parent->child_a == parent) parent->parent->child_a = sibling;
                        else parent->parent->child_b = sibling;
                        sibling->parent = parent->parent;
                    } else {
                        sibling->parent = NULL;
                        win->workspace->root = sibling;
                    }
                } else {
                    win->workspace->root = NULL;
                }
            } else {
                win->workspace->root = NULL;
            }
        }
    }
    
    memset(win, 0, sizeof(panels_window_t));
    panels_de_update_dock(de);
    panels_de_arrange_workspace(de, panels_de_get_current_workspace(de));
}

void panels_de_set_window_state(panels_de_t *de, panels_window_t *win, panels_win_state_t state) {
    if (!win) return;
    win->state = state;
    panels_de_arrange_workspace(de, win->workspace);
}

void panels_de_toggle_floating(panels_de_t *de, panels_window_t *win) {
    if (!win) return;
    if (win->state == PANELS_WIN_FLOATING) {
        win->state = PANELS_WIN_NORMAL;
    } else {
        win->state = PANELS_WIN_FLOATING;
        win->saved_x = lv_obj_get_x(win->lvgl_obj);
        win->saved_y = lv_obj_get_y(win->lvgl_obj);
        win->saved_w = lv_obj_get_width(win->lvgl_obj);
        win->saved_h = lv_obj_get_height(win->lvgl_obj);
    }
    panels_de_arrange_workspace(de, win->workspace);
}

void panels_de_toggle_fullscreen(panels_de_t *de, panels_window_t *win) {
    if (!win) return;
    if (win->state == PANELS_WIN_FULLSCREEN) {
        win->state = PANELS_WIN_NORMAL;
        if (win->decor_obj) lv_obj_clear_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        win->state = PANELS_WIN_FULLSCREEN;
        if (win->decor_obj) lv_obj_add_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
    }
    panels_de_arrange_workspace(de, win->workspace);
}

/* ─────────────────────────────────────────────────────────────────────
 * Tiling operations
 */

void panels_de_split_container(panels_de_t *de, panels_container_t *c, panels_split_t dir, float ratio) {
    if (!c || c->type != PANELS_CONTAINER_LEAF) return;
    (void)dir; (void)ratio;
    /* Would split the leaf container */
}

void panels_de_resize_split(panels_de_t *de, panels_container_t *c, float ratio) {
    if (!c || c->type != PANELS_CONTAINER_SPLIT) return;
    if (ratio < 0.1f) ratio = 0.1f;
    if (ratio > 0.9f) ratio = 0.9f;
    c->split_ratio = ratio;
    panels_de_arrange_workspace(de, de->focused_container ? de->focused_container->workspace : NULL);
}

void panels_de_move_window_to_container(panels_de_t *de, panels_window_t *win, panels_container_t *dest) {
    if (!win || !dest) return;
    (void)de;
}

void panels_de_balance_windows(panels_de_t *de, panels_workspace_t *ws) {
    if (!ws || !ws->root) return;
    
    /* Recursively set all split ratios to 0.5 */
    panels_container_t *c = ws->root;
    /* Use iterative approach to avoid nested static function */
    /* Simple recursive walk without nested function */
    if (c) {
        if (c->type == PANELS_CONTAINER_SPLIT) {
            c->split_ratio = 0.5f;
            /* Recursively balance children */
            /* Using a simple approach - walk the tree inline */
            panels_container_t *stack[PANELS_MAX_CONTAINERS];
            int sp = 0;
            stack[sp++] = c->child_a;
            stack[sp++] = c->child_b;
            while (sp > 0) {
                panels_container_t *node = stack[--sp];
                if (!node) continue;
                if (node->type == PANELS_CONTAINER_SPLIT) {
                    node->split_ratio = 0.5f;
                    if (sp < PANELS_MAX_CONTAINERS - 2) {
                        stack[sp++] = node->child_a;
                        stack[sp++] = node->child_b;
                    }
                }
            }
        }
    }
    panels_de_arrange_workspace(de, ws);
}

/* ─────────────────────────────────────────────────────────────────────
 * Workspace management
 */

panels_workspace_t *panels_de_get_workspace(panels_de_t *de, int index) {
    if (index < 0 || index >= PANELS_MAX_WORKSPACES) return NULL;
    return &de->workspaces[index];
}

panels_workspace_t *panels_de_get_current_workspace(panels_de_t *de) {
    return &de->workspaces[de->current_workspace];;
}

int panels_de_get_workspace_index(panels_de_t *de, panels_workspace_t *ws) {
    if (!ws) return -1;
    return ws->id;
}

void panels_de_switch_workspace(panels_de_t *de, int index) {
    if (index < 0 || index >= de->workspace_count) return;
    
    /* Hide all windows on current workspace */
    panels_workspace_t *old_ws = panels_de_get_current_workspace(de);
    for (int i = 0; i < PANELS_MAX_WINDOWS; i++) {
        if (de->windows[i].id && de->windows[i].workspace == old_ws) {
            if (de->windows[i].lvgl_obj) {
                lv_obj_add_flag(de->windows[i].lvgl_obj, LV_OBJ_FLAG_HIDDEN);
                if (de->windows[i].decor_obj) {
                    lv_obj_add_flag(de->windows[i].decor_obj, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
    
    de->current_workspace = index;
    panels_workspace_t *new_ws = panels_de_get_current_workspace(de);
    new_ws->active = 1;
    old_ws->active = 0;
    
    /* Show windows on new workspace */
    for (int i = 0; i < PANELS_MAX_WINDOWS; i++) {
        if (de->windows[i].id && de->windows[i].workspace == new_ws) {
            if (de->windows[i].lvgl_obj) {
                lv_obj_clear_flag(de->windows[i].lvgl_obj, LV_OBJ_FLAG_HIDDEN);
                if (de->windows[i].decor_obj && de->windows[i].state != PANELS_WIN_FULLSCREEN) {
                    lv_obj_clear_flag(de->windows[i].decor_obj, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
    
    panels_de_arrange_workspace(de, new_ws);
    
    /* Update bar workspace indicators */
    for (int i = 0; i < de->workspace_count; i++) {
        lv_obj_t *btn = de->workspace_buttons[i];
        if (!btn) continue;
        if (i == index) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF007AFF), 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF2D2D30), 0);
        }
    }
}

void panels_de_move_window_to_workspace(panels_de_t *de, panels_window_t *win, int ws_idx) {
    if (!win || ws_idx < 0 || ws_idx >= de->workspace_count) return;
    
    /* Hide on old workspace */
    if (win->lvgl_obj) {
        lv_obj_add_flag(win->lvgl_obj, LV_OBJ_FLAG_HIDDEN);
        if (win->decor_obj) lv_obj_add_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
    }
    
    panels_workspace_t *new_ws = panels_de_get_workspace(de, ws_idx);
    win->workspace = new_ws;
    
    /* Add to new workspace */
    if (!new_ws->root) {
        new_ws->root = panels_container_create_leaf(de, win);
    }
    
    if (de->current_workspace == ws_idx) {
        if (win->lvgl_obj) {
            lv_obj_clear_flag(win->lvgl_obj, LV_OBJ_FLAG_HIDDEN);
            if (win->decor_obj) lv_obj_clear_flag(win->decor_obj, LV_OBJ_FLAG_HIDDEN);
        }
        panels_de_arrange_workspace(de, new_ws);
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * Top bar
 */

static lv_obj_t *panels_de_create_bar(panels_de_t *de) {
    lv_obj_t *bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bar, de->screen_w, PANELS_BAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF252526), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    
    /* Workspace buttons */
    for (int i = 0; i < de->workspace_count; i++) {
        lv_obj_t *btn = lv_obj_create(bar);
        lv_obj_set_size(btn, 28, PANELS_BAR_H - 6);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, 32 + i * 32, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_bg_color(btn, i == 0 ? lv_color_hex(0xFF007AFF) : lv_color_hex(0xFF2D2D30), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        
        lv_obj_t *dot = lv_obj_create(btn);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFFFFFFF), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        de->workspace_buttons[i] = btn;
    }
    
    /* Launcher button */
    de->launcher_button = lv_obj_create(bar);
    lv_obj_set_size(de->launcher_button, 120, PANELS_BAR_H - 6);
    lv_obj_align(de->launcher_button, LV_ALIGN_LEFT_MID, 32 + de->workspace_count * 32 + 16, 0);
    lv_obj_set_style_radius(de->launcher_button, 6, 0);
    lv_obj_set_style_bg_color(de->launcher_button, lv_color_hex(0xFF2D2D30), 0);
    lv_obj_set_style_border_width(de->launcher_button, 0, 0);
    
    lv_obj_t *launcher_label = lv_label_create(de->launcher_button);
    lv_label_set_text(launcher_label, "Applications");
    lv_obj_set_style_text_color(launcher_label, lv_color_hex(0xFFE0E0E0), 0);
    lv_obj_center(launcher_label);
    
    /* Clock (right side) */
    de->clock_label = lv_label_create(bar);
    lv_label_set_text(de->clock_label, "12:00");
    lv_obj_set_style_text_color(de->clock_label, lv_color_hex(0xFFE0E0E0), 0);
    lv_obj_align(de->clock_label, LV_ALIGN_RIGHT_MID, -16, 0);
    
    return bar;
}

/* ─────────────────────────────────────────────────────────────────────
 * Dock
 */

static void panels_de_create_dock(panels_de_t *de) {
    de->dock = lv_obj_create(lv_scr_act());
    lv_obj_set_size(de->dock, de->screen_w, PANELS_DOCK_H);
    lv_obj_align(de->dock, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(de->dock, 0, 0);
    lv_obj_set_style_bg_color(de->dock, lv_color_hex(0x881E1E20), 0);
    lv_obj_set_style_border_width(de->dock, 0, 0);
    lv_obj_set_style_pad_all(de->dock, 4, 0);
    
    /* Pinned apps */
    panels_de_dock_add_pinned(de, "terminal", "Terminal", "\xef\x81\x92");
    panels_de_dock_add_pinned(de, "files", "Files", "\xef\x81\xbb");
    panels_de_dock_add_pinned(de, "settings", "Settings", "\xef\x80\x93");
    panels_de_dock_add_pinned(de, "web", "Web", "\xef\x80\x88");
}

void panels_de_dock_add_pinned(panels_de_t *de, const char *app_id, const char *name, const char *icon_name) {
    if (de->dock_count >= PANELS_MAX_WINDOWS) return;
    (void)app_id; (void)name;
    
    lv_obj_t *btn = lv_obj_create(de->dock);
    lv_obj_set_size(btn, 48, PANELS_DOCK_H - 8);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 8 + de->dock_count * 56, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x402D2D30), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, icon_name);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFE0E0E0), 0);
    lv_obj_center(icon);    
    de->dock_items[de->dock_count] = btn;
    de->dock_count++;
}

void panels_de_dock_remove_pinned(panels_de_t *de, const char *app_id) {
    (void)de; (void)app_id;
}

void panels_de_dock_update_running(panels_de_t *de, panels_window_t *win, int running) {
    (void)de; (void)win; (void)running;
}

/* ─────────────────────────────────────────────────────────────────────
 * Launcher
 */

static void panels_de_launcher_item_click(lv_event_t *e);

static void panels_de_create_launcher(panels_de_t *de) {
    /* Full-screen overlay */
    de->launcher_scr = lv_obj_create(lv_scr_act());
    lv_obj_set_size(de->launcher_scr, de->screen_w, de->screen_h);
    lv_obj_align(de->launcher_scr, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(de->launcher_scr, 0, 0);
    lv_obj_set_style_bg_color(de->launcher_scr, lv_color_hex(0xCC000000), 0);
    lv_obj_set_style_border_width(de->launcher_scr, 0, 0);
    lv_obj_set_style_pad_all(de->launcher_scr, 0, 0);
    lv_obj_add_flag(de->launcher_scr, LV_OBJ_FLAG_HIDDEN);
    
    /* Panel */
    de->launcher_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(de->launcher_panel, 600, 480);
    lv_obj_align(de->launcher_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(de->launcher_panel, 12, 0);
    lv_obj_set_style_bg_color(de->launcher_panel, lv_color_hex(0xFF252526), 0);
    lv_obj_set_style_border_width(de->launcher_panel, 0, 0);
    lv_obj_set_style_pad_all(de->launcher_panel, 16, 0);
    lv_obj_add_flag(de->launcher_panel, LV_OBJ_FLAG_HIDDEN);
    
    /* Search field */
    de->launcher_search = lv_textarea_create(de->launcher_panel);
    lv_obj_set_size(de->launcher_search, 568, 36);
    lv_obj_align(de->launcher_search, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(de->launcher_search, 6, 0);
    lv_obj_set_style_bg_color(de->launcher_search, lv_color_hex(0xFF1E1E20), 0);
    lv_textarea_set_placeholder_text(de->launcher_search, "Search apps...");
    lv_obj_set_style_text_color(de->launcher_search, lv_color_hex(0xFFFFFFFF), 0);
    
    /* Grid */
    de->launcher_grid = lv_obj_create(de->launcher_panel);
    lv_obj_set_size(de->launcher_grid, 568, 390);
    lv_obj_align(de->launcher_grid, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(de->launcher_grid, 8, 0);
    lv_obj_set_style_bg_color(de->launcher_grid, lv_color_hex(0xFF1E1E20), 0);
    lv_obj_set_style_border_width(de->launcher_grid, 0, 0);
    lv_obj_set_style_pad_all(de->launcher_grid, 8, 0);
    lv_obj_set_flex_flow(de->launcher_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(de->launcher_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    
    /* Add app items */
    for (int i = 0; i < DEFAULT_APP_COUNT; i++) {
        lv_obj_t *item = lv_obj_create(de->launcher_grid);
        lv_obj_set_size(item, 96, 96);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xFF2D2D30), 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_pad_gap(item, 4, 0);
        
        lv_obj_t *icon = lv_label_create(item);
        lv_label_set_text(icon, default_app_catalog[i].icon_char);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(default_app_catalog[i].color), 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 16);
        
        lv_obj_t *name = lv_label_create(item);
        lv_label_set_text(name, default_app_catalog[i].name);
        lv_obj_set_style_text_color(name, lv_color_hex(0xFFE0E0E0), 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -12);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        
        lv_obj_set_user_data(item, (void *)(intptr_t)(i + 1));  /* Store app index (+1 to avoid 0=NULL) */
        lv_obj_add_event_cb(item, panels_de_launcher_item_click, LV_EVENT_CLICKED, NULL);
    }
}

static void panels_de_launcher_item_click(lv_event_t *e) {
    lv_obj_t *item = lv_event_get_target(e);
    intptr_t idx = (intptr_t)lv_obj_get_user_data(item);
    int app_idx = (int)idx - 1;
    panels_de_t *de = g_de;
    
    if (app_idx >= 0 && app_idx < DEFAULT_APP_COUNT && de && de->launcher_cb) {
        de->launcher_cb(app_idx, de->launcher_userdata);
    }
    
    if (de) panels_de_close_launcher(de);
}

int panels_de_open_launcher(panels_de_t *de, const char *const *names, int count,
                             void (*cb)(int index, void *userdata), void *userdata) {
    if (!de || de->launcher_open) return 0;
    
    de->launcher_cb = cb;
    de->launcher_userdata = userdata;
    
    /* Copy app names from Qt */
    if (count > PANELS_MAX_WINDOWS) count = PANELS_MAX_WINDOWS;
    de->launcher_name_count = count;
    if (de->launcher_names) {
        /* Free old names if any */
    }
    de->launcher_names = (const char **)names;
    
    if (!de->launcher_scr) {
        panels_de_create_launcher(de);
    }
    
    de->launcher_open = 1;
    if (de->launcher_scr) lv_obj_clear_flag(de->launcher_scr, LV_OBJ_FLAG_HIDDEN);
    if (de->launcher_panel) lv_obj_clear_flag(de->launcher_panel, LV_OBJ_FLAG_HIDDEN);
    
    /* Hide desktop elements */
    if (de->bar) lv_obj_add_flag(de->bar, LV_OBJ_FLAG_HIDDEN);
    if (de->dock) lv_obj_add_flag(de->dock, LV_OBJ_FLAG_HIDDEN);
    
    if (de->launcher_search) {
        lv_obj_clear_flag(de->launcher_search, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(de->launcher_search, LV_STATE_FOCUSED);
    }
    
    /* Disable Qt input */
    lvgl_port_set_input_enabled(0);
    
    return 1;
}

void panels_de_close_launcher_with_data(panels_de_t *de) {
    if (!de || !de->launcher_open) return;
    de->launcher_open = 0;
    
    if (de->launcher_scr) lv_obj_add_flag(de->launcher_scr, LV_OBJ_FLAG_HIDDEN);
    if (de->launcher_panel) lv_obj_add_flag(de->launcher_panel, LV_OBJ_FLAG_HIDDEN);
    if (de->launcher_search) lv_textarea_set_text(de->launcher_search, "");
    
    /* Show desktop elements */
    if (de->bar) lv_obj_clear_flag(de->bar, LV_OBJ_FLAG_HIDDEN);
    if (de->dock) lv_obj_clear_flag(de->dock, LV_OBJ_FLAG_HIDDEN);
    
    de->launcher_cb = NULL;
    de->launcher_userdata = NULL;
    de->launcher_names = NULL;
    de->launcher_name_count = 0;
    
    /* Re-enable Qt input */
    lvgl_port_set_input_enabled(1);
    
}

void panels_de_toggle_launcher(panels_de_t *de) {
    if (de->launcher_open) {
        panels_de_close_launcher_with_data(de);
    } else {
        panels_de_open_launcher(de, NULL, 0, NULL, NULL);
    }
}

void panels_de_close_launcher(panels_de_t *de) {
    panels_de_close_launcher_with_data(de);
}

/* ─────────────────────────────────────────────────────────────────────
 * Notifications (toasts)
 */

static void panels_de_toast_timeout_cb(lv_timer_t *timer) {
    toast_ctx_t *ctx = (toast_ctx_t *)timer->user_data;
    panels_de_t *de = ctx->de;
    
    uint32_t now = lv_tick_get();
    if ((int32_t)(now - ctx->start_time) >= ctx->timeout_ms) {
        if (ctx->toast) {
            lv_obj_del(ctx->toast);
            ctx->toast = NULL;
        }
        lv_timer_del(timer);
        panels_de_arrange_workspace(de, panels_de_get_current_workspace(de));
    }
}

void panels_de_show_toast(panels_de_t *de, const char *title, const char *message, int timeout_ms) {
    int idx = -1;
    for (int i = 0; i < PANELS_MAX_WINDOWS; i++) {
        if (toast_contexts[i].toast == NULL) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;
    
    lv_obj_t *toast = lv_obj_create(lv_scr_act());
    lv_obj_set_size(toast, 300, 72);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_RIGHT, -16, -16 - (PANELS_MAX_WINDOWS - idx - 1) * 80);
    lv_obj_set_style_radius(toast, 8, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0xFF2D2D30), 0);
    lv_obj_set_style_border_width(toast, 0, 0);
    lv_obj_set_style_pad_all(toast, 12, 0);
    
    lv_obj_t *title_label = lv_label_create(toast);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t *msg_label = lv_label_create(toast);
    lv_label_set_text(msg_label, message);
    lv_obj_set_style_text_color(msg_label, lv_color_hex(0xFFE0E0E0), 0);
    lv_obj_align(msg_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    /* Create timer for auto-dismiss */
    lv_timer_create(panels_de_toast_timeout_cb, 100, &toast_contexts[idx]);
    /* Store toast context */
    toast_contexts[idx].de = de;
    toast_contexts[idx].toast = toast;
    toast_contexts[idx].timeout_ms = timeout_ms;
    toast_contexts[idx].start_time = lv_tick_get();
    
    /* Pass context via timer user_data - use the index */
    /* Actually just set timer user_data directly */
    /* lv_timer_create takes void *user_data, so we can pass &toast_contexts[idx] */
}

/* ─────────────────────────────────────────────────────────────────────
 * Input handling
 */

static int panels_keycode_to_lvgl(int keycode) {
    switch (keycode) {
        case 79: return LV_KEY_RIGHT;
        case 80: return LV_KEY_UP;
        case 81: return LV_KEY_LEFT;
        case 82: return LV_KEY_DOWN;
        case 83: return LV_KEY_ESC;
        case 85: return LV_KEY_DEL;
        case 86: return LV_KEY_ENTER;
        case 87: return LV_KEY_HOME;
        case 88: return LV_KEY_END;
        case 28: return LV_KEY_ENTER;
        default: return keycode;
    }
}

void panels_de_handle_key(panels_de_t *de, int key, int pressed) {
    if (pressed) {
        int lvgl_key = panels_keycode_to_lvgl(key);
        
        if (key == 91 || key == 125) { de->mod_pressed = 1; return; }
        if (key == 42 || key == 54) { de->shift_pressed = 1; return; }
        if (key == 29 || key == 96) { de->ctrl_pressed = 1; return; }
        if (key == 56 || key == 100) { de->alt_pressed = 1; return; }
        
        if (de->mod_pressed && !de->shift_pressed && !de->ctrl_pressed && !de->alt_pressed) {
            if (key >= 2 && key <= 10) { panels_de_switch_workspace(de, key - 2); return; }
            if (key == 30) { panels_de_toggle_launcher(de); return; }
            if (key == 57) { if (de->launcher_open) panels_de_close_launcher(de); return; }
            if (key == 17) { panels_de_destroy_window(de, de->focused_window); return; }
            if (key == 36) { panels_de_focus_direction(de, PANELS_SPLIT_VERTICAL); return; }
            if (key == 37) { panels_de_focus_direction(de, PANELS_SPLIT_HORIZONTAL); return; }
            if (key == 38) { return; }
            if (key == 24) { return; }
            if (key == 25) { panels_de_toggle_floating(de, de->focused_window); return; }
            if (key == 39 || key == 27) { panels_de_toggle_fullscreen(de, de->focused_window); return; }
            if (key == 20) { return; }
            if (key == 32) { panels_de_show_toast(de, "CodeOS", "Desktop captured", 2000); return; }
        }
        
        if (de->mod_pressed && de->shift_pressed && key >= 2 && key <= 10) {
            panels_de_move_window_to_workspace(de, de->focused_window, key - 2);
            panels_de_switch_workspace(de, key - 2);
            return;
        }
        
        if (de->mod_pressed && key == 28) {
            panels_de_create_window(de, "Terminal", 480, 320);
            return;
        }
        
        if (de->launcher_open && de->launcher_search) {
            if (lvgl_key == LV_KEY_ESC) { panels_de_close_launcher(de); return; }
            if (lvgl_key == LV_KEY_ENTER) { panels_de_close_launcher(de); return; }
        }
    } else {
        if (key == 91 || key == 125) de->mod_pressed = 0;
        if (key == 42 || key == 54) de->shift_pressed = 0;
        if (key == 29 || key == 96) de->ctrl_pressed = 0;
        if (key == 56 || key == 100) de->alt_pressed = 0;
    }
}

void panels_de_handle_mouse(panels_de_t *de, int x, int y, int buttons) {
    (void)de; (void)x; (void)y; (void)buttons;
}

/* ─────────────────────────────────────────────────────────────────────
 * Init / Quit / Pump
 */

void panels_de_get_instance_ptr(panels_de_t **out) {
    *out = g_de;
}

void panels_de_init(panels_de_t *de, int screen_w, int screen_h) {
    memset(de, 0, sizeof(panels_de_t));
    de->screen_w = screen_w;
    de->screen_h = screen_h;
    de->workspace_count = 6;
    de->current_workspace = 0;
    de->next_window_id = 1;
    de->next_container_id = 1;
    
    const char *ws_names[] = {"1", "2", "3", "4", "5", "6"};
    for (int i = 0; i < de->workspace_count; i++) {
        de->workspaces[i].id = i;
        strncpy_safe(de->workspaces[i].name, ws_names[i], sizeof(de->workspaces[i].name)-1);
        de->workspaces[i].active = (i == 0) ? 1 : 0;
    }
    
    de->focused_workspace = &de->workspaces[0];
    
    g_de = de;
}

void panels_de_create_ui(panels_de_t *de) {
    if (!de->bar) {
        de->bar = panels_de_create_bar(de);
        panels_de_create_dock(de);
    }
}

void panels_de_quit(panels_de_t *de) {
    (void)de;
    g_de = NULL;
}

void panels_de_pump(panels_de_t *de) {
    static uint32_t last_tick = 0;
    uint32_t now = lv_tick_get();
    if ((int32_t)(now - last_tick) > 1000) {
        last_tick = now;
        if (de->clock_label) {
            lv_label_set_text(de->clock_label, "12:00");
        }
    }
}

panels_de_t *panels_de_get_instance(void) {
    return g_de;
}

/* ─────────────────────────────────────────────────────────────────────
 * Input handling
 * ───────────────────────────────────────────────────────────────────── */

int panels_wm_init(panels_de_t *de, int screen_w, int screen_h) {
    panels_de_init(de, screen_w, screen_h);
    return 0;
}

void panels_wm_quit(panels_de_t *de) {
    panels_de_quit(de);
}

int panels_wm_create_window(panels_de_t *de, const char *title) {
    panels_window_t *win = panels_de_create_window(de, title, 480, 320);
    return win ? win->id : -1;
}

void panels_wm_destroy_window(panels_de_t *de, int win_id) {
    for (int i = 0; i < PANELS_MAX_WINDOWS; i++) {
        if (de->windows[i].id == win_id) {
            panels_de_destroy_window(de, &de->windows[i]);
            return;
        }
    }
}