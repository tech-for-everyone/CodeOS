extern "C" {
#include "desktop.h"

/* Weak stubs for qt6_panels_* — overridden by strong definitions from Qt6 objects
 * when they are linked (final target). When building stage1 without Qt6, these
 * stubs ensure the linker has a definition to use. */
__attribute__((weak)) int qt6_panels_init(void) { return 0; }
__attribute__((weak)) int qt6_panels_active(void) { return 0; }
__attribute__((weak)) void qt6_panels_run(void) {}
__attribute__((weak)) void qt6_panels_stop(void) {}

int desktop_init(void) { return qt6_panels_init(); }
int desktop_active(void) { return qt6_panels_active(); }
void desktop_run(void) { qt6_panels_run(); }
void desktop_stop(void) { qt6_panels_stop(); }

void desktop_redraw(void) {}
void desktop_stop_with_reason(int reason) { (void)reason; }
int  desktop_stop_reason_get(void) { return 0; }

int desktop_new_window(int x, int y, int w, int h, const char *title, uint32_t fg, uint32_t bg) {
    (void)x; (void)y; (void)w; (void)h; (void)title; (void)fg; (void)bg;
    return -1;
}
void desktop_close_window(int idx) { (void)idx; }
void desktop_set_title(int idx, const char *title) { (void)idx; (void)title; }
int  desktop_window_count(void) { return 0; }
window_t *desktop_get_window(int idx) { (void)idx; return 0; }
int  desktop_focused_window(void) { return -1; }
void desktop_focus_next(void) {}
void desktop_focus_prev(void) {}
void desktop_set_focused(int idx) { (void)idx; }
void desktop_window_show(int idx, int show) { (void)idx; (void)show; }
void desktop_toggle_start(void) {}
} /* extern "C" */
