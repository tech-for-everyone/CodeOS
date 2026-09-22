#ifndef INPUT_H
#define INPUT_H

#include "types.h"

typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY,
    INPUT_EVENT_MOUSE,
    INPUT_EVENT_TOUCH,
    INPUT_EVENT_GESTURE
} input_event_type_t;

/* Gesture types for touch input */
typedef enum {
    GESTURE_NONE,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_TAP,
    GESTURE_LONG_PRESS,
    GESTURE_PINCH_IN,
    GESTURE_PINCH_OUT,
} gesture_type_t;

typedef struct {
    input_event_type_t type;
    union {
        struct {
            int key;
            uint8_t modifiers;
        } key;
        struct {
            int x;
            int y;
            int dx;
            int dy;
            int buttons;
            int wheel;
        } mouse;
        struct {
            int x;
            int y;
            int dx;
            int dy;
            int touch_id;   /* for multi-touch tracking */
            int pressure;   /* 0-100 */
        } touch;
        struct {
            gesture_type_t gesture;
            int x, y;        /* origin */
            int dx, dy;      /* displacement */
            int velocity;    /* pixels per frame */
        } gesture;
    } data;
} input_event_t;

void input_init(void);
void input_poll(void);
int  input_available(void);
int  input_get_event(input_event_t *event);
void input_flush_events(void);

#endif
