#ifndef PANELS_API_H
#define PANELS_API_H

#include <stdint.h>

/*
 * panels_api.h — Single entry point for the CodeOS GUI package.
 *
 * The panels package owns ALL GUI code: compositor, dock, launcher,
 * menubar, windows, wallpaper, notifications, context menus, app UIs.
 *
 * The kernel calls only these functions. Everything else is internal.
 */

/* Lifecycle */
int  panels_api_init(void);
void panels_api_run(void);
void panels_api_stop(void);
void panels_api_stop_with_reason(int reason);
int  panels_api_stop_reason_get(void);
int  panels_api_active(void);

/* Redraw */
void panels_api_redraw(void);

/* Window management (delegated from kernel callers) */
int  panels_api_new_window(int x, int y, int w, int h, const char *title, uint32_t fg, uint32_t bg);
void panels_api_close_window(int idx);
void panels_api_set_title(int idx, const char *title);
int  panels_api_window_count(void);
void *panels_api_get_window(int idx);
int  panels_api_focused_window(void);
void panels_api_focus_next(void);
void panels_api_focus_prev(void);
void panels_api_set_focused(int idx);
void panels_api_window_show(int idx, int show);
void panels_api_toggle_start(void);

/* Stop reasons */
#define PANELS_STOP_NONE  0
#define PANELS_STOP_EXIT  2

#endif
