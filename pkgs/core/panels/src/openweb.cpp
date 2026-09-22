extern "C" {
#include "openweb.h"
#include "ow_http.h"
#include "ow_html.h"
#include "desktop.h"
#include "windows.h"
#include "string.h"
#include "keyboard.h"
#include "fb.h"
#include "timer.h"
#include "freecode_gui.h"
}

/* ══════════════════════════════════════════════════════════════════════
   OpenWeb C++ Frontend — WhiteSur-KDE / macOS toolbar style
   Uses Rust backend (ow_http) for HTTP + search engine routing
   Uses ow_html for HTML→text rendering
   ══════════════════════════════════════════════════════════════════════ */

#define OW_TAB_H        32
#define OW_ADDR_BAR_H   34
#define OW_STATUS_H     20
#define OW_TOOLBAR_H    38
#define OW_BOOKMARKS_H  24
#define OW_MAX_BOOKMARKS 16

/* ── Big Sur WhiteSur color palette ── */
#define C_OW_BG         0xFFF5F5F7
#define C_OW_TOOLBAR    0xDDD6D8E0
#define C_OW_TOOLBARBot 0xCCBFC1CC
#define C_OW_TAB_BG     0x33C8C8D0
#define C_OW_TAB_ACT    0xFFEEEEF2
#define C_OW_URL_BG     0xFFFFFFFF
#define C_OW_URL_BORDER 0x44AAAAAA
#define C_OW_TEXT       0xFF1D1D1F
#define C_OW_SUBTEXT    0xFF86868B
#define C_OW_BG_DARK    0xFFE8E8ED
#define C_OW_BOOK_BG    0xFFEDEDF0
#define C_OW_STATUS     0xFFF0F0F4
#define C_OW_SHADOW     0x22000000

/* ── IP-only navigation (DNS is disabled in the kernel) ── */

static int ow_is_ip_literal(const char *s) {
    if (!s || !s[0]) return 0;
    int segs = 0, digits = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '.') {
            if (digits == 0 || digits > 3) return 0;
            segs++; digits = 0;
        } else if (s[i] >= '0' && s[i] <= '9') {
            digits++;
            if (digits > 3) return 0;
        } else {
            return 0;
        }
    }
    if (digits == 0 || digits > 3) return 0;
    return segs == 3;
}

static int ow_is_url(const char *s) {
    if (!s || !s[0]) return 0;
    if (strncmp(s, "http://", 7) == 0) return 1;
    if (strncmp(s, "file://", 7) == 0) return 1;
    if (strncmp(s, "about:", 6) == 0) return 1;
    return ow_is_ip_literal(s);
}

/* ── URL normalization (IP-only stack) ── */
static void ow_normalize_url(const char *input, char *output, int max_len) {
    const char *trimmed = input;
    while (*trimmed == ' ') trimmed++;

    if (strncmp(trimmed, "http://", 7) == 0) {
        strlcpy(output, trimmed, max_len);
    } else {
        /* Bare numeric IP or path → http:// form */
        snprintf(output, max_len, "http://%s", trimmed);
    }
}

/* ── Frontend-local tab state (mirrors Rust backend) ── */
struct OwTab {
    char title[64];
    char url[OW_URL_MAX];
    int  loading;
    int  hist_pos;
    int  hist_len;
    char hist_urls[32][OW_URL_MAX];
    int  search_engine; /* 0=google, 1=duckduckgo */
};

static struct {
    int open;
    int win;
    OwTab tabs[OW_MAX_TABS];
    int  tab_count;
    int  active;

    char addr_buf[512];
    int  addr_len;
    int  addr_cursor;
    int  addr_focused;
    int  scroll_y;

    int  bookmark_count;
    char bookmark_names[OW_MAX_BOOKMARKS][32];
    char bookmark_urls[OW_MAX_BOOKMARKS][OW_URL_MAX];
    int  show_bookmarks;
} ow;

/* ── Bookmarks ── */

static void bookmark_add(const char *name, const char *url) {
    if (ow.bookmark_count >= OW_MAX_BOOKMARKS) return;
    for (int i = 0; i < ow.bookmark_count; i++)
        if (strcmp(ow.bookmark_urls[i], url) == 0) return;
    strncpy_safe(ow.bookmark_names[ow.bookmark_count], name, 32);
    strncpy_safe(ow.bookmark_urls[ow.bookmark_count], url, OW_URL_MAX);
    ow.bookmark_count++;
}

static void bookmark_defaults(void) {
    bookmark_add("CodeOS", "http://localhost");
}

/* ── Title from URL ── */

static void ow_set_title(int t, const char *u) {
    char buf[64];
    int i = 0, dot = 0;
    for (int j = 0; u[j] && j < 63; j++) {
        if (u[j] == '/' && dot) break;
        if (u[j] == '.') dot = 1;
        buf[i++] = u[j];
    }
    buf[i] = 0;
    sprintf(ow.tabs[t].title, "%s", buf[0] ? buf : "New Tab");
}

/* ── History ── */

static void ow_hist_push(int t, const char *url) {
    if (t < 0 || t >= ow.tab_count) return;
    if (!url || !url[0]) return;
    if (ow.tabs[t].hist_len > 0 &&
        strcmp(ow.tabs[t].hist_urls[ow.tabs[t].hist_len - 1], url) == 0)
        return;
    if (ow.tabs[t].hist_pos < ow.tabs[t].hist_len - 1)
        ow.tabs[t].hist_len = ow.tabs[t].hist_pos + 1;
    if (ow.tabs[t].hist_len < 32) {
        strlcpy(ow.tabs[t].hist_urls[ow.tabs[t].hist_len], url, OW_URL_MAX);
        ow.tabs[t].hist_len++;
    } else {
        for (int i = 0; i < 31; i++)
            strlcpy(ow.tabs[t].hist_urls[i], ow.tabs[t].hist_urls[i+1], OW_URL_MAX);
        strlcpy(ow.tabs[t].hist_urls[31], url, OW_URL_MAX);
    }
    ow.tabs[t].hist_pos = ow.tabs[t].hist_len - 1;
}

/* ── Tab management ── */

static int ow_new_tab(const char *url) {
    if (ow.tab_count >= OW_MAX_TABS) return -1;
    int t = ow.tab_count++;
    strcpy(ow.tabs[t].title, "New Tab");
    strlcpy(ow.tabs[t].url, url ? url : "about:blank", OW_URL_MAX);
    ow.tabs[t].loading = 0;
    ow.tabs[t].hist_pos = -1;
    ow.tabs[t].hist_len = 0;
    ow.active = t;
    ow_tab_new(url);
    return t;
}

static void ow_close_tab(int idx) {
    if (idx < 0 || idx >= ow.tab_count) return;
    for (int i = idx; i < ow.tab_count - 1; i++)
        ow.tabs[i] = ow.tabs[i + 1];
    ow.tab_count--;
    if (ow.active >= ow.tab_count) ow.active = ow.tab_count - 1;
    if (ow.active < 0) ow.active = 0;
    ow_tab_close(idx);
}

/* ── Navigation ── */

static void ow_load_url(const char *url) {
    int t = ow.active;
    strlcpy(ow.tabs[t].url, url, OW_URL_MAX);
    ow.tabs[t].loading = 1;
    ow_set_title(t, url);
    ow_hist_push(t, url);
    ow_set_tab_active(t);
    ow_navigate(url);
    desktop_redraw();
}

static void ow_go(void) {
    ow.addr_buf[ow.addr_len] = 0;
    char input[512];
    int j = 0;
    for (int i = 0; ow.addr_buf[i] && i < 511; i++) {
        if (ow.addr_buf[i] != ' ') input[j++] = ow.addr_buf[i];
    }
    input[j] = 0;
    if (!input[0]) return;

    int t = ow.active;
    ow.addr_focused = 0;

    if (ow_is_url(input)) {
        char normalized[OW_URL_MAX];
        ow_normalize_url(input, normalized, sizeof(normalized));
        ow_load_url(normalized);
    } else {
        /* Search query → route through Rust backend */
        strlcpy(ow.tabs[t].url, input, OW_URL_MAX);
        ow.tabs[t].loading = 1;
        ow_set_title(t, input);
        ow_hist_push(t, input);
        ow_set_tab_active(t);
        ow_search(input);
        desktop_redraw();
    }
}

static void ow_go_to_hist(int dir) {
    int t = ow.active;
    int np = ow.tabs[t].hist_pos + dir;
    if (np < 0 || np >= ow.tabs[t].hist_len) return;
    ow.tabs[t].hist_pos = np;
    ow_load_url(ow.tabs[t].hist_urls[np]);
}

/* ── Sync state from Rust backend ── */

static void ow_sync_tabs(void) {
    openweb_tab_t *rtabs = ow_get_tabs();
    int rcount = ow_get_tab_count();
    int ractive = ow_get_tab_active();

    if (rcount > 0 && rcount <= OW_MAX_TABS) {
        ow.tab_count = rcount;
        ow.active = ractive;
        for (int i = 0; i < rcount; i++) {
            ow.tabs[i].loading = rtabs[i].loading;
            strlcpy(ow.tabs[i].url, rtabs[i].url, OW_URL_MAX);
            if (rtabs[i].content_len > 0) {
                render_html(rtabs[i].content, rtabs[i].content_len);
                ow_need_render = 1;
                ow.tabs[i].loading = 0;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   PUBLIC API (extern "C" — called from desktop.cpp)
   ══════════════════════════════════════════════════════════════════════ */

int openweb_open(void) {
    if (ow.open) return 1;
    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    int ww = sw * 7 / 8, wh = sh * 5 / 6;
    ow.win = desktop_new_window((sw - ww) / 2, (sh - wh) / 4,
                                ww, wh, "OpenWeb", 0xFFFFFFFF, C_OW_BG);
    if (ow.win < 0) return 0;
    ow.open = 1;
    ow.tab_count = 0;
    ow.active = 0;
    ow.addr_len = 0;
    ow.addr_cursor = 0;
    ow.addr_focused = 0;
    ow.scroll_y = 0;
    ow.show_bookmarks = 1;
    ow.bookmark_count = 0;
    bookmark_defaults();
    ow_new_tab("about:blank");
    desktop_redraw();
    return 1;
}

int openweb_is_open(void) { return ow.open; }
int openweb_is_focused(void) { return ow.open; }

void openweb_close(void) {
    ow.open = 0;
    desktop_close_window(ow.win);
}

/* ── Hover state tracking ── */
static int ow_hover_btn = 0;  /* 0=none, 1=back, 2=forward, 3=home, 4=reload, 5=ai */
static int ow_hover_tab = -1; /* -1=none, else tab index */
static int ow_btn_x[5], ow_btn_y, ow_btn_w, ow_btn_h;
static int ow_tab_x[OW_MAX_TABS], ow_tab_w_arr[OW_MAX_TABS];

/* ══════════════════════════════════════════════════════════════════════
   DRAW — WhiteSur-KDE macOS-style toolbar
   ══════════════════════════════════════════════════════════════════════ */

void openweb_draw(void) {
    if (!ow.open) return;
    window_t *w = desktop_get_window(ow.win);
    if (!w || !w->visible) return;

    /* Sync with Rust backend */
    ow_sync_tabs();

    int cx = w->x + 4, cy = w->y + 22;
    int cw = w->w - 8, ch = w->h - 26;
    if (cw < 80 || ch < 80) return;

    /* ── Window background (light theme) ── */
    fb_fill_rounded_rect(cx, cy, cw, ch, 10, C_OW_BG);

    int toolbar_h = OW_TOOLBAR_H + OW_TAB_H;
    if (ow.show_bookmarks) toolbar_h += OW_BOOKMARKS_H;
    int content_y = cy + toolbar_h;
    int content_h = ch - toolbar_h - OW_STATUS_H;

    /* ── Tab bar (WhiteSur style — frosted glass) ── */
    fb_fillrect_gradient_v(cx, cy, cw, OW_TAB_H, 0xDDD6D8E0, 0xCCBFC1CC);
    fb_fillrect_alpha(cx, cy, cw, 1, 0xFFFFFF, 20);

    int tx = cx + 10;
    for (int i = 0; i < ow.tab_count; i++) {
        int tw = 140;
        if (tw > (cw - 100) / (ow.tab_count > 0 ? ow.tab_count : 1))
            tw = (cw - 100) / (ow.tab_count > 0 ? ow.tab_count : 1);
        if (tw < 60) tw = 60;

        ow_tab_x[i] = tx;
        ow_tab_w_arr[i] = tw;

        if (i == ow.active) {
            fb_fill_rounded_rect(tx, cy + 3, tw, OW_TAB_H - 4, 8, C_OW_TAB_ACT);
            fb_draw_rounded_rect(tx, cy + 3, tw, OW_TAB_H - 4, 8, 0x330066FF);
            fb_draw_rounded_rect(tx - 1, cy + 2, tw + 2, OW_TAB_H - 2, 9, C_FOCUS_GLOW);
        } else if (i == ow_hover_tab) {
            fb_fill_rounded_rect(tx, cy + 3, tw, OW_TAB_H - 4, 8, C_GLASS_HI);
        } else {
            fb_fill_rounded_rect(tx, cy + 3, tw, OW_TAB_H - 4, 8, C_OW_TAB_BG);
        }

        /* Tab title */
        int title_max = tw - 20;
        char truncated[64];
        int ti = 0;
        while (ow.tabs[i].title[ti] && ti < 60 && fb_text_width(truncated) < title_max) {
            truncated[ti] = ow.tabs[i].title[ti];
            ti++;
        }
        truncated[ti] = 0;

        uint32_t tcol = (i == ow.active) ? C_BLUE : C_OW_TEXT;
        fb_drawstr_px(tx + 10, cy + (OW_TAB_H - 14) / 2, truncated, tcol, 0);

        /* Loading indicator */
        if (ow.tabs[i].loading) {
            int spin = (timer_get_ticks() / 80) % 4;
            static const char *sp = "-\\|/";
            char sc[2] = {sp[spin], 0};
            fb_drawstr_px(tx + tw - 16, cy + (OW_TAB_H - 14) / 2, sc, C_GREEN, 0);
        }

        /* Close button on active tab */
        if (i == ow.active && ow.tab_count > 1) {
            fb_drawstr_px(tx + tw - 14, cy + (OW_TAB_H - 14) / 2, "x", C_OW_SUBTEXT, 0);
        }

        tx += tw + 2;
    }

    /* New tab button */
    if (ow.tab_count < OW_MAX_TABS) {
        fb_fill_rounded_rect(tx, cy + 6, 24, 20, 6, C_OW_TAB_BG);
        fb_drawstr_px(tx + 8, cy + (OW_TAB_H - 14) / 2, "+", C_BLUE, 0);
    }

    /* ── Address bar / toolbar ── */
    int aby = cy + OW_TAB_H;
    fb_fillrect_gradient_v(cx, aby, cw, OW_TOOLBAR_H, C_OW_TOOLBAR, C_OW_TOOLBARBot);
    fb_fillrect_alpha(cx, aby, cw, 1, 0xFFFFFF, 16);

    /* Search engine icon in URL bar */
    int url_x = cx + 12;
    int url_w = cw - 180;
    fb_fill_rounded_rect(url_x, aby + 6, url_w, OW_TOOLBAR_H - 12, 10, C_OW_URL_BG);
    fb_draw_rounded_rect(url_x, aby + 6, url_w, OW_TOOLBAR_H - 12, 10,
                         ow.addr_focused ? C_BLUE : C_OW_URL_BORDER);

    /* Search icon (magnifying glass placeholder) */
    const char *engine_icon = ow.tabs[ow.active].search_engine == 0 ? "G" : "D";
    fb_drawstr_px(url_x + 8, aby + (OW_TOOLBAR_H - 14) / 2, engine_icon, C_OW_SUBTEXT, C_OW_URL_BG);

    /* URL / search text */
    char addr_disp[520];
    if (ow.addr_focused) {
        int ai;
        for (ai = 0; ai < ow.addr_len; ai++) addr_disp[ai] = ow.addr_buf[ai];
        addr_disp[ai] = 0;
    } else {
        strlcpy(addr_disp, ow.tabs[ow.active].url, sizeof(addr_disp));
    }
    uint32_t addr_col = ow.addr_focused ? C_OW_TEXT : C_OW_SUBTEXT;
    fb_drawstr_px(url_x + 24, aby + (OW_TOOLBAR_H - 14) / 2, addr_disp, addr_col, C_OW_URL_BG);

    /* Back / Forward / Home / Reload buttons */
    int btn_x = cx + cw - 160;
    int btn_y = aby + 6;
    int btn_h = OW_TOOLBAR_H - 12;

    ow_btn_x[0] = btn_x;       ow_btn_y = btn_y;
    ow_btn_x[1] = btn_x + 34;  ow_btn_w = 30; ow_btn_h = btn_h;
    ow_btn_x[2] = btn_x + 68;
    ow_btn_x[3] = btn_x + 102;
    ow_btn_x[4] = btn_x + 140;

    uint32_t btn_bg;
    /* Back */
    btn_bg = (ow_hover_btn == 1) ? C_GLASS_HI : C_OW_TAB_BG;
    fb_fill_rounded_rect(btn_x, btn_y, 30, btn_h, 8, btn_bg);
    fb_drawstr_px(btn_x + 10, aby + (OW_TOOLBAR_H - 14) / 2, "<", C_BLUE, 0);

    /* Forward */
    btn_bg = (ow_hover_btn == 2) ? C_GLASS_HI : C_OW_TAB_BG;
    fb_fill_rounded_rect(btn_x + 34, btn_y, 30, btn_h, 8, btn_bg);
    fb_drawstr_px(btn_x + 44, aby + (OW_TOOLBAR_H - 14) / 2, ">", C_BLUE, 0);

    /* Home */
    btn_bg = (ow_hover_btn == 3) ? C_GLASS_HI : C_OW_TAB_BG;
    fb_fill_rounded_rect(btn_x + 68, btn_y, 30, btn_h, 8, btn_bg);
    fb_drawstr_px(btn_x + 78, aby + (OW_TOOLBAR_H - 14) / 2, "^", C_BLUE, 0);

    /* Reload / Stop */
    if (ow.tabs[ow.active].loading) {
        fb_fill_rounded_rect(btn_x + 102, btn_y, 30, btn_h, 8, 0xFFEEEEEE);
        fb_drawstr_px(btn_x + 112, aby + (OW_TOOLBAR_H - 14) / 2, "x", C_RED, 0);
    } else {
        btn_bg = (ow_hover_btn == 4) ? C_GLASS_HI : C_OW_TAB_BG;
        fb_fill_rounded_rect(btn_x + 102, btn_y, 30, btn_h, 8, btn_bg);
        fb_drawstr_px(btn_x + 110, aby + (OW_TOOLBAR_H - 14) / 2, "~", C_BLUE, 0);
    }

    /* AI Summarize button */
    {
        uint32_t ai_bg = (ow_hover_btn == 5) ? 0x33BF5AF2 : C_OW_TAB_BG;
        fb_fill_rounded_rect(btn_x + 140, btn_y, 30, btn_h, 8, ai_bg);
        fb_drawstr_px(btn_x + 148, aby + (OW_TOOLBAR_H - 14) / 2, "AI", C_MAUVE, 0);
    }

    /* ── Bookmarks bar ── */
    if (ow.show_bookmarks) {
        int bby = aby + OW_TOOLBAR_H;
        fb_fillrect(cx, bby, cw, OW_BOOKMARKS_H, C_OW_BOOK_BG);
        fb_fillrect_alpha(cx, bby, cw, 1, 0xFFFFFF, 10);

        int bm_count = ow.bookmark_count;
        if (bm_count > 0) {
            int bmw = (cw - 16) / bm_count;
            if (bmw > 120) bmw = 120;
            for (int i = 0; i < bm_count; i++) {
                int bmx = cx + 8 + i * (bmw + 2);
                fb_fill_rounded_rect(bmx, bby + 4, bmw, OW_BOOKMARKS_H - 8, 6, 0x33C8C8D0);
                fb_drawstr_px(bmx + 6, bby + 4, ow.bookmark_names[i], C_OW_TEXT, 0);
            }
        }
    }

    /* ── Content area ── */
    fb_fillrect(cx, content_y, cw, content_h, C_OW_BG);

    if (ow_need_render && ow_txt_lines > 0) {
        /* Rendered HTML content */
        int ry = content_y + 4 - ow.scroll_y;
        for (int ln = 0; ln < ow_txt_lines; ln++) {
            if (ry + 16 > content_y && ry < content_y + content_h) {
                uint32_t fg = C_OW_TEXT;
                uint32_t bg = C_OW_BG;
                int indent = 0;

                switch (ow_line_info[ln].type) {
                    case OW_LT_H1: fg = C_BLUE; indent = 0; break;
                    case OW_LT_H2: fg = 0xFF333333; indent = 0; break;
                    case OW_LT_H3: fg = 0xFF444444; indent = 0; break;
                    case OW_LT_H4: case OW_LT_H5: case OW_LT_H6:
                        fg = 0xFF555555; break;
                    case OW_LT_HR:
                        fb_fillrect_alpha(cx + 16, ry + 8, cw - 32, 1, C_OW_SUBTEXT, 100);
                        ry += 18;
                        continue;
                    case OW_LT_LI: indent = 12; break;
                    case OW_LT_BQ:
                        fb_fillrect_alpha(cx + 12, ry, 2, 14, C_BLUE, 120);
                        indent = 18;
                        fg = C_OW_SUBTEXT;
                        break;
                    case OW_LT_TH:
                        fg = C_BLUE;
                        break;
                    case OW_LT_EMPTY:
                        ry += 8;
                        continue;
                }

                fb_drawstr_px(cx + 12 + indent, ry, ow_txt[ln], fg, bg);
            }
            ry += 16;
        }

        /* Scrollbar */
        int total_h = ow_txt_lines * 16;
        if (total_h > content_h) {
            int sb_h = content_h * content_h / (total_h + content_h);
            if (sb_h < 20) sb_h = 20;
            if (sb_h > content_h) sb_h = content_h;
            int sb_y = content_y + (ow.scroll_y * (content_h - sb_h)) / (total_h);
            if (sb_y < content_y) sb_y = content_y;
            if (sb_y + sb_h > content_y + content_h) sb_y = content_y + content_h - sb_h;
            fb_fill_rounded_rect(cx + cw - 8, sb_y, 4, sb_h, 2, 0x44AAAAAA);
        }
    } else if (ow.tabs[ow.active].loading) {
        /* Loading spinner */
        int spin = (timer_get_ticks() / 100) % 4;
        static const char *sp = "|/-\\";
        char sc[2] = {sp[spin], 0};
        fb_drawstr_px(cx + cw / 2 - 4, content_y + content_h / 2 - 8, sc, C_BLUE, C_OW_BG);
        fb_drawstr_px(cx + cw / 2 + 12, content_y + content_h / 2 - 8, "Loading...", C_OW_SUBTEXT, C_OW_BG);
    } else {
        /* Empty state — Search prompt */
        int ey = content_y + content_h / 2;
        /* Google "G" logo (centered) */
        fb_drawstr_px(cx + cw / 2 - 6, ey - 24, "G", C_BLUE, C_OW_BG);
        /* Subtitle */
        fb_drawstr_px(cx + cw / 2 - 100, ey + 8,
                      "Search with Google or enter a URL", C_OW_SUBTEXT, C_OW_BG);
    }

    /* ── Status bar ── */
    int sby = cy + ch - OW_STATUS_H;
    fb_fillrect(cx, sby, cw, OW_STATUS_H, C_OW_STATUS);
    fb_fillrect_alpha(cx, sby, cw, 1, 0xFFFFFF, 10);

    /* Show URL or status from Rust backend */
    openweb_tab_t *rtabs = ow_get_tabs();
    int ractive = ow_get_tab_active();
    if (rtabs && ractive >= 0 && ractive < OW_MAX_TABS && rtabs[ractive].status[0]) {
        fb_drawstr_px(cx + 12, sby + 2, rtabs[ractive].status, C_OW_SUBTEXT, C_OW_STATUS);
    } else {
        fb_drawstr_px(cx + 12, sby + 2, ow.tabs[ow.active].url, C_OW_SUBTEXT, C_OW_STATUS);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   MOUSE MOVE — hover state tracking
   ══════════════════════════════════════════════════════════════════════ */

void openweb_mousemove(int mx, int my) {
    if (!ow.open) return;
    window_t *w = desktop_get_window(ow.win);
    if (!w || !w->visible) return;

    int changed = 0;

    /* Check toolbar buttons */
    int btn_base = mx >= ow_btn_x[0] && mx < ow_btn_x[0] + ow_btn_w &&
                   my >= ow_btn_y && my < ow_btn_y + ow_btn_h;
    int new_btn = 0;
    if (btn_base) {
        if (mx < ow_btn_x[0] + 30) new_btn = 1;
        else if (mx < ow_btn_x[1] + 30) new_btn = 2;
        else if (mx < ow_btn_x[2] + 30) new_btn = 3;
        else if (mx < ow_btn_x[3] + 30) new_btn = 4;
        else if (mx < ow_btn_x[4] + 30) new_btn = 5;
    }
    if (new_btn != ow_hover_btn) { ow_hover_btn = new_btn; changed = 1; }

    /* Check tabs */
    int cy = w->y + 22;
    int new_tab = -1;
    if (my >= cy && my < cy + OW_TAB_H) {
        for (int i = 0; i < ow.tab_count; i++) {
            if (mx >= ow_tab_x[i] && mx < ow_tab_x[i] + ow_tab_w_arr[i]) {
                new_tab = i;
                break;
            }
        }
    }
    if (new_tab != ow_hover_tab) { ow_hover_tab = new_tab; changed = 1; }

    if (changed) desktop_redraw();
}

/* ══════════════════════════════════════════════════════════════════════
   CLICK handling
   ══════════════════════════════════════════════════════════════════════ */

int openweb_click(int mx, int my) {
    if (!ow.open) return 0;
    window_t *w = desktop_get_window(ow.win);
    if (!w || !w->visible) return 0;

    desktop_set_focused(ow.win);

    int cx = w->x + 4, cy = w->y + 22;
    int cw = w->w - 8;
    int toolbar_h = OW_TOOLBAR_H + OW_TAB_H;
    if (ow.show_bookmarks) toolbar_h += OW_BOOKMARKS_H;

    /* ── Tab bar clicks ── */
    if (my >= cy && my < cy + OW_TAB_H) {
        int tx = cx + 10;
        for (int i = 0; i < ow.tab_count; i++) {
            int tw = 140;
            if (tw > (cw - 100) / (ow.tab_count > 0 ? ow.tab_count : 1))
                tw = (cw - 100) / (ow.tab_count > 0 ? ow.tab_count : 1);
            if (tw < 60) tw = 60;

            if (mx >= tx && mx < tx + tw) {
                ow.active = i;
                ow.addr_focused = 0;
                ow.scroll_y = 0;
                ow_set_tab_active(i);
                desktop_redraw();
                return 1;
            }

            /* Close button on active tab */
            if (i == ow.active && ow.tab_count > 1) {
                if (mx >= tx + tw - 18 && mx < tx + tw - 2) {
                    ow_close_tab(i);
                    desktop_redraw();
                    return 1;
                }
            }

            tx += tw + 2;
        }
        /* New tab button */
        if (ow.tab_count < OW_MAX_TABS) {
            if (mx >= tx && mx < tx + 24) {
                ow_new_tab("about:blank");
                desktop_redraw();
            }
        }
        return 1;
    }

    /* ── Address bar / toolbar clicks ── */
    int aby = cy + OW_TAB_H;
    if (my >= aby && my < aby + OW_TOOLBAR_H) {
        int url_x = cx + 12;
        int url_w = cw - 180;

        /* URL bar click */
        if (mx >= url_x && mx < url_x + url_w) {
            ow.addr_focused = 1;
            ow.addr_len = 0;
            ow.addr_cursor = 0;
            for (int i = 0; ow.tabs[ow.active].url[i] && i < 511; i++)
                ow.addr_buf[ow.addr_len++] = ow.tabs[ow.active].url[i];
            desktop_redraw();
            return 1;
        }

        /* Back button */
        int btn_x = cx + cw - 160;
        int btn_y = aby + 6;
        int btn_h = OW_TOOLBAR_H - 12;

        if (mx >= btn_x && mx < btn_x + 30 && my >= btn_y && my < btn_y + btn_h) {
            ow_go_to_hist(-1);
            desktop_redraw();
            return 1;
        }
        /* Forward */
        if (mx >= btn_x + 34 && mx < btn_x + 64 && my >= btn_y && my < btn_y + btn_h) {
            ow_go_to_hist(1);
            desktop_redraw();
            return 1;
        }
        /* Home */
        if (mx >= btn_x + 68 && mx < btn_x + 98 && my >= btn_y && my < btn_y + btn_h) {
            ow_load_url("https://codeos.dev");
            desktop_redraw();
            return 1;
        }
        /* Reload / Stop */
        if (mx >= btn_x + 102 && mx < btn_x + 132 && my >= btn_y && my < btn_y + btn_h) {
            if (ow.tabs[ow.active].loading) {
                ow.tabs[ow.active].loading = 0;
            } else {
                ow_load_url(ow.tabs[ow.active].url);
            }
            desktop_redraw();
            return 1;
        }
        /* AI Summarize */
        if (mx >= btn_x + 140 && mx < btn_x + 170 && my >= btn_y && my < btn_y + btn_h) {
            freecode_gui_open();
            desktop_redraw();
            return 1;
        }
        return 1;
    }

    /* ── Bookmarks bar clicks ── */
    if (ow.show_bookmarks) {
        int bby = aby + OW_TOOLBAR_H;
        if (my >= bby && my < bby + OW_BOOKMARKS_H) {
            int bm_count = ow.bookmark_count;
            if (bm_count > 0) {
                int bmw = (cw - 16) / bm_count;
                if (bmw > 120) bmw = 120;
                for (int i = 0; i < bm_count; i++) {
                    int bmx = cx + 8 + i * (bmw + 2);
                    if (mx >= bmx && mx < bmx + bmw) {
                        ow_load_url(ow.bookmark_urls[i]);
                        return 1;
                    }
                }
            }
            return 1;
        }
    }

    /* ── Content area — link clicks ── */
    if (ow_need_render && ow_txt_lines > 0) {
        int content_y = cy + toolbar_h;
        int content_h = w->h - 26 - toolbar_h - OW_STATUS_H;
        if (my >= content_y && my < content_y + content_h) {
            int click_line = (my - content_y + ow.scroll_y) / 16;
            if (click_line >= 0 && click_line < ow_txt_lines) {
                for (int li = 0; li < ow_link_cnt; li++) {
                    if (ow_links[li].line == click_line) {
                        int lx = cx + 12;
                        int lw = fb_text_width(ow_txt[click_line]) * (ow_links[li].ec - ow_links[li].sc) /
                                 (ow_links[li].ec > 0 ? ow_links[li].ec : 1);
                        if (mx >= lx && mx < lx + lw + 100) {
                            ow_load_url(ow_links[li].url);
                            return 1;
                        }
                    }
                }
            }
            return 1;
        }
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
   KEYBOARD handling
   ══════════════════════════════════════════════════════════════════════ */

int openweb_key(int key) {
    if (!ow.open) return 0;
    window_t *w = desktop_get_window(ow.win);
    if (!w || !w->visible) return 0;

    if (ow.addr_focused) {
        if (key == '\n' || key == '\r') {
            ow_go();
            return 1;
        }
        if (key == '\x1b') {
            ow.addr_focused = 0;
            desktop_redraw();
            return 1;
        }
        if ((key == '\b' || key == 0x7F) && ow.addr_len > 0) {
            ow.addr_len--;
            if (ow.addr_cursor > ow.addr_len) ow.addr_cursor = ow.addr_len;
            desktop_redraw();
            return 1;
        }
        if (key == KEY_LEFT && ow.addr_cursor > 0) {
            ow.addr_cursor--;
            desktop_redraw();
            return 1;
        }
        if (key == KEY_RIGHT && ow.addr_cursor < ow.addr_len) {
            ow.addr_cursor++;
            desktop_redraw();
            return 1;
        }
        if (key >= ' ' && key <= '~' && ow.addr_len < 510) {
            ow.addr_buf[ow.addr_len++] = key;
            desktop_redraw();
            return 1;
        }
        return 1;
    }

    /* Global shortcuts */
    if (key == 't' || key == 'T') {
        if (ow.tab_count < OW_MAX_TABS) {
            ow_new_tab("about:blank");
            desktop_redraw();
        }
        return 1;
    }
    if (key == 'w' || key == 'W') {
        if (ow.tab_count > 1) {
            ow_close_tab(ow.active);
            desktop_redraw();
        }
        return 1;
    }
    if (key == KEY_UP) {
        if (ow.scroll_y > 0) ow.scroll_y -= 20;
        desktop_redraw();
        return 1;
    }
    if (key == KEY_DOWN) {
        ow.scroll_y += 20;
        desktop_redraw();
        return 1;
    }
    if (key == KEY_PGUP) {
        int content_h = w->h - 26 - (OW_TOOLBAR_H + OW_TAB_H + OW_STATUS_H +
                        (ow.show_bookmarks ? OW_BOOKMARKS_H : 0));
        if (ow.scroll_y > content_h) ow.scroll_y -= content_h;
        else ow.scroll_y = 0;
        desktop_redraw();
        return 1;
    }
    if (key == KEY_PGDN) {
        int content_h = w->h - 26 - (OW_TOOLBAR_H + OW_TAB_H + OW_STATUS_H +
                        (ow.show_bookmarks ? OW_BOOKMARKS_H : 0));
        ow.scroll_y += content_h;
        desktop_redraw();
        return 1;
    }
    if (key == '\x1b' && ow.tabs[ow.active].loading) {
        ow.tabs[ow.active].loading = 0;
        desktop_redraw();
        return 1;
    }
    if ((key == 'r' || key == 'R') && !ow.tabs[ow.active].loading) {
        ow_load_url(ow.tabs[ow.active].url);
        return 1;
    }
    if (key == 'l' || key == 'L') {
        ow.addr_focused = 1;
        ow.addr_len = 0;
        ow.addr_cursor = 0;
        ow.addr_buf[0] = 0;
        desktop_redraw();
        return 1;
    }
    if (key == 'b' || key == 'B') {
        ow.show_bookmarks = !ow.show_bookmarks;
        desktop_redraw();
        return 1;
    }
    if (key == 'h' || key == 'H') {
        ow_load_url("https://codeos.dev");
        return 1;
    }
    if (key == 'g' || key == 'G') {
        /* Toggle search engine: Google <-> DuckDuckGo */
        ow.tabs[ow.active].search_engine = ow.tabs[ow.active].search_engine == 0 ? 1 : 0;
        snprintf(ow.tabs[ow.active].title, 64, "Search: %s",
                 ow.tabs[ow.active].search_engine == 0 ? "Google" : "DDG");
        desktop_redraw();
        return 1;
    }
    return 0;
}
