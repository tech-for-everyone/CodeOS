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
