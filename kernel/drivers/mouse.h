#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

#define MOUSE_LEFT   1
#define MOUSE_RIGHT  2
#define MOUSE_MIDDLE 4
#define MOUSE_X1     8
#define MOUSE_X2     16

/* Mouse event (for event queue) */
typedef struct {
    int x, y;
    int dx, dy;
    int buttons;
    int buttons_changed;  /* bits that changed since last event */
    int wheel;
    uint64_t timestamp;
} mouse_event_t;

#define MOUSE_EVENT_BUF_SIZE 64

void mouse_init(void);
void mouse_reset(void);
void mouse_poll(void);
void mouse_set_pos(int x, int y);
void mouse_set_bounds(int max_x, int max_y);
void mouse_zero_deltas(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
int  mouse_get_dx(void);
int  mouse_get_dy(void);
int  mouse_get_wheel(void);
int  mouse_get_buttons(void);
int  mouse_available(void);
int  mouse_has_wheel(void);
int  mouse_has_extra_buttons(void);

/* Sensitivity: 100 = 1:1, >100 = faster, <100 = slower */
void mouse_set_sensitivity(int percent);
int  mouse_get_sensitivity(void);

/* Acceleration: 0 = off, 1 = low, 2 = medium, 3 = high */
void mouse_set_acceleration(int level);
int  mouse_get_acceleration(void);

/* Double-click detection */
int  mouse_double_clicked(int button);
uint64_t mouse_last_click_ms(int button);

/* Click debounce: returns 1 only on first press, ignores bounce */
int  mouse_button_just_pressed(int button);
int  mouse_button_just_released(int button);

/* Scroll speed: 1 = normal (1 line), 2 = fast (3 lines), 3 = very fast (5 lines) */
void mouse_set_scroll_speed(int speed);
int  mouse_get_scroll_speed(void);

/* Event queue (optional — for event-driven callers) */
int  mouse_get_event(mouse_event_t *out);
int  mouse_has_event(void);

#endif
