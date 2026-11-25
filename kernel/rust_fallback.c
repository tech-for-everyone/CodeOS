/* rust_fallback.c — C substitutes for the optional Rust backends.
 *
 * The x86_64 kernel normally links two Rust static archives:
 *   libow_http.a    OpenWeb's tab/HTTP backend, HTML renderer and form cache
 *   libwindowland.a the Windowland compositor / layout engine
 * Both need the no-std Rust target for the selected architecture
 * (x86_64-unknown-none or aarch64-unknown-none).  A host without that target
 * cannot build them, so when USE_RUST=0 (the automatic default when the target
 * is missing) this file supplies the same C ABI on x86_64 and the image still
 * builds and boots.  Behaviour is deliberately reduced:
 *   - OpenWeb shows a short notice instead of fetching/rendering real pages;
 *   - Windowland has no layout policy, but still initializes the bridge and owns
 *     the frame pump: it composites X11 surfaces and paints HyperDE chrome.
 *
 * Set USE_RUST=1 to build and link the real Rust backends instead.
 */
#include "ow_http.h"
#include "ow_html.h"
#include "penrose_bridge.h"
#include "string.h"

#define OW_FB_NOTICE \
    "OpenWeb: built without Rust. Install the matching no-std Rust target for full browsing."

static openweb_tab_t s_tabs[OW_MAX_TABS];
static int s_tab_count;
static int s_active;

static void set_notice(openweb_tab_t *t, const char *url) {
    strlcpy(t->url, url ? url : "about:blank", sizeof(t->url));
    size_t n = strlcpy(t->content, OW_FB_NOTICE, sizeof(t->content));
    t->content_len = (int)n;
    strlcpy(t->status, "no Rust backend", sizeof(t->status));
    t->loading = 0;
    t->error = 5;
    t->can_go_back = 0;
    t->can_go_forward = 0;
    t->_redirect_depth = 0;
    t->scroll = 0;
}

openweb_tab_t *ow_get_tabs(void) { return s_tabs; }
int ow_get_tab_count(void) { return s_tab_count; }
int ow_get_tab_active(void) { return s_active; }

void ow_set_tab_active(int idx) {
    if (idx >= 0 && idx < s_tab_count) s_active = idx;
}

int ow_get_load_progress(void) { return 100; }

int ow_tab_used_count(void) {
    int n = 0;
    for (int i = 0; i < OW_MAX_TABS; i++) {
        if (s_tabs[i].url[0]) n++;
    }
    return n;
}

void ow_tab_new(const char *url) {
    int idx = -1;
    for (int i = 0; i < OW_MAX_TABS; i++) {
        if (!s_tabs[i].url[0]) { idx = i; break; }
    }
    if (idx < 0) return;
    memset(&s_tabs[idx], 0, sizeof(s_tabs[idx]));
    set_notice(&s_tabs[idx], url);
    if (idx >= s_tab_count) s_tab_count = idx + 1;
    s_active = idx;
}

void ow_tab_close(int idx) {
    if (idx < 0 || idx >= s_tab_count) return;
    for (int i = idx; i + 1 < s_tab_count; i++) {
        s_tabs[i] = s_tabs[i + 1];
    }
    memset(&s_tabs[s_tab_count - 1], 0, sizeof(s_tabs[0]));
    s_tab_count--;
    if (s_active >= s_tab_count) s_active = s_tab_count - 1;
    if (s_active < 0) s_active = 0;
}

static void active_navigate(const char *url) {
    if (s_active < 0 || s_active >= s_tab_count) {
        ow_tab_new(url);
    } else {
        set_notice(&s_tabs[s_active], url);
    }
}

void ow_navigate(const char *url) { active_navigate(url); }
void ow_navigate_fresh(const char *url) { active_navigate(url); }
void ow_navigate_post(const char *url, const void *body, int body_len) {
    (void)body;
    (void)body_len;
    active_navigate(url);
}
void ow_search(const char *query) { active_navigate(query); }

/* No history is kept by the fallback; the UI simply reports none available. */
void ow_go_back(void) {}
void ow_go_forward(void) {}
void ow_stop_loading(void) {}

/* Text-only renderer: strip markup into the C-owned grid.  The full CSS/layout
 * renderer lives in Rust; this keeps the grid finite and sane without it. */
void ow_render_rs(const char *html, int len) {
    if (!html || len < 0) return;
    for (int i = 0; i < OW_TXT_LINES; i++) {
        ow_txt[i][0] = 0;
        ow_line_info[i].type = OW_LT_NORMAL;
        ow_line_img[i] = -1;
    }
    ow_txt_lines = 0;
    ow_link_cnt = 0;
    ow_image_cnt = 0;
    ow_form_cnt = 0;
    ow_field_cnt = 0;
    ow_page_title[0] = 0;
    ow_page_desc[0] = 0;
    ow_need_render = 1;

    int line = 0, col = 0, in_tag = 0;
    for (int i = 0; i < len && line < OW_TXT_LINES; i++) {
        char c = html[i];
        if (c == '<') { in_tag = 1; continue; }
        if (c == '>') { in_tag = 0; continue; }
        if (in_tag) continue;
        if (c == '\n' || c == '\r') {
            if (col) { ow_txt[line][col] = 0; line++; col = 0; }
            continue;
        }
        if ((c == ' ' || c == '\t') && col == 0) continue;
        if (col < OW_TXT_COLS - 1) ow_txt[line][col++] = c;
    }
    if (col) { ow_txt[line][col] = 0; line++; }
    ow_txt_lines = line;
}

/* The fallback renderer emits no controls, so the form API is inert. */
void ow_field_set_value(int fi, const char *v) { (void)fi; (void)v; }
void ow_field_toggle(int fi) { (void)fi; }
int  ow_field_at(int line, int col) { (void)line; (void)col; return -1; }
int  ow_form_build_query(const ow_form_t *f, char *out, int out_max) {
    (void)f;
    if (out && out_max > 0) out[0] = 0;
    return 0;
}
void ow_fv_restore(ow_form_field_t *f, int fi) { (void)f; (void)fi; }
void ow_fv_store(const ow_form_field_t *f, int fi) { (void)f; (void)fi; }

/* No Rust layout engine, but the frame ABI is real: Windowland still decides
 * when to composite and paint chrome, via the same bridge the Rust path uses. */
extern void prs_compose_frame(void *wm);

void windowland_init(void) {
    static int initialized;
    if (!initialized) {
        initialized = 1;
        prs_init(); /* the Rust path does this from CodeOSConn::try_new */
    }
}
void windowland_frame(void *wm) { prs_compose_frame(wm); }
void windowland_pump(void) { windowland_frame(0); }
void penrose_init(void) { windowland_init(); }
void penrose_pump(void) { windowland_pump(); }
