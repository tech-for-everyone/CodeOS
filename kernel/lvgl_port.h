#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <stdint.h>

/* Init the LVGL library and register the CodeOS display + input drivers.
 * Idempotent: safe to call multiple times. Requires fb_init() to have run. */
void lvgl_port_init(void);

/* Enable/disable LVGL consuming keyboard/mouse events. Must stay disabled
 * while the Qt desktop owns the screen, otherwise LVGL would drain the
 * shared kernel input queues. */
void lvgl_port_set_input_enabled(int enabled);

/* True while LVGL owns the input queues (set_input_enabled(1)). The Qt QPA
 * should stop polling input in this state. */
int lvgl_port_input_enabled(void);

/* Registered LVGL input devices (NULL until lvgl_port_init has run). */
void *lvgl_port_get_pointer(void);
void *lvgl_port_get_keypad(void);

/* True when a CodeOS display has been registered. */
int lvgl_port_ready(void);

#endif
