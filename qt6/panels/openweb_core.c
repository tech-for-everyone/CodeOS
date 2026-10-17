#include "openweb_core.h"
#include "ow_html.h"

#include <string.h>
#include "kprintf.h"

static int g_initialized;

void ow_core_init(void) {
    if (g_initialized) return;
    ow_tab_new("about:blank");
    g_initialized = 1;
}

int ow_core_is_initialized(void) { return g_initialized; }

void ow_core_navigate(const char *text) {
    if (!text || !text[0]) return;
    ow_core_init();
    kprintf("CORE nav text='%s'\n", text);
    if (!strchr(text, '.') && strncmp(text, "http", 4) != 0 &&
        strncmp(text, "about:", 6) != 0) {
        kprintf("CORE -> ow_search\n");
        ow_search(text);
        kprintf("CORE ow_search done\n");
    }
    else {
        kprintf("CORE -> ow_navigate\n");
        ow_navigate(text);
        kprintf("CORE ow_navigate done\n");
    }
}

void ow_core_navigate_post(const char *action, const char *body, int body_len) {
    if (!action || !action[0]) return;
    ow_core_init();
    kprintf("CORE POST action='%s' len=%d\n", action, body_len);
    ow_navigate_post(action, body, body_len);
}

void ow_core_reload(void) {
    int index;
    openweb_tab_t *tabs;
    ow_core_init();
    index = ow_get_tab_active();
    tabs = ow_get_tabs();
    if (tabs && index >= 0 && index < ow_get_tab_count() && tabs[index].url[0])
        ow_navigate_fresh(tabs[index].url);
}

/* WebView-style navigation: history lives in the Rust core, so every frontend
 * (Qt, tty `openweb`, Zircon browser) shares the same back/forward stacks. */
void ow_core_back(void) {
    if (!g_initialized) return;
    ow_go_back();
}

void ow_core_forward(void) {
    if (!g_initialized) return;
    ow_go_forward();
}

void ow_core_stop(void) {
    if (!g_initialized) return;
    ow_stop_loading();
}

int ow_core_can_go_back(void) {
    openweb_tab_t *tabs;
    int index;
    if (!g_initialized) return 0;
    index = ow_get_tab_active();
    tabs = ow_get_tabs();
    if (tabs && index >= 0 && index < ow_get_tab_count())
        return tabs[index].can_go_back;
    return 0;
}

int ow_core_can_go_forward(void) {
    openweb_tab_t *tabs;
    int index;
    if (!g_initialized) return 0;
    index = ow_get_tab_active();
    tabs = ow_get_tabs();
    if (tabs && index >= 0 && index < ow_get_tab_count())
        return tabs[index].can_go_forward;
    return 0;
}

void ow_core_new_tab(void) {
    ow_core_init();
    ow_tab_new("about:blank");
}

void ow_core_close_active_tab(void) {
    int index;
    if (!g_initialized || ow_get_tab_count() <= 1) return;
    index = ow_get_tab_active();
    if (index >= 0) ow_tab_close(index);
}

void ow_core_set_active_tab(int index) {
    if (g_initialized && index >= 0 && index < ow_get_tab_count())
        ow_set_tab_active(index);
}

void ow_core_render_active(void) {
    int index;
    openweb_tab_t *tabs;
    if (!g_initialized) return;
    index = ow_get_tab_active();
    tabs = ow_get_tabs();
    if (tabs && index >= 0 && index < ow_get_tab_count() &&
        tabs[index].content_len > 0 && tabs[index].content_len < OW_CONTENT_MAX)
        ow_render_rs(tabs[index].content, tabs[index].content_len);
}

openweb_tab_t *ow_core_tabs(void) { return g_initialized ? ow_get_tabs() : 0; }
int ow_core_tab_count(void) { return g_initialized ? ow_get_tab_count() : 0; }
int ow_core_active_tab(void) { return g_initialized ? ow_get_tab_active() : -1; }
int ow_core_used_tab_count(void) { return g_initialized ? ow_tab_used_count() : 0; }
int ow_core_load_progress(void) { return g_initialized ? ow_get_load_progress() : 0; }

/* Render the active tab with the Rust renderer (ow_render_rs) and print the
 * resulting grid to the console. Headless verification aid. */
void ow_core_dump_active(void) {
    int index, r, k;
    openweb_tab_t *tabs;
    ow_core_init();
    index = ow_get_tab_active();
    tabs = ow_get_tabs();
    if (!tabs || index < 0 || index >= ow_get_tab_count() ||
        tabs[index].content_len <= 0) {
        kprintf("ow: no page loaded\n");
        return;
    }
    ow_render_rs(tabs[index].content, tabs[index].content_len);
    kprintf("ow: url='%s' title='%s' lines=%d links=%d images=%d forms=%d fields=%d\n",
            tabs[index].url, ow_page_title, ow_txt_lines, ow_link_cnt,
            ow_image_cnt, ow_form_cnt, ow_field_cnt);
    for (r = 0; r < ow_txt_lines && r < OW_TXT_LINES; r++)
        kprintf("|%s|\n", ow_txt[r]);
    for (k = 0; k < ow_link_cnt; k++)
        kprintf("ow: link[%d] line=%d sc=%d ec=%d url='%s'\n",
                k, ow_links[k].line, ow_links[k].sc, ow_links[k].ec, ow_links[k].url);
    for (k = 0; k < ow_field_cnt; k++)
        kprintf("ow: field[%d] type=%d name='%s' value='%s' line=%d col=%d w=%d checked=%d\n",
                k, ow_form_fields[k].type, ow_form_fields[k].name, ow_form_fields[k].value,
                ow_form_fields[k].line, ow_form_fields[k].col, ow_form_fields[k].width,
                ow_form_fields[k].checked);
}

/* Headless diagnostic for the Rust form API (kernel/kernel/rust_ow/src/
 * ow_form.rs). Renders the active tab, prints every form's built query via
 * ow_form_build_query, then edits the first text/checkbox/select control with
 * ow_field_set_value / ow_field_toggle and re-renders. The edit must come back
 * on the rebuilt field through ow_fv_restore(), which is what proves the cache
 * round-trip. Used by the kernel shell `ow form` command. */
void ow_core_forms_dump(void) {
    int index, k, tf = -1, cb = -1, sel = -1, sel_expect = -1;
    openweb_tab_t *tabs;
    char q[512];

    ow_core_init();
    index = ow_get_tab_active();
    tabs = ow_get_tabs();
    if (!tabs || index < 0 || index >= ow_get_tab_count() ||
        tabs[index].content_len <= 0) {
        kprintf("ow: no page loaded\n");
        return;
    }

    ow_render_rs(tabs[index].content, tabs[index].content_len);
    kprintf("ow: forms=%d fields=%d\n", ow_form_cnt, ow_field_cnt);
    for (k = 0; k < ow_form_cnt; k++) {
        int n = ow_form_build_query(&ow_forms[k], q, sizeof(q));
        kprintf("ow: form[%d] action='%s' method=%d count=%d query(%d)='%s'\n",
                k, ow_forms[k].action, ow_forms[k].method,
                ow_forms[k].field_count, n, q);
    }
    for (k = 0; k < ow_field_cnt; k++)
        kprintf("ow: field[%d] type=%d name='%s' value='%s' checked=%d\n",
                k, ow_form_fields[k].type, ow_form_fields[k].name,
                ow_form_fields[k].value, ow_form_fields[k].checked);

    for (k = 0; k < ow_field_cnt; k++) {
        int t = ow_form_fields[k].type;
        if (tf < 0 && (t == OW_FT_TEXT || t == OW_FT_PASSWORD || t == OW_FT_TEXTAREA)) tf = k;
        if (cb < 0 && (t == OW_FT_CHECKBOX || t == OW_FT_RADIO)) cb = k;
        if (sel < 0 && t == OW_FT_SELECT) sel = k;
    }
    if (tf < 0 && cb < 0 && sel < 0) {
        kprintf("ow: no editable fields to exercise\n");
        return;
    }
    if (tf >= 0)  { ow_field_set_value(tf, "codeos"); kprintf("ow: set field[%d]='codeos'\n", tf); }
    if (cb >= 0)  { ow_field_toggle(cb);               kprintf("ow: toggled field[%d]\n", cb); }
    if (sel >= 0) {
        /* Select the LAST option, so a successful restore is a visible change
         * from the HTML default. It must be a real option: a value matching no
         * option takes ow_field_set_value()'s fallback branch, which by design
         * records no edit (the C original behaved the same), so re-rendering
         * would prove nothing about the cache. */
        int nc = ow_form_fields[sel].opt_cnt;
        if (nc > 1) {
            sel_expect = nc - 1;
            ow_field_set_value(sel, ow_form_fields[sel].opts[sel_expect]);
            kprintf("ow: select field[%d] -> option[%d]='%s'\n",
                    sel, sel_expect, ow_form_fields[sel].opts[sel_expect]);
        } else if (nc == 1) {
            sel_expect = 0;
            ow_field_set_value(sel, ow_form_fields[sel].opts[0]);
            kprintf("ow: select field[%d] -> option[0] (only option)\n", sel);
        } else {
            kprintf("ow: select field[%d] has no options to exercise\n", sel);
        }
    }

    /* Rebuild from the same HTML: the edits must survive via the cache. */
    ow_render_rs(tabs[index].content, tabs[index].content_len);
    if (tf >= 0)
        kprintf("ow: restored field[%d] value='%s' (expect 'codeos')\n", tf, ow_form_fields[tf].value);
    if (cb >= 0)
        kprintf("ow: restored field[%d] checked=%d (expect 1)\n", cb, ow_form_fields[cb].checked);
    if (sel >= 0 && sel_expect >= 0)
        kprintf("ow: restored field[%d] value='%s' opt_sel=%d (expect %d)\n",
                sel, ow_form_fields[sel].value, ow_form_fields[sel].opt_sel, sel_expect);
    for (k = 0; k < ow_form_cnt; k++) {
        int n = ow_form_build_query(&ow_forms[k], q, sizeof(q));
        kprintf("ow: form[%d] query-after(%d)='%s'\n", k, n, q);
    }
}
