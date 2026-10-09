/* lvgl_launcher.h — compatibility wrapper for Panels DE launcher.
 * 
 * The Qt desktop calls these functions; they delegate to the Panels DE
 * fullscreen start menu. When open, LVGL takes over the framebuffer and
 * the shared input queues. The Qt desktop must call lvgl_launcher_pump()
 * every main-loop iteration while lvgl_launcher_is_open() so input is
 * drained and LVGL redraws. */

#ifndef LVGL_LAUNCHER_H
#define LVGL_LAUNCHER_H

#include "lvgl_panels.h"

typedef void (*lvgl_launcher_launch_cb_t)(int index, void *userdata);

/* Opens the launcher with the given app names. Returns 1 if opened, 0 if
 * already open. */
int lvgl_launcher_open(const char *const *names, int count,
                       lvgl_launcher_launch_cb_t cb, void *userdata);

/* Closes the launcher. */
void lvgl_launcher_close(void);

/* Returns 1 if the launcher is open. */
int lvgl_launcher_is_open(void);

/* Must be called each frame from the Qt main loop while open. */
void lvgl_launcher_pump(void);

/* Get the global Panels DE instance (set by lvgl_wm_init) */
panels_de_t *panels_de_get_instance(void);

#endif