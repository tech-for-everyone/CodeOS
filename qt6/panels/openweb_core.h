#ifndef CODEOS_OPENWEB_CORE_H
#define CODEOS_OPENWEB_CORE_H

#include "ow_http.h"

/* C backend shared by the Qt frontend and legacy OpenWeb clients. */
void ow_core_init(void);
int  ow_core_is_initialized(void);
void ow_core_navigate(const char *text);
void ow_core_navigate_post(const char *action, const char *body, int body_len);
void ow_core_reload(void);
void ow_core_back(void);
void ow_core_forward(void);
void ow_core_stop(void);
int  ow_core_can_go_back(void);
int  ow_core_can_go_forward(void);
void ow_core_new_tab(void);
void ow_core_close_active_tab(void);
void ow_core_set_active_tab(int index);
void ow_core_render_active(void);
openweb_tab_t *ow_core_tabs(void);
int  ow_core_tab_count(void);
int  ow_core_active_tab(void);
int  ow_core_used_tab_count(void);
int  ow_core_load_progress(void);

/* Headless diagnostics: render the active tab with the Rust renderer and dump
 * the resulting text grid / links / title to the console. Used by the kernel
 * shell `ow render <url>` command. */
void ow_core_dump_active(void);

#endif
