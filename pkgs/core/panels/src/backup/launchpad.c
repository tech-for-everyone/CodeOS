#include "launchpad.h"
#include "panels.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
#include "pixelman.h"

#define LP_PAD_X        40
#define LP_PAD_TOP      80
#define LP_PAD_BOT      40
#define LP_ICON_SZ      72
#define LP_GAP_X        16
#define LP_GAP_Y        24
#define LP_LABEL_H      18
#define LP_SEARCH_W     360
#define LP_SEARCH_H     36
#define LP_CAT_H        28
#define LP_CAT_PAD      8
#define LP_DOT_R        5
#define LP_DOT_GAP      20
#define LP_ANIM_MS      200
#define LP_COLS         6

#define LP_BG           0xCC141416
#define LP_GLASS_TOP    0x18FFFFFF
#define LP_SEARCH_BG    0x882C2C2E
#define LP_SEARCH_BDR   0x440A84FF
#define LP_HOVER_BG     0x22FFFFFF
#define LP_LABEL_FG     0xFFF5F5F7
#define LP_DIM_FG       0xFF8E8E93
#define LP_DOT_ON       0xFF0A84FF
#define LP_DOT_OFF      0x668E8E93

#define LP_CAT_ALL      0
#define LP_CAT_SYSTEM   1
#define LP_CAT_DEV      2
#define LP_CAT_NET      3
#define LP_CAT_MEDIA    4
#define LP_CAT_ANDROID  5
#define LP_CAT_GAMES    6
#define LP_CAT_COUNT    7

static const char *lp_cat_names[LP_CAT_COUNT] = {
    "All Apps", "System", "Development", "Network", "Media", "Android", "Games"
};
static const uint32_t lp_cat_colors[LP_CAT_COUNT] = {
    C_SKY, C_GREEN, C_PEACH, C_BLUE, C_PINK, C_GREEN, C_MAUVE
};

static int lp_open;
static int lp_page;
static int lp_cat;
static int lp_cat_hover;
static int lp_hover_idx;
static int lp_search_focused;
static char lp_search[48];
static int lp_search_len;
static int lp_anim_start;
static int lp_bounce_app;
static uint64_t lp_bounce_t0;
static int lp_pending_app;

static int lp_rows;
static int lp_cell_w, lp_cell_h;
static int lp_grid_x, lp_grid_y;
static int lp_apps_per_page;
static int lp_total_pages;

static int lp_all_indices[APP_COUNT_MAX];
static int lp_all_count;
static int lp_filtered[APP_COUNT_MAX];
static int lp_filtered_count;

extern const char *app_names[APP_COUNT_MAX];
extern int app_is_system[APP_COUNT_MAX];
extern uint32_t *icon_buf_launch[APP_COUNT_MAX];

static int cat_from_app(int idx) {
    const char *n = app_names[idx];
    if (n[0] == 'T' && n[1] == 'e') return LP_CAT_SYSTEM;
    if (n[0] == 'S' && n[1] == 'y') return LP_CAT_SYSTEM;
    if (n[0] == 'E' && n[1] == 'x') return LP_CAT_SYSTEM;
    if (n[0] == 'D' && n[1] == 'e') return LP_CAT_MEDIA;
    if (n[0] == 'O' && n[1] == 'p') return LP_CAT_NET;
    if (n[0] == 'F' && n[1] == 'r') return LP_CAT_DEV;
    if (n[0] == 'A' && n[1] == 'b') return LP_CAT_SYSTEM;
    if (n[0] == 'C' && n[1] == 'a') return LP_CAT_DEV;
    if (n[0] == 'S' && n[1] == 'e') return LP_CAT_MEDIA;
    return LP_CAT_ALL;
}

static int match_search(const char *name) {
    if (!lp_search[0]) return 1;
    const char *q = lp_search;
    while (*q && *name) {
        char a = *name, b = *q;
        if (a >= 'A' && a <= 'Z') a += 0x20;
        if (b >= 'A' && b <= 'Z') b += 0x20;
        if (a != b) return 0;
        name++; q++;
    }
    return !*q;
}

static void rebuild_filtered(void) {
    lp_filtered_count = 0;
    for (int i = 0; i < lp_all_count; i++) {
        int idx = lp_all_indices[i];
        if (lp_cat != LP_CAT_ALL && cat_from_app(idx) != lp_cat) continue;
        if (!match_search(app_names[idx])) continue;
        lp_filtered[lp_filtered_count++] = idx;
    }
}

static void calc_layout(uint32_t sw, uint32_t sh) {
    int avail_w = (int)sw - LP_PAD_X * 2;
    lp_cell_w = (avail_w + LP_GAP_X) / LP_COLS;
    lp_cell_h = LP_ICON_SZ + LP_LABEL_H + LP_GAP_Y;
    lp_grid_x = ((int)sw - LP_COLS * lp_cell_w) / 2;

    int top_h = LP_PAD_TOP + LP_SEARCH_H + 32 + LP_CAT_H + LP_CAT_PAD * 2 + 16;
    int dot_h = 30;
    int avail_h = (int)sh - top_h - dot_h - LP_PAD_BOT;
    lp_rows = avail_h / lp_cell_h;
    if (lp_rows < 1) lp_rows = 1;
    if (lp_rows > 5) lp_rows = 5;

    lp_grid_y = top_h;
    lp_apps_per_page = LP_COLS * lp_rows;
    if (lp_apps_per_page < 1) lp_apps_per_page = 1;

    rebuild_filtered();
    lp_total_pages = (lp_filtered_count + lp_apps_per_page - 1) / lp_apps_per_page;
    if (lp_total_pages < 1) lp_total_pages = 1;
    if (lp_page >= lp_total_pages) lp_page = lp_total_pages - 1;
}

static int idx_at(int col, int row) {
    int i = lp_page * lp_apps_per_page + row * LP_COLS + col;
    if (i >= lp_filtered_count) return -1;
    return lp_filtered[i];
}

static int hit_test(int mx, int my, uint32_t sw, uint32_t sh) {
    if (!lp_open) return -1;
    calc_layout(sw, sh);
    for (int r = 0; r < lp_rows; r++)
        for (int c = 0; c < LP_COLS; c++) {
            int ai = idx_at(c, r);
            if (ai < 0) continue;
            int ix = lp_grid_x + c * lp_cell_w + (lp_cell_w - LP_ICON_SZ) / 2;
            int iy = lp_grid_y + r * lp_cell_h;
            if (mx >= ix && mx < ix + LP_ICON_SZ && my >= iy && my < iy + LP_ICON_SZ + LP_LABEL_H + 4)
                return ai;
        }
    return -1;
}

static int hit_category(int mx, int my, uint32_t sw) {
    int cat_y = LP_PAD_TOP + LP_SEARCH_H + 32 + LP_CAT_PAD;
    int cx = (int)sw / 2;
    int total_w = 0;
    for (int i = 0; i < LP_CAT_COUNT; i++)
        total_w += (int)strlen(lp_cat_names[i]) * 7 + 30;
    cx -= total_w / 2;
    for (int i = 0; i < LP_CAT_COUNT; i++) {
        int tw = (int)strlen(lp_cat_names[i]) * 7 + 24;
        if (mx >= cx && mx < cx + tw && my >= cat_y && my < cat_y + LP_CAT_H) return i;
        cx += tw + 6;
    }
    return -1;
}

static void draw_icon(uint32_t *buf, int sz, int dx, int dy, int max_sz) {
    if (!buf) return;
    uint32_t *fb = fb_get_active_buffer();
    int stride = fb_get_pitch() / 4;
    for (int r = 0; r < sz && r < max_sz; r++)
        for (int c = 0; c < sz && c < max_sz; c++) {
            uint32_t src = buf[r * sz + c];
            uint8_t a = (src >> 24);
            if (a == 0) continue;
            int fx = dx + c, fy = dy + r;
            if (fx < 0 || fy < 0 || fx >= (int)fb_getwidth() || fy >= (int)fb_getheight()) continue;
            uint32_t *p = fb + fy * stride + fx;
            *p = pixelman_blend_over(a < 255 ? (src & 0x00FFFFFF) | ((a * 9 / 10) << 24) : src, *p);
        }
}

int launchpad_init(void) {
    lp_open = 0; lp_page = 0; lp_cat = LP_CAT_ALL;
    lp_cat_hover = -1; lp_hover_idx = -1;
    lp_search_focused = 0; lp_search[0] = 0; lp_search_len = 0;
    lp_anim_start = 0; lp_bounce_app = -1; lp_pending_app = -1;
    lp_all_count = 0;
    for (int i = 0; i < APP_COUNT_MAX; i++)
        lp_all_indices[lp_all_count++] = i;
    for (int i = 0; i < lp_all_count - 1; i++)
        for (int j = 0; j < lp_all_count - 1 - i; j++)
            if (strcmp(app_names[lp_all_indices[j]], app_names[lp_all_indices[j+1]]) > 0) {
                int t = lp_all_indices[j];
                lp_all_indices[j] = lp_all_indices[j+1];
                lp_all_indices[j+1] = t;
            }
    return 1;
}

int launchpad_is_open(void) { return lp_open; }

void launchpad_toggle(void) {
    lp_open = !lp_open;
    lp_page = 0; lp_cat = LP_CAT_ALL;
    lp_hover_idx = -1; lp_search[0] = 0; lp_search_len = 0;
    lp_search_focused = 0; lp_bounce_app = -1; lp_pending_app = -1;
    lp_anim_start = (int)timer_get_milliseconds();
}

void launchpad_close(void) {
    lp_open = 0; lp_hover_idx = -1;
    lp_bounce_app = -1; lp_pending_app = -1;
}

void launchpad_draw(uint32_t scr_w, uint32_t scr_h) {
    if (!lp_open) return;
    calc_layout(scr_w, scr_h);

    fb_fillrect(0, 0, scr_w, scr_h, LP_BG);
    fb_fillrect_gradient_v(0, 0, scr_w, scr_h / 3, LP_GLASS_TOP, 0x00000000);
    fb_fillrect_alpha(0, 1, scr_w, 1, 0xFFFFFF, 8);

    /* Search bar */
    int sb_x = ((int)scr_w - LP_SEARCH_W) / 2;
    int sb_y = LP_PAD_TOP;
    if (lp_search_focused)
        fb_fill_rounded_rect(sb_x - 3, sb_y - 3, LP_SEARCH_W + 6, LP_SEARCH_H + 6,
                             LP_SEARCH_H / 2 + 3, 0x280A84FF);
    fb_fill_rounded_rect(sb_x, sb_y, LP_SEARCH_W, LP_SEARCH_H,
                         LP_SEARCH_H / 2, LP_SEARCH_BG);
    fb_draw_rounded_rect(sb_x, sb_y, LP_SEARCH_W, LP_SEARCH_H,
                         LP_SEARCH_H / 2, lp_search_focused ? C_BLUE : LP_SEARCH_BDR);

    if (lp_search[0]) {
        fb_drawstr_px(sb_x + 20, sb_y + 10, lp_search, C_TEXT, 0);
        uint64_t now = timer_get_milliseconds();
        if (((now / 500) & 1))
            fb_fillrect(sb_x + 20 + lp_search_len * 8 + 2, sb_y + 10, 1, 15, C_TEXT);
    } else {
        fb_drawstr_px(sb_x + 20, sb_y + 10, "Search Launchpad...", LP_DIM_FG, 0);
    }

    /* Category pills */
    int cat_y = sb_y + LP_SEARCH_H + 32 + LP_CAT_PAD;
    int total_cat_w = 0;
    for (int i = 0; i < LP_CAT_COUNT; i++)
        total_cat_w += (int)strlen(lp_cat_names[i]) * 7 + 30;
    int cx = ((int)scr_w - total_cat_w) / 2;
    for (int i = 0; i < LP_CAT_COUNT; i++) {
        int tw = (int)strlen(lp_cat_names[i]) * 7 + 24;
        int is_sel = (i == lp_cat);
        uint32_t bg = is_sel ? lp_cat_colors[i] : 0x22FFFFFF;
        fb_fill_rounded_rect(cx, cat_y, tw, LP_CAT_H, LP_CAT_H / 2, bg);
        uint32_t fg = is_sel ? C_CRUST : C_TEXT;
        fb_drawstr_px(cx + 12, cat_y + (LP_CAT_H - 14) / 2, lp_cat_names[i], fg, bg);
        cx += tw + 6;
    }

    /* App grid */
    for (int r = 0; r < lp_rows; r++)
        for (int c = 0; c < LP_COLS; c++) {
            int ai = idx_at(c, r);
            if (ai < 0) continue;
            int ix = lp_grid_x + c * lp_cell_w + (lp_cell_w - LP_ICON_SZ) / 2;
            int iy = lp_grid_y + r * lp_cell_h;
            int is_sel = (ai == lp_hover_idx);

            int bsz = LP_ICON_SZ, boff = 0;
            if (ai == lp_bounce_app) {
                uint64_t now = timer_get_milliseconds();
                uint64_t dt = now - lp_bounce_t0;
                if (dt < 250) {
                    int t = (int)(dt * 256 / 250);
                    int f = (4 * 12 * t * (256 - t)) / (256 * 256);
                    bsz += f; boff = f / 2;
                }
            }

            int tx = ix - boff, ty = iy - boff;

            if (is_sel)
                fb_fill_rounded_rect(tx - 6, ty - 6, bsz + 12, bsz + 12, 14, LP_HOVER_BG);

            int icon_off = (bsz - 48) / 2;
            if (icon_buf_launch[ai])
                draw_icon(icon_buf_launch[ai], 48, tx + icon_off, ty + icon_off, 48);

            const char *nm = app_names[ai];
            int nl = (int)strlen(nm);
            int tw = nl * 7;
            int lx = lp_grid_x + c * lp_cell_w + (lp_cell_w - tw) / 2;
            int ly = ty + bsz + 6;
            fb_drawstr_px(lx, ly, nm, LP_LABEL_FG, 0);
        }

    /* Page dots */
    if (lp_total_pages > 1) {
        int dw = lp_total_pages * LP_DOT_GAP;
        int dx = ((int)scr_w - dw) / 2;
        int dy = (int)scr_h - LP_PAD_BOT - 16;
        for (int p = 0; p < lp_total_pages; p++) {
            uint32_t col = (p == lp_page) ? LP_DOT_ON : LP_DOT_OFF;
            fb_fill_rounded_rect(dx + p * LP_DOT_GAP - LP_DOT_R, dy - LP_DOT_R,
                                 LP_DOT_R * 2, LP_DOT_R * 2, LP_DOT_R, col);
        }
    }
}

int launchpad_update(uint64_t now_ms) {
    if (lp_bounce_app >= 0 && now_ms - lp_bounce_t0 >= 250) {
        lp_pending_app = lp_bounce_app;
        lp_bounce_app = -1;
    }
    if (lp_pending_app >= 0) {
        int r = lp_pending_app;
        lp_pending_app = -1;
        return r;
    }
    return -1;
}

int launchpad_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    if (!lp_open) return -1;

    /* Search bar click */
    int sb_x = ((int)scr_w - LP_SEARCH_W) / 2;
    int sb_y = LP_PAD_TOP;
    if (mx >= sb_x && mx < sb_x + LP_SEARCH_W && my >= sb_y && my < sb_y + LP_SEARCH_H) {
        lp_search_focused = 1;
        return -1;
    }

    /* Category click */
    int cat_hit = hit_category(mx, my, scr_w);
    if (cat_hit >= 0) {
        lp_cat = cat_hit;
        lp_page = 0;
        lp_hover_idx = -1;
        return -1;
    }

    /* App click */
    int item = hit_test(mx, my, scr_w, scr_h);
    if (item >= 0) {
        lp_bounce_app = item;
        lp_bounce_t0 = timer_get_milliseconds();
        lp_search_focused = 0;
        return item;
    }

    /* Click outside = close */
    lp_search_focused = 0;
    launchpad_close();
    return -1;
}

int launchpad_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    if (!lp_open) return 0;
    int h = hit_test(mx, my, scr_w, scr_h);
    if (h != lp_hover_idx) { lp_hover_idx = h; return 1; }
    return 0;
}

int launchpad_key(int key) {
    if (!lp_open) return 0;

    if (key == 0x1B) {
        if (lp_search_len > 0) {
            lp_search_len = 0; lp_search[0] = 0;
        } else {
            launchpad_close();
        }
        return 1;
    }
    if (key == 0x09) {
        lp_page++;
        if (lp_page >= lp_total_pages) lp_page = 0;
        return 1;
    }

    if (lp_search_focused) {
        if (key == '\b' && lp_search_len > 0) {
            lp_search_len--;
            lp_search[lp_search_len] = 0;
            lp_page = 0;
        } else if (key >= ' ' && lp_search_len < 46) {
            lp_search[lp_search_len++] = (char)key;
            lp_search[lp_search_len] = 0;
            lp_page = 0;
        }
        return 1;
    }

    if (key == '/' || key == 's' || key == 'S') {
        lp_search_focused = 1;
        return 1;
    }

    return 1;
}
