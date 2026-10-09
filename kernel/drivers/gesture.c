#include "gesture.h"

static gesture_finger_t *find_finger(gesture_tracker_t *gt, int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++)
        if (gt->fingers[i].active && gt->fingers[i].touch_id == id)
            return &gt->fingers[i];
    return NULL;
}

static gesture_finger_t *find_free_slot(gesture_tracker_t *gt, int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++)
        if (!gt->fingers[i].active) {
            gt->fingers[i].touch_id = id;
            return &gt->fingers[i];
        }
    return NULL;
}

static void classify_finger(gesture_finger_t *f, uint64_t ms, gesture_type_t *out, int *out_dx, int *out_dy) {
    if (out_dx) *out_dx = f->total_dx;
    if (out_dy) *out_dy = f->total_dy;

    uint64_t elapsed = ms - f->start_ms;

    if (!f->moved && elapsed >= GESTURE_LONG_PRESS_MS) {
        if (out) *out = GESTURE_LONG_PRESS;
        return;
    }

    if (!f->moved && elapsed <= GESTURE_TAP_MAX_MS) {
        if (out) *out = GESTURE_TAP;
        return;
    }

    int adx = f->total_dx < 0 ? -f->total_dx : f->total_dx;
    int ady = f->total_dy < 0 ? -f->total_dy : f->total_dy;

    if (adx < GESTURE_SWIPE_THRESHOLD && ady < GESTURE_SWIPE_THRESHOLD) {
        if (out) *out = GESTURE_NONE;
        return;
    }

    if (adx > ady) {
        if (out) *out = (f->total_dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
    } else {
        if (out) *out = (f->total_dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
    }
}

static int calc_dist(gesture_tracker_t *gt, int id1, int id2) {
    gesture_finger_t *a = find_finger(gt, id1);
    gesture_finger_t *b = find_finger(gt, id2);
    if (!a || !b || !a->active || !b->active) return 0;
    int dx = a->cur_x - b->cur_x;
    int dy = a->cur_y - b->cur_y;
    return dx * dx + dy * dy;
}

void gesture_init(gesture_tracker_t *gt) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++)
        gt->fingers[i].active = 0;
    gt->finger_count = 0;
    gt->pinch_active = 0;
    gt->pinch_init_dist = 0;
    gt->pinch_cur_dist = 0;
    gt->pinch_finger1 = -1;
    gt->pinch_finger2 = -1;
}

void gesture_touch_down(gesture_tracker_t *gt, int x, int y, int id, uint64_t ms) {
    gesture_finger_t *f = find_free_slot(gt, id);
    if (!f) return;

    f->active = 1;
    f->touch_id = id;
    f->start_x = x;
    f->start_y = y;
    f->last_x = x;
    f->last_y = y;
    f->cur_x = x;
    f->cur_y = y;
    f->total_dx = 0;
    f->total_dy = 0;
    f->start_ms = ms;
    f->last_ms = ms;
    f->moved = 0;
    gt->finger_count++;

    /* Check if we now have exactly 2 fingers → start pinch tracking */
    if (gt->finger_count == 2) {
        int ids[MAX_TOUCH_POINTS], n = 0;
        for (int i = 0; i < MAX_TOUCH_POINTS && n < 2; i++)
            if (gt->fingers[i].active) ids[n++] = gt->fingers[i].touch_id;
        if (n == 2) {
            gt->pinch_active = 1;
            gt->pinch_finger1 = ids[0];
            gt->pinch_finger2 = ids[1];
            gt->pinch_init_dist = calc_dist(gt, ids[0], ids[1]);
            gt->pinch_cur_dist = gt->pinch_init_dist;
            /* Abort any single-finger gesture on first finger */
            gt->fingers[0].total_dx = 0;
            gt->fingers[0].total_dy = 0;
            if (n > 1) {
                gt->fingers[1].total_dx = 0;
                gt->fingers[1].total_dy = 0;
            }
        }
    }
}

void gesture_touch_move(gesture_tracker_t *gt, int x, int y, int id, uint64_t ms) {
    gesture_finger_t *f = find_finger(gt, id);
    if (!f) return;

    f->last_x = f->cur_x;
    f->last_y = f->cur_y;
    f->cur_x = x;
    f->cur_y = y;
    f->total_dx += x - f->last_x;
    f->total_dy += y - f->last_y;
    f->last_ms = ms;

    int moved = f->total_dx * f->total_dx + f->total_dy * f->total_dy;
    if (moved > GESTURE_TAP_MAX_MOVE * GESTURE_TAP_MAX_MOVE)
        f->moved = 1;

    /* Update pinch distance */
    if (gt->pinch_active && id == gt->pinch_finger1) {
        gt->pinch_cur_dist = calc_dist(gt, gt->pinch_finger1, gt->pinch_finger2);
    } else if (gt->pinch_active && id == gt->pinch_finger2) {
        gt->pinch_cur_dist = calc_dist(gt, gt->pinch_finger1, gt->pinch_finger2);
    }
}

int gesture_touch_up(gesture_tracker_t *gt, int id, uint64_t ms, gesture_type_t *out_gesture, int *out_dx, int *out_dy) {
    gesture_finger_t *f = find_finger(gt, id);
    if (!f) return 0;

    int was_pinching = gt->pinch_active && gt->finger_count >= 2;

    f->active = 0;
    gt->finger_count--;

    if (was_pinching) {
        /* Pinch detection */
        int delta = gt->pinch_cur_dist - gt->pinch_init_dist;
        int adelta = delta < 0 ? -delta : delta;

        if (adelta >= GESTURE_PINCH_THRESHOLD * GESTURE_PINCH_THRESHOLD) {
            if (out_gesture) *out_gesture = (delta < 0) ? GESTURE_PINCH_IN : GESTURE_PINCH_OUT;
            if (out_dx) *out_dx = 0;
            if (out_dy) *out_dy = 0;
            gt->pinch_active = 0;
            return 1;
        }
        gt->pinch_active = 0;
    }

    /* If other fingers still down, don't emit single-finger gesture */
    if (gt->finger_count > 0) {
        if (out_gesture) *out_gesture = GESTURE_NONE;
        return 0;
    }

    classify_finger(f, ms, out_gesture, out_dx, out_dy);
    return *out_gesture != GESTURE_NONE;
}

void gesture_abort(gesture_tracker_t *gt) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++)
        gt->fingers[i].active = 0;
    gt->finger_count = 0;
    gt->pinch_active = 0;
}

gesture_finger_t *gesture_get_finger(gesture_tracker_t *gt, int id) {
    return find_finger(gt, id);
}
