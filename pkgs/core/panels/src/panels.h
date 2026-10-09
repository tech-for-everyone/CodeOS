#ifndef PANELS_H
#define PANELS_H

#include <stdint.h>
#include "windows.h"

#define APP_COUNT_MAX 12
#define ICON_SZ_DOCK   44
#define ICON_SZ_LAUNCH 64

extern const char *app_names[APP_COUNT_MAX];
extern int app_is_system[APP_COUNT_MAX];
extern uint32_t *icon_buf_dock[APP_COUNT_MAX];
extern uint32_t *icon_buf_launch[APP_COUNT_MAX];
extern uint32_t *trash_buf_dock;

int  panels_init(void);
void panels_draw_menubar(uint32_t scr_w, uint32_t scr_h, int active_win, const char *active_title);
void panels_draw_dock(uint32_t scr_w, uint32_t scr_h);
void panels_draw_launcher(uint32_t scr_w, uint32_t scr_h);
void panels_draw_all(uint32_t scr_w, uint32_t scr_h, int active_win, const char *active_title);
int  panels_click(int mx, int my, uint32_t scr_w, uint32_t scr_h);
int  panels_hover(int mx, int my, uint32_t scr_w, uint32_t scr_h);
void panels_toggle_start(void);
int  panels_menubar_h(void);
int  panels_dock_h(void);
void panels_anim_update(uint64_t now_ms);

/* Desktop API — owns the full GUI lifecycle */
int  panels_desktop_init(void);
void panels_desktop_run(void);
void panels_desktop_redraw(void);
void panels_desktop_stop(void);
void panels_desktop_stop_with_reason(int reason);
int  panels_desktop_active(void);
int  panels_desktop_stop_reason_get(void);

/* Window management (delegated) */
int  panels_new_window(int x, int y, int w, int h, const char *title, uint32_t fg, uint32_t bg);
void panels_close_window(int idx);
void panels_set_title(int idx, const char *title);
int  panels_window_count(void);
window_t *panels_get_window(int idx);
int  panels_focused_window(void);
void panels_focus_next(void);
void panels_focus_prev(void);
void panels_set_focused(int idx);
void panels_window_show(int idx, int show);

#define PANELS_STOP_NONE  0
#define PANELS_STOP_EXIT  2

#endif
