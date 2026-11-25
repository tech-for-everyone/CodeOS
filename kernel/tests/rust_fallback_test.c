/* Host test for the Rust-free OpenWeb/Windowland ABI fallback.
 *
 * Compiles the REAL kernel/rust_fallback.c and the real public headers.  The
 * only stubs are the C-owned render globals and strlcpy, which the kernel
 * supplies elsewhere; everything under test is production code.
 */
#include "ow_http.h"
#include "ow_html.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Normally defined by qt6/panels/ow_html.c. */
char ow_txt[OW_TXT_LINES][OW_TXT_COLS];
int ow_txt_lines;
ow_link_t ow_links[OW_MAX_LINKS];
int ow_link_cnt;
ow_image_t ow_images[OW_MAX_IMAGES];
int ow_image_cnt;
int ow_need_render;
ow_line_info_t ow_line_info[OW_TXT_LINES];
int ow_line_img[OW_TXT_LINES];
char ow_page_title[OW_URL_MAX];
char ow_page_desc[OW_URL_MAX];
ow_form_t ow_forms[OW_MAX_FORMS];
int ow_form_cnt;
ow_form_field_t ow_form_fields[OW_MAX_FIELDS];
int ow_field_cnt;

/* Windowland's C ABI has no public kernel header. */
void windowland_init(void);
void windowland_frame(void *wm);
void windowland_pump(void);
void penrose_init(void);
void penrose_pump(void);

/* The real bridge composites surfaces plus chrome; count calls instead of
 * linking the kernel compositor into this host test. */
static int init_calls;
static int compose_calls;
static void *last_wm;
void prs_init(void) { init_calls++; }
void prs_compose_frame(void *wm) { compose_calls++; last_wm = wm; }

/* Kernel string helper; host libc does not provide it everywhere. */
size_t strlcpy(char *dst, const char *src, size_t dstsize) {
    size_t len = strlen(src);
    if (dstsize) {
        size_t n = len < dstsize - 1 ? len : dstsize - 1;
        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return len;
}

static int failures;

#define CHECK(cond) do {                                   \
    if (!(cond)) {                                         \
        printf("FAIL: %s (line %d)\n", #cond, __LINE__);   \
        failures++;                                        \
    }                                                      \
} while (0)

int main(void) {
    char buf[64];

    /* All 27 Rust-exported entry points must remain linkable.  Calling the
     * inert ones also proves the reduced fallback cannot crash the UI. */
    windowland_init();
    CHECK(init_calls == 1); /* fallback must initialize the C bridge once */
    penrose_init();
    CHECK(init_calls == 1); /* the alias re-enters init, but the guard holds */
    CHECK(compose_calls == 0); /* init must not composite a frame */
    windowland_pump();
    CHECK(compose_calls == 1);
    CHECK(last_wm == 0); /* legacy aliases have no Qt window manager */
    penrose_pump();
    CHECK(compose_calls == 2);
    int wm_token;
    windowland_frame(&wm_token);
    CHECK(compose_calls == 3);
    CHECK(last_wm == &wm_token);
    ow_go_back();
    ow_go_forward();
    ow_stop_loading();
    ow_field_set_value(0, "x");
    ow_field_toggle(0);
    ow_fv_restore(NULL, 0);
    ow_fv_store(NULL, 0);

    CHECK(ow_get_tab_count() == 0);
    CHECK(ow_get_tab_active() == 0);
    CHECK(ow_get_load_progress() == 100);
    CHECK(ow_tab_used_count() == 0);

    ow_tab_new("https://example.com/");
    CHECK(ow_get_tab_count() == 1);
    CHECK(ow_get_tab_active() == 0);
    CHECK(strcmp(ow_get_tabs()[0].url, "https://example.com/") == 0);
    CHECK(strstr(ow_get_tabs()[0].content, "without Rust") != NULL);
    CHECK(ow_get_tabs()[0].content_len > 0);
    CHECK(ow_get_tabs()[0].status[0] != 0);
    CHECK(ow_get_tabs()[0].error != 0);
    CHECK(ow_get_tabs()[0].loading == 0);
    CHECK(ow_tab_used_count() == 1);

    /* Invalid tab selection must be ignored, not shape later navigation. */
    ow_set_tab_active(99);
    CHECK(ow_get_tab_active() == 0);

    ow_navigate("https://example.org/");
    CHECK(ow_get_tab_count() == 1);
    CHECK(strcmp(ow_get_tabs()[0].url, "https://example.org/") == 0);
    ow_navigate_fresh("https://example.net/");
    CHECK(strcmp(ow_get_tabs()[0].url, "https://example.net/") == 0);
    ow_navigate_post("https://example.net/post", "a=b", 3);
    CHECK(strcmp(ow_get_tabs()[0].url, "https://example.net/post") == 0);
    ow_search("codeos");
    CHECK(strcmp(ow_get_tabs()[0].url, "codeos") == 0);

    ow_tab_new("about:blank");
    CHECK(ow_get_tab_count() == 2);
    CHECK(ow_get_tab_active() == 1);
    CHECK(ow_tab_used_count() == 2);

    ow_tab_close(0);
    CHECK(ow_get_tab_count() == 1);
    CHECK(ow_get_tab_active() == 0);
    CHECK(strcmp(ow_get_tabs()[0].url, "about:blank") == 0);

    ow_tab_close(0);
    CHECK(ow_get_tab_count() == 0);
    CHECK(ow_get_tab_active() == 0);
    CHECK(ow_tab_used_count() == 0);

    /* Out-of-range closes are no-ops. */
    ow_tab_close(7);
    CHECK(ow_get_tab_count() == 0);

    /* Text-only rendering must bound the grid and reset all metadata. */
    ow_link_cnt = 123;
    ow_image_cnt = 4;
    ow_form_cnt = 2;
    ow_field_cnt = 9;
    strcpy(ow_page_title, "stale");
    strcpy(ow_page_desc, "stale");
    {
        const char html[] = "<p>hello</p><div>world</div>";
        ow_render_rs(html, (int)sizeof(html) - 1);
    }
    CHECK(ow_txt_lines >= 1);
    CHECK(strcmp(ow_txt[0], "helloworld") == 0);
    CHECK(ow_txt[OW_TXT_LINES - 1][0] == 0);
    CHECK(ow_link_cnt == 0);
    CHECK(ow_image_cnt == 0);
    CHECK(ow_form_cnt == 0);
    CHECK(ow_field_cnt == 0);
    CHECK(ow_page_title[0] == 0);
    CHECK(ow_page_desc[0] == 0);
    CHECK(ow_need_render == 1);
    CHECK(ow_line_img[0] == -1);
    CHECK(ow_line_info[0].type == OW_LT_NORMAL);

    CHECK(ow_field_at(0, 0) == -1);
    CHECK(ow_form_build_query(NULL, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == 0);

    if (failures) {
        printf("%d fallback assertion(s) failed\n", failures);
        return 1;
    }
    printf("rust-free fallback: all assertions passed\n");
    return 0;
}
