extern "C" {
#include "wm.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
#include "process.h"
}

#define WM_WINDOW_W 800
#define WM_WINDOW_H 600

static window_info_t {
    int x, y, w, h;
    int floating;
    int focused;
    char title[64];
} wm_windows[WM_MAX_WINDOWS];

static int wm_window_count = 0;
static int wm_focused_idx = -1;
static int wm_layout = WM_LAYOUT_FLOATING;
static int wm_gaps = 8;
static int wm_master_factor = 50;

void wm_init(void) {
    wm_window_count = 0;
    wm_focused_idx = -1;
    memset(wm_windows, 0, sizeof(wm_windows));
}

void wm_load_config(const char *path) {
    (void)path; /* Simplified - no config file parsing */
}

void wm_handle_key(int key) {
    /* Handle modifier keys, workspace switching, etc. */
    if (key >= '1' && key <= '9') {
        int idx = key - '1';
        if (idx < wm_window_count) {
            wm_focused_idx = idx;
        }
    }
}

void wm_apply_layout(void) {
    /* Apply the current layout to all windows */
    for (int i = 0; i < wm_window_count; i++) {
        window_info_t *w = &wm_windows[i];
        /* Layout handling simplified */
    }
}

int wm_active(void) { return wm_focused_idx; }

int wm_active_workspace(void) { return 0; }

int wm_workspace_count(void) { return 1; }

int wm_border_width(void) { return 2; }

uint32_t wm_border_focus(void) { return 0x400066FF; }

uint32_t wm_border_normal(void) { return 0x30808080; }

int wm_window_floating(int idx) {
    if (idx < 0 || idx >= wm_window_count) return 0;
    return wm_windows[idx].floating;
}

void wm_window_set_floating(int idx, int floating) {
    if (idx < 0 || idx >= wm_window_count) return;
    wm_windows[idx].floating = floating;
}

void wm_window_toggle_floating(int idx) {
    if (idx < 0 || idx >= wm_window_count) return;
    wm_windows[idx].floating = !wm_windows[idx].floating;
}

void wm_anim_start(int win_idx, enum wm_anim_type type, uint64_t duration_ms) {
    (void)win_idx; (void)type; (void)duration_ms;
    /* Animation state simplified */
}

void wm_anim_update(uint64_t now_ms) {
    /* Animation update - simplified, no-op */
}

int wm_anim_get_pos(int win_idx, int *x, int *y, int *w, int *h, int *opacity) {
    (void)win_idx;
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = WM_WINDOW_W;
    if (h) *h = WM_WINDOW_H;
    if (opacity) *opacity = 255;
    return 0;
}

int wm_get_gaps(void) { return wm_gaps; }

int wm_get_master_factor(void) { return wm_master_factor; }

void wm_set_master_factor(int factor) {
    wm_master_factor = factor > 100 ? 100 : factor < 0 ? 0 : factor;
}

enum wm_layout wm_get_layout(int ws) { (void)ws; return WM_LAYOUT_FLOATING; }

void wm_set_layout(int ws, int layout) {
    (void)ws; (void)layout;
}

void wm_cycle_layout(void) {
    /* Cycle through layouts */
    static int layouts[] = {
        WM_LAYOUT_MASTER_STACK, WM_LAYOUT_GRID,
        WM_LAYOUT_MONOCLE, WM_LAYOUT_FLOATING
    };
    static int current = 0;
    current = (current + 1) % 4;
    wm_set_layout(0, layouts[current]);
}

int wm_dock_enabled(void) { return 1; }

int wm_menubar_enabled(void) { return 1; }

int wm_menubar_clock(void) { return 1; }

const char *wm_dock_icons(void) { return "default"; }