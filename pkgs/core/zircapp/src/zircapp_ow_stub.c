/* ─────────────────────────────────────────────────────────────
 * Offline OpenWeb backend for the ZircApp host.
 *
 * Zircon's apps/browser.c is written against the OpenWeb C API.  In the
 * Zircon tree that API is the Rust HTTP/tab backend (libow_http.a) plus the
 * render globals from qt6/panels/ow_html.c, and Zircon already ships a
 * browser/net_shim.c that answers every fetch with "network unavailable".
 *
 * The CodeOS-side ZircApp host does not link the Rust backend yet, so this
 * file provides the same C surface with no network either: a small in-memory
 * tab list (so tab counts / active tab / history flags behave) and empty
 * render globals.  It is deliberately the backend that is stubbed -- the
 * browser *app* is the unmodified Zircon source.
 * ───────────────────────────────────────────────────────────── */
#include <ow_http.h>
#include <ow_html.h>

#include <string.h>

#define ZC_TABS 4

static openweb_tab_t zc_tabs[ZC_TABS];
static int zc_tab_count = 1;
static int zc_active;

static void zc_set_url(int idx, const char *url) {
    if (idx < 0 || idx >= ZC_TABS) return;
    zc_tabs[idx].url[0] = '\0';
    if (url) {
        strncpy(zc_tabs[idx].url, url, OW_URL_MAX - 1);
        zc_tabs[idx].url[OW_URL_MAX - 1] = '\0';
    }
    zc_tabs[idx].status[0] = '\0';
    zc_tabs[idx].content_len = 0;
    zc_tabs[idx].loading = 0;
    zc_tabs[idx].scroll = 0;
    zc_tabs[idx].error = 1; /* no network in this host */
}

openweb_tab_t *ow_get_tabs(void) { return zc_tabs; }
int ow_get_tab_count(void) { return zc_tab_count; }
int ow_get_tab_active(void) { return zc_active; }
void ow_set_tab_active(int idx) {
    if (idx >= 0 && idx < zc_tab_count) zc_active = idx;
}
int ow_get_load_progress(void) { return 0; }
int ow_tab_used_count(void) { return zc_tab_count; }

void ow_tab_new(const char *url) {
    if (zc_tab_count >= ZC_TABS) return;
    int idx = zc_tab_count++;
    zc_active = idx;
    zc_set_url(idx, url);
    zc_tabs[idx].can_go_back = 0;
    zc_tabs[idx].can_go_forward = 0;
}

void ow_tab_close(int idx) {
    if (idx <= 0 || idx >= zc_tab_count) return; /* always keep one tab */
    for (int i = idx; i < zc_tab_count - 1; i++)
        zc_tabs[i] = zc_tabs[i + 1];
    zc_tab_count--;
    if (zc_active >= zc_tab_count)
        zc_active = zc_tab_count - 1;
}

void ow_navigate(const char *url) {
    if (!url) return;
    zc_tabs[zc_active].can_go_back = 1;
    zc_set_url(zc_active, url);
}

void ow_navigate_fresh(const char *url) {
    if (!url) return;
    zc_tabs[zc_active].can_go_back = 0;
    zc_tabs[zc_active].can_go_forward = 0;
    zc_set_url(zc_active, url);
}

void ow_navigate_post(const char *url, const void *body, int body_len) {
    (void)body;
    (void)body_len;
    ow_navigate(url);
}

void ow_search(const char *query) { ow_navigate(query); }
void ow_go_back(void) { zc_tabs[zc_active].can_go_back = 0; }
void ow_go_forward(void) { zc_tabs[zc_active].can_go_forward = 0; }
void ow_stop_loading(void) { zc_tabs[zc_active].loading = 0; }

/* Render output globals — normally filled by the Rust renderer. */
char ow_txt[OW_TXT_LINES][OW_TXT_COLS];
int  ow_txt_lines;
ow_link_t ow_links[OW_MAX_LINKS];
int  ow_link_cnt;
int  ow_need_render;
ow_line_info_t ow_line_info[OW_TXT_LINES];

void render_html(const char *html, int len) {
    (void)html;
    (void)len;
}
