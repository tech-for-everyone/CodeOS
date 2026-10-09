/* lvgl_launcher.c — compatibility wrappers for old launcher API.
 * 
 * The Qt desktop calls these functions; they delegate to the old LVGL
 * launcher that was removed. Now they're no-ops since we use the
 * QtLauncherOverlay in qt_panels.cpp instead. */

#include "lvgl_launcher.h"
#include "lvgl_port.h"
#include "lvgl/lvgl.h"
#include "kprintf.h"
#include <string.h>

static int launcher_open = 0;

int lvgl_launcher_open(const char *const *names, int count,
                       lvgl_launcher_launch_cb_t cb, void *userdata) {
    (void)names; (void)count; (void)cb; (void)userdata;
    if (launcher_open) return 0;
    launcher_open = 1;
    kprintf("launcher: open requested (no-op)\n");
    return 1;
}

void lvgl_launcher_close(void) {
    launcher_open = 0;
}

int lvgl_launcher_is_open(void) {
    return launcher_open;
}

void lvgl_launcher_pump(void) {
    /* Old launcher was removed, Qt now handles its own launcher */
    (void)lv_timer_handler();
}