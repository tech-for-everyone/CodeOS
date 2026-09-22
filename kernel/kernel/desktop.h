#ifndef DESKTOP_H
#define DESKTOP_H

#include "types.h"
#include "windows.h"

#define DESKTOP_STOP_NONE      0
#define DESKTOP_STOP_EXIT      2

int  desktop_init(void);
int  desktop_active(void);
void desktop_run(void);
void desktop_redraw(void);
int  desktop_new_window(int x, int y, int w, int h, const char *title, uint32_t fg, uint32_t bg);
void desktop_close_window(int idx);
void desktop_set_title(int idx, const char *title);
void desktop_stop(void);
void desktop_stop_with_reason(int reason);
int  desktop_stop_reason_get(void);

int  desktop_window_count(void);
window_t *desktop_get_window(int idx);
int  desktop_focused_window(void);
void desktop_focus_next(void);
void desktop_focus_prev(void);
void desktop_set_focused(int idx);
void desktop_window_show(int idx, int show);
void desktop_toggle_start(void);

#endif
