#ifndef GESTURE_H
#define GESTURE_H

#include "input.h"
#include "types.h"

#define MAX_TOUCH_POINTS 4

/* Single-finger state */
typedef struct {
    int active;
    int start_x, start_y;
    int last_x, last_y;
    int cur_x, cur_y;
    int total_dx, total_dy;
    uint64_t start_ms;
    uint64_t last_ms;
    int touch_id;
    int moved;
} gesture_finger_t;

/* Multi-touch gesture tracker */
typedef struct {
    gesture_finger_t fingers[MAX_TOUCH_POINTS];
    int finger_count;

    /* Pinch state */
    int pinch_active;
    int pinch_init_dist;
    int pinch_cur_dist;
    int pinch_finger1;  /* ids of the two pinching fingers */
    int pinch_finger2;
} gesture_tracker_t;

void gesture_init(gesture_tracker_t *gt);
void gesture_touch_down(gesture_tracker_t *gt, int x, int y, int id, uint64_t ms);
void gesture_touch_move(gesture_tracker_t *gt, int x, int y, int id, uint64_t ms);
int  gesture_touch_up(gesture_tracker_t *gt, int id, uint64_t ms, gesture_type_t *out_gesture, int *out_dx, int *out_dy);
void gesture_abort(gesture_tracker_t *gt);

/* Low-level single-finger helper (for callers that need raw finger state) */
gesture_finger_t *gesture_get_finger(gesture_tracker_t *gt, int id);

/* Recognizer parameters */
#define GESTURE_SWIPE_THRESHOLD  50
#define GESTURE_LONG_PRESS_MS    600
#define GESTURE_TAP_MAX_MS       300
#define GESTURE_TAP_MAX_MOVE     15
#define GESTURE_PINCH_THRESHOLD  30   /* min distance change to emit pinch */

#endif
