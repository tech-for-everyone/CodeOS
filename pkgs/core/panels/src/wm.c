#include "wm.h"
#include "desktop.h"
#include "dock.h"
#include "kprintf.h"
#include "string.h"
#include "keyboard.h"
#include "fb.h"
#include "ext2.h"
#include "timer.h"
#include "switcher.h"

static int border_w = 2;
static int gap = 4;
static int master_factor = 55;
static uint32_t cfg_border_focus = 0xFF64D2FF;   /* C_SKY */
static uint32_t cfg_border_normal = 0xFF48484A;  /* C_SURFACE2 */

static int cfg_dock_enabled = 1;
static int cfg_menubar_enabled = 1;
static int cfg_menubar_clock = 1;
static char cfg_dock_icons[256] = "Terminal,About,Calc,DevStore,Settings,OpenWeb,Exit";

static int active_workspace;
static int win_workspace[WM_MAX_WINDOWS];
static int win_tiled[WM_MAX_WINDOWS];
static int win_floating[WM_MAX_WINDOWS];
static enum wm_layout layouts[WM_MAX_WORKSPACES];
static int wm_initialized;

static wm_anim_t animations[WM_MAX_WINDOWS];

static int get_panel_height(void) { return cfg_menubar_enabled ? 24 : 0; }

static int smoothstep_int(int t) {
    if (t <= 0) return 0;
    if (t >= 1000) return 1000;
    uint64_t t2 = (uint64_t)t * t / 1000;
    uint64_t t3 = (uint64_t)t2 * t / 1000;
    return (int)(3 * t2 - 2 * t3);
}

void wm_anim_start(int win_idx, enum wm_anim_type type, uint64_t duration_ms) {
    if (win_idx < 0 || win_idx >= WM_MAX_WINDOWS) return;
    wm_anim_t *a = &animations[win_idx];
    a->active = 1;
    a->win_idx = win_idx;
    a->type = type;
    a->start_ms = timer_get_milliseconds();
    a->duration_ms = duration_ms;
    a->progress = 0;

    window_t *w = desktop_get_window(win_idx);
    if (w) {
        if (type == WM_ANIM_OPEN) {
            int cx = w->x + w->w / 2;
            int cy = w->y + w->h / 2;
            a->x_from = cx; a->y_from = cy;
            a->x_to = w->x; a->y_to = w->y;
            a->w_from = 0;  a->w_to = w->w;
            a->h_from = 0;  a->h_to = w->h;
        } else if (type == WM_ANIM_CLOSE) {
            int cx = w->x + w->w / 2;
            int cy = w->y + w->h / 2;
            a->x_from = w->x; a->y_from = w->y;
            a->x_to = cx;     a->y_to = cy;
            a->w_from = w->w; a->w_to = 0;
            a->h_from = w->h; a->h_to = 0;
        } else if (type == WM_ANIM_MINIMIZE) {
            uint32_t sw = fb_getwidth();
            uint32_t sh = fb_getheight();
            int dock_h_val = dock_h();
            int target_x = (int)sw / 2 - 10;
            int target_y = (int)sh - dock_h_val - 10;
            a->x_from = w->x; a->y_from = w->y;
            a->x_to = target_x; a->y_to = target_y;
            a->w_from = w->w; a->w_to = 0;
            a->h_from = w->h; a->h_to = 0;
        } else {
            a->x_from = w->x; a->y_from = w->y;
            a->x_to = w->x;   a->y_to = w->y;
            a->w_from = w->w; a->h_from = w->h;
            a->w_to = w->w;   a->h_to = w->h;
        }
    }
}

void wm_anim_update(uint64_t now_ms) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_anim_t *a = &animations[i];
        if (!a->active) continue;
        uint64_t elapsed = now_ms - a->start_ms;
        if (elapsed >= a->duration_ms) {
            a->active = 0;
            if (a->type == WM_ANIM_MINIMIZE) {
                window_t *w = desktop_get_window(a->win_idx);
                if (w) { w->minimized = 1; w->visible = 0; }
            }
            if (a->type == WM_ANIM_CLOSE) {
                window_t *w = desktop_get_window(a->win_idx);
                if (w) w->visible = 0;
            }
            continue;
        }
        int t = (int)(elapsed * 1000 / a->duration_ms);
        a->progress = smoothstep_int(t);
    }
}

int wm_anim_get_pos(int win_idx, int *x, int *y, int *w, int *h, int *opacity) {
    if (win_idx < 0 || win_idx >= WM_MAX_WINDOWS) return 0;
    wm_anim_t *a = &animations[win_idx];
    if (!a->active) return 0;
    int p = a->progress;
    if (opacity) *opacity = (a->type == WM_ANIM_CLOSE || a->type == WM_ANIM_MINIMIZE) ? (1000 - p) : p;
    if (x) *x = a->x_from + (a->x_to - a->x_from) * p / 1000;
    if (y) *y = a->y_from + (a->y_to - a->y_from) * p / 1000;
    if (w) *w = a->w_from + (a->w_to - a->w_from) * p / 1000;
    if (h) *h = a->h_from + (a->h_to - a->h_from) * p / 1000;
    return 1;
}

void wm_init(void) {
    for (int i = 0; i < WM_MAX_WORKSPACES; i++)
        layouts[i] = WM_LAYOUT_MASTER_STACK;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        win_workspace[i] = 0;
        win_tiled[i] = 1;
        win_floating[i] = 0;
    }
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        animations[i].active = 0;
    active_workspace = 0;
    wm_initialized = 1;
}

void wm_load_config(const char *path) {
    if (!ext2_mounted()) return;
    char buf[2048];
    int n = ext2_read_file_path(path, buf, sizeof(buf) - 1);
    if (n <= 0) {
        const char *def = "# CodeOS WM + Desktop Configuration\n"
            "border_width = 2\n"
            "gap_size = 4\n"
            "master_factor = 55\n"
            "border_focus = 0xFF89dceb\n"
            "border_normal = 0xFF45475a\n"
            "dock_enabled = 1\n"
            "menubar_enabled = 1\n"
            "menubar_clock = 1\n"
            "dock_icons = Terminal,About,Calc,DevStore,Settings,OpenWeb,Exit\n";
        ext2_write_file_path(path, def, strlen(def));
        n = strlen(def);
        for (int i = 0; i < n; i++) buf[i] = def[i];
        buf[n] = 0;
    }
    if (n > 0) buf[n] = 0;

    char *p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p || *p == '#') { while (*p && *p != '\n') p++; continue; }
        if (*p == '[') { while (*p && *p != '\n') p++; continue; }

        char key[64], val[128];
        int ki = 0, vi = 0;
        while (*p && *p != '=' && *p != ' ' && *p != '\t' && ki < 63)
            key[ki++] = *p++;
        key[ki] = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;
        while (*p && *p != '\n' && vi < 127)
            val[vi++] = *p++;
        val[vi] = 0;

        if (!strcmp(key, "border_width")) {
            int v = 0, sign = 1;
            char *vp = val;
            if (*vp == '-') { sign = -1; vp++; }
            while (*vp >= '0' && *vp <= '9') v = v * 10 + (*vp++ - '0');
            border_w = v * sign;
        } else if (!strcmp(key, "gap_size")) {
            int v = 0;
            char *vp = val;
            while (*vp >= '0' && *vp <= '9') v = v * 10 + (*vp++ - '0');
            gap = v;
        } else if (!strcmp(key, "master_factor")) {
            int v = 0;
            char *vp = val;
            while (*vp >= '0' && *vp <= '9') v = v * 10 + (*vp++ - '0');
            if (v >= 10 && v <= 90) master_factor = v;
        } else if (!strcmp(key, "border_focus")) {
            unsigned long uv = 0;
            char *vp = val;
            if (vp[0] == '0' && (vp[1] == 'x' || vp[1] == 'X')) vp += 2;
            while (*vp) { uv <<= 4;
                if (*vp >= '0' && *vp <= '9') uv |= *vp - '0';
                else if (*vp >= 'a' && *vp <= 'f') uv |= *vp - 'a' + 10;
                else if (*vp >= 'A' && *vp <= 'F') uv |= *vp - 'A' + 10;
                vp++;
            }
            cfg_border_focus = (uint32_t)uv;
        } else if (!strcmp(key, "border_normal")) {
            unsigned long uv = 0;
            char *vp = val;
            if (vp[0] == '0' && (vp[1] == 'x' || vp[1] == 'X')) vp += 2;
            while (*vp) { uv <<= 4;
                if (*vp >= '0' && *vp <= '9') uv |= *vp - '0';
                else if (*vp >= 'a' && *vp <= 'f') uv |= *vp - 'a' + 10;
                else if (*vp >= 'A' && *vp <= 'F') uv |= *vp - 'A' + 10;
                vp++;
            }
            cfg_border_normal = (uint32_t)uv;
        } else if (!strcmp(key, "dock_enabled")) {
            cfg_dock_enabled = (val[0] == '1' || val[0] == 'y' || val[0] == 'Y');
        } else if (!strcmp(key, "menubar_enabled")) {
            cfg_menubar_enabled = (val[0] == '1' || val[0] == 'y' || val[0] == 'Y');
        } else if (!strcmp(key, "menubar_clock")) {
            cfg_menubar_clock = (val[0] == '1' || val[0] == 'y' || val[0] == 'Y');
        } else if (!strcmp(key, "dock_icons")) {
            strncpy_safe(cfg_dock_icons, val, sizeof(cfg_dock_icons));
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

static void layout_master_stack(int ws, int win_indices[], int nwin) {
    (void)ws;
    if (nwin == 0) return;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int ph = get_panel_height();
    int avail_y = (int)sh - ph;
    int usable_x = (int)sw - gap * 2;
    int usable_y = avail_y - gap * 2;

    if (nwin == 1) {
        window_t *w = desktop_get_window(win_indices[0]);
        if (w) {
            w->x = gap;
            w->y = gap;
            w->w = usable_x - border_w * 2;
            w->h = usable_y - border_w * 2;
        }
        return;
    }

    int master_w = usable_x * master_factor / 100;
    int stack_w = usable_x - master_w - gap;

    int i = 0;
    window_t *master = desktop_get_window(win_indices[i]);
    if (master) {
        master->x = gap;
        master->y = gap;
        master->w = master_w - border_w * 2;
        master->h = usable_y - border_w * 2;
    }
    i++;

    int remain = nwin - 1;
    if (remain > 0) {
        int stack_x = gap + master_w + gap;
        int each_h = (usable_y - (remain - 1) * gap) / remain;
        for (int j = 0; j < remain && i < nwin; j++) {
            window_t *w = desktop_get_window(win_indices[i]);
            if (w) {
                w->x = stack_x;
                w->y = gap + j * (each_h + gap);
                w->w = stack_w - border_w * 2;
                w->h = each_h - border_w * 2;
            }
            i++;
        }
    }
}

static void layout_grid(int ws, int win_indices[], int nwin) {
    (void)ws;
    if (nwin == 0) return;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int ph = get_panel_height();
    int avail_y = (int)sh - ph;

    int cols = 1;
    while (cols * cols < nwin) cols++;
    int rows = (nwin + cols - 1) / cols;
    int cell_w = ((int)sw - gap * 2) / cols - gap + gap / cols;
    int cell_h = (avail_y - gap * 2) / rows - gap + gap / rows;
    if (cell_w < 50) cell_w = 50;
    if (cell_h < 50) cell_h = 50;

    for (int i = 0; i < nwin; i++) {
        window_t *w = desktop_get_window(win_indices[i]);
        if (!w) continue;
        int c = i % cols;
        int r = i / cols;
        w->x = gap + c * (cell_w + gap);
        w->y = gap + r * (cell_h + gap);
        w->w = cell_w - border_w * 2;
        w->h = cell_h - border_w * 2;
    }
}

static void layout_monocle(int ws, int win_indices[], int nwin) {
    (void)ws;
    if (nwin == 0) return;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int ph = get_panel_height();
    int avail_y = (int)sh - ph;

    int focused = desktop_focused_window();
    for (int i = 0; i < nwin; i++) {
        window_t *w = desktop_get_window(win_indices[i]);
        if (!w) continue;
        if (win_indices[i] == focused) {
            w->x = gap;
            w->y = gap;
            w->w = (int)sw - gap * 2 - border_w * 2;
            w->h = avail_y - gap * 2 - border_w * 2;
            desktop_window_show(win_indices[i], 1);
        } else {
            desktop_window_show(win_indices[i], 0);
        }
    }
}

static void apply_layout_for_workspace(int ws) {
    int indices[WM_MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < desktop_window_count(); i++) {
        window_t *w = desktop_get_window(i);
        if (!w || !w->visible) continue;
        if (win_workspace[i] != ws) continue;
        if (win_floating[i]) continue;
        indices[n++] = i;
    }
    if (n == 0) return;

    switch (layouts[ws]) {
        case WM_LAYOUT_MASTER_STACK: layout_master_stack(ws, indices, n); break;
        case WM_LAYOUT_GRID:         layout_grid(ws, indices, n);         break;
        case WM_LAYOUT_MONOCLE:      layout_monocle(ws, indices, n);      break;
        default: break;
    }
}

void wm_apply_layout(void) {
    apply_layout_for_workspace(active_workspace);
    desktop_redraw();
}

void wm_window_to_workspace(int win_idx, int ws) {
    if (win_idx < 0 || ws < 0 || ws >= WM_MAX_WORKSPACES) return;
    win_workspace[win_idx] = ws;
    apply_layout_for_workspace(ws);
}

int wm_active(void) { return wm_initialized; }
int wm_active_workspace(void) { return active_workspace; }
int wm_workspace_count(void)  { return WM_MAX_WORKSPACES; }
int wm_border_width(void)     { return border_w; }
uint32_t wm_border_focus(void)   { return cfg_border_focus; }
uint32_t wm_border_normal(void)  { return cfg_border_normal; }
int wm_window_floating(int idx) {
    if (idx < 0 || idx >= WM_MAX_WINDOWS) return 0;
    return win_floating[idx];
}
void wm_window_set_floating(int idx, int floating) {
    if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
    win_floating[idx] = floating;
    if (!floating) wm_apply_layout();
}
void wm_window_toggle_floating(int idx) {
    if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
    win_floating[idx] = !win_floating[idx];
    if (win_floating[idx]) {
        window_t *w = desktop_get_window(idx);
        if (w) { w->x = 100; w->y = 100; w->w = 600; w->h = 400; }
    } else {
        wm_apply_layout();
    }
}
int wm_get_gaps(void) { return gap; }
int wm_get_master_factor(void) { return master_factor; }
int wm_dock_enabled(void) { return cfg_dock_enabled; }
int wm_menubar_enabled(void) { return cfg_menubar_enabled; }
int wm_menubar_clock(void) { return cfg_menubar_clock; }
const char *wm_dock_icons(void) { return cfg_dock_icons; }

void wm_set_master_factor(int factor) { if (factor >= 10 && factor <= 90) master_factor = factor; }
enum wm_layout wm_get_layout(int ws) {
    if (ws < 0 || ws >= WM_MAX_WORKSPACES) return WM_LAYOUT_MASTER_STACK;
    return layouts[ws];
}
void wm_set_layout(int ws, int layout) {
    if (ws < 0 || ws >= WM_MAX_WORKSPACES) return;
    if (layout < WM_LAYOUT_MASTER_STACK || layout > WM_LAYOUT_MONOCLE) return;
    layouts[ws] = (enum wm_layout)layout;
}
void wm_cycle_layout(void) {
    layouts[active_workspace]++;
    if (layouts[active_workspace] > WM_LAYOUT_MONOCLE)
        layouts[active_workspace] = WM_LAYOUT_MASTER_STACK;
    wm_apply_layout();
}

void wm_handle_key(int key) {
    if (!wm_initialized) return;

    int alt = keyboard_is_alt_down();
    int ctrl = keyboard_is_ctrl_down();
    int shift = keyboard_is_shift_down();

    if (alt) {
        switch (key) {
            case 'j': case '\t': {
                if (switcher_active()) {
                    switcher_next();
                } else {
                    switcher_activate();
                    desktop_redraw();
                }
                break;
            }
            case 'k': {
                if (switcher_active()) {
                    switcher_prev();
                } else {
                    desktop_focus_prev();
                    wm_apply_layout();
                }
                break;
            }
            case 'm': {
                int fw = desktop_focused_window();
                if (fw >= 0) {
                    window_t *w = desktop_get_window(fw);
                    if (w) {
                        w->maximized = !w->maximized;
                        if (w->maximized) {
                            w->x = 0; w->y = 0;
                            w->w = fb_getwidth(); w->h = fb_getheight();
                        } else {
                            w->x = w->orig_x ? w->orig_x : 100;
                            w->y = w->orig_y ? w->orig_y : 100;
                            w->w = w->orig_w ? w->orig_w : 400;
                            w->h = w->orig_h ? w->orig_h : 300;
                        }
                        desktop_redraw();
                    }
                }
                break;
            }
            case 'h': {
                master_factor -= 5;
                if (master_factor < 10) master_factor = 10;
                wm_apply_layout();
                break;
            }
            case 'l': {
                master_factor += 5;
                if (master_factor > 90) master_factor = 90;
                wm_apply_layout();
                break;
            }
            case 'q': {
                int fw = desktop_focused_window();
                if (fw >= 0) desktop_close_window(fw);
                wm_apply_layout();
                break;
            }
            case 'f': {
                int fw = desktop_focused_window();
                if (fw >= 0) wm_window_toggle_floating(fw);
                desktop_redraw();
                break;
            }
            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9': {
                int ws = key - '1';
                active_workspace = ws;
                for (int i = 0; i < desktop_window_count(); i++) {
                    window_t *w = desktop_get_window(i);
                    if (!w) continue;
                    if (win_workspace[i] == ws)
                        desktop_window_show(i, 1);
                    else
                        desktop_window_show(i, 0);
                }
                desktop_set_focused(-1);
                wm_apply_layout();
                break;
            }
            case ' ': {
                wm_cycle_layout();
                break;
            }
            case '\r': {
                extern int terminal_open(void);
                terminal_open();
                break;
            }
            case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT: {
                if (shift) {
                    int fw = desktop_focused_window();
                    if (fw < 0) break;
                    window_t *w = desktop_get_window(fw);
                    if (!w) break;
                    int ti = -1;
                    int cx = w->x + w->w / 2, cy = w->y + w->h / 2;
                    int best_dist = 999999;
                    for (int i = 0; i < desktop_window_count(); i++) {
                        if (i == fw || !desktop_get_window(i)->visible) continue;
                        window_t *ow = desktop_get_window(i);
                        int ox = ow->x + ow->w / 2, oy = ow->y + ow->h / 2;
                        int ok = 0;
                        if (key == KEY_UP && oy < cy) ok = 1;
                        if (key == KEY_DOWN && oy > cy) ok = 1;
                        if (key == KEY_LEFT && ox < cx) ok = 1;
                        if (key == KEY_RIGHT && ox > cx) ok = 1;
                        if (ok) {
                            int d = (ox - cx) * (ox - cx) + (oy - cy) * (oy - cy);
                            if (d < best_dist) { best_dist = d; ti = i; }
                        }
                    }
                    if (ti >= 0) {
                        desktop_set_focused(ti);
                        wm_apply_layout();
                    }
                } else {
                    int fw = desktop_focused_window();
                    if (fw < 0) break;
                    window_t *w = desktop_get_window(fw);
                    if (!w) break;
                    int step = 10;
                    switch (key) {
                        case KEY_UP:    w->y -= step; break;
                        case KEY_DOWN:  w->y += step; break;
                        case KEY_LEFT:  w->x -= step; break;
                        case KEY_RIGHT: w->x += step; break;
                    }
                    w->maximized = 0;
                    desktop_redraw();
                }
                break;
            }
            default: break;
        }
        return;
    }

    if (ctrl) {
        switch (key) {
            case 'q' - 'a' + 1: {
                int fw = desktop_focused_window();
                if (fw >= 0) desktop_close_window(fw);
                wm_apply_layout();
                break;
            }
            case 'd' - 'a' + 1: {
                extern void desktop_toggle_start(void);
                desktop_toggle_start();
                break;
            }
            default: break;
        }
        return;
    }

    /* Super key — toggle Launchpad */
    if (key == KEY_SUPER) {
        extern void launchpad_toggle(void);
        launchpad_toggle();
        desktop_redraw();
        return;
    }

    /* F6 — toggle FreeCode AI overlay (Spotlight-style) */
    if (key == KEY_F6) {
        extern void ai_overlay_toggle(void);
        ai_overlay_toggle();
        desktop_redraw();
        return;
    }
}
