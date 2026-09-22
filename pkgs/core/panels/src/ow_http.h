#ifndef OW_HTTP_H
#define OW_HTTP_H

#include "openweb.h"

typedef struct {
    char url[OW_URL_MAX];
    char content[OW_CONTENT_MAX];
    int content_len;
    int loading;
    int scroll;
    char status[80];
    int error;
    int can_go_back;
    int can_go_forward;
    /* Must mirror the trailing field of Rust `OpenwebTab` (ow_http.rs) so the
     * C and Rust `sizeof` (array stride) agree. */
    int _redirect_depth;
} openweb_tab_t;

/* ── Rust backend API (ow_http.rs → libow_http.a) ── */
openweb_tab_t *ow_get_tabs(void);
int  ow_get_tab_count(void);
int  ow_get_tab_active(void);
void ow_set_tab_active(int idx);
int  ow_get_load_progress(void);

void ow_tab_new(const char *url);
void ow_tab_close(int idx);
int  ow_tab_used_count(void);

void ow_navigate(const char *url);
void ow_navigate_fresh(const char *url);
void ow_navigate_post(const char *url, const void *body, int body_len);
void ow_search(const char *query);

#endif
