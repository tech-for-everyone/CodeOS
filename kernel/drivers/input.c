#include "input.h"
#include "keyboard.h"
#include "mouse.h"
#include "mouse_g305.h"
#include "kbd_aula_f87.h"
#include "ps2.h"
#include "usb.h"
#include "gesture.h"
#include "../arch/x86_64/io.h"
#include "../drivers/timer.h"
#include "../arch/x86_64/serial.h"

#define INPUT_QUEUE_SIZE 64

static input_event_t input_queue[INPUT_QUEUE_SIZE];
static int input_head;
static int input_tail;
static int input_count;
static int last_mouse_buttons;

/* Key repeat */
#define KEY_REPEAT_DELAY_MS  500   /* initial delay before repeat starts */
#define KEY_REPEAT_RATE_MS   30    /* repeat interval once repeating */
static int repeat_key;
static uint64_t repeat_first_time;   /* when key was first pressed */
static uint64_t repeat_last_time;    /* when last repeat was emitted */
static int repeat_active;            /* 1 = key is held down */

/* Gesture tracking for touch input */
static gesture_tracker_t gesture_tracker;

static void input_queue_push(const input_event_t *event) {
    if (input_count >= INPUT_QUEUE_SIZE) {
        input_head = (input_head + 1) % INPUT_QUEUE_SIZE;
        input_count--;
    }
    input_queue[input_tail] = *event;
    input_tail = (input_tail + 1) % INPUT_QUEUE_SIZE;
    input_count++;
}

static void input_push_gesture(gesture_type_t g, int x, int y, int dx, int dy) {
    input_event_t ev;
    ev.type = INPUT_EVENT_GESTURE;
    ev.data.gesture.gesture = g;
    ev.data.gesture.x = x;
    ev.data.gesture.y = y;
    ev.data.gesture.dx = dx;
    ev.data.gesture.dy = dy;
    ev.data.gesture.velocity = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    input_queue_push(&ev);
}

void input_init(void) {
    /* Configure PS/2 controller (enables keyboard+aux ports, polling mode). */
    ps2_init();
    keyboard_init();
    mouse_init();
    /* Default mouse bounds to a sane framebuffer-sized area; callers that
     * know the real dimensions (fb init / desktop) should override. */
    mouse_set_bounds(4095, 4095);
    gesture_init(&gesture_tracker);
    input_head = 0;
    input_tail = 0;
    input_count = 0;
    last_mouse_buttons = mouse_get_buttons();
    repeat_key = 0;
    repeat_active = 0;
}

static void push_key_event(int key) {
    input_event_t event;
    event.type = INPUT_EVENT_KEY;
    event.data.key.key = key;
    event.data.key.modifiers = (shift_down ? 1 : 0)
                             | (ctrl_down  ? 2 : 0)
                             | (alt_down   ? 4 : 0)
                             | (super_down ? 8 : 0);
    input_queue_push(&event);
}

void input_poll(void) {

    int key = aula_f87_is_available() ? aula_f87_poll() : 0;
    /* A detected USB keyboard must not mask a working PS/2 keyboard when
     * that USB device has no event ready. */
    if (!key && (inb(0x64) & 0x01) && !(inb(0x64) & 0x20))
        key = keyboard_poll();
    if (!key && usb_kbd_available())
        key = usb_kbd_poll();

    if (key) {
        push_key_event(key);
        uint64_t now = timer_get_milliseconds();
        if (key != repeat_key || !repeat_active) {
            repeat_key = key;
            repeat_first_time = now;
            repeat_last_time = now;
            repeat_active = 1;
        }
    } else if (!aula_f87_is_available()) {
        repeat_active = 0;
        repeat_key = 0;
    }

    /* Key repeat: if a key is held down and enough time has passed,
       emit repeat events. Skip modifiers and special keys. */
    if (repeat_active && repeat_key) {
        uint64_t now = timer_get_milliseconds();
        uint64_t elapsed = now - repeat_first_time;

        /* Don't repeat modifier keys, arrows, F-keys, Escape, or media keys */
        int repeatable = (repeat_key >= '!' && repeat_key <= '~')
                       || repeat_key == '\b' || repeat_key == '\t'
                       || repeat_key == '\n' || repeat_key == ' '
                       || (repeat_key >= KEY_KP_0 && repeat_key <= KEY_KP_9);

        if (repeatable && elapsed >= KEY_REPEAT_DELAY_MS) {
            uint64_t since_last = now - repeat_last_time;
            if (since_last >= KEY_REPEAT_RATE_MS) {
                push_key_event(repeat_key);
                repeat_last_time = now;
            }
        }
    }

    if (mouse_available()) {
        mouse_poll();
        int buttons = mouse_get_buttons();
        int dx = mouse_get_dx();
        int dy = mouse_get_dy();
        int wheel = mouse_get_wheel();
        /* Zero stale deltas immediately to prevent phantom events */
        mouse_zero_deltas();

        if (dx || dy || wheel || buttons != last_mouse_buttons) {
            input_event_t event;
            event.type = INPUT_EVENT_MOUSE;
            event.data.mouse.x = mouse_get_x();
            event.data.mouse.y = mouse_get_y();
            event.data.mouse.dx = dx;
            event.data.mouse.dy = dy;
            event.data.mouse.buttons = buttons;
            event.data.mouse.wheel = wheel;
            input_queue_push(&event);
            last_mouse_buttons = buttons;
        }
    }

    /* Logitech G305 Lightspeed Mouse */
    if (g305_is_available()) {
        static int prev_g305_buttons;
        int gdx = 0, gdy = 0, gbtn = 0, gwhl = 0;
        if (g305_poll(&gdx, &gdy, &gbtn, &gwhl)) {
            if (gdx || gdy || gwhl || gbtn != prev_g305_buttons) {
                input_event_t event;
                event.type = INPUT_EVENT_MOUSE;
                event.data.mouse.x = 0;
                event.data.mouse.y = 0;
                event.data.mouse.dx = gdx;
                event.data.mouse.dy = gdy;
                event.data.mouse.buttons = gbtn;
                event.data.mouse.wheel = gwhl;
                input_queue_push(&event);
                prev_g305_buttons = gbtn;
            }
        }
    }
    /* USB HID relative mouse (boot protocol) — integrate deltas into the
     * global absolute cursor position so poll_mouse_continuous / fb cursor
     * (and Qt) see movement even without PS/2. */
    else if (usb_mouse_available()) {
        static int prev_buttons;
        int mdx, mdy, mbuttons, mwheel;
        if (usb_mouse_poll(&mdx, &mdy, &mbuttons, &mwheel)) {
            if (mdx || mdy) {
                int nx = mouse_get_x() + mdx;
                int ny = mouse_get_y() + mdy;
                /* Clamp to the bounds configured on the global mouse state. */
                int maxx = 4095, maxy = 4095;
                /* Best-effort: use current position as reference; set_pos will
                 * be bounded by the next mouse_set_bounds caller, but clamp
                 * here against a large sane range to avoid wrap. */
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx > maxx) nx = maxx;
                if (ny > maxy) ny = maxy;
                mouse_set_pos(nx, ny);
            }
            if (mdx || mdy || mwheel || mbuttons != prev_buttons) {
                input_event_t event;
                event.type = INPUT_EVENT_MOUSE;
                event.data.mouse.x = mouse_get_x();
                event.data.mouse.y = mouse_get_y();
                event.data.mouse.dx = mdx;
                event.data.mouse.dy = mdy;
                event.data.mouse.buttons = mbuttons;
                event.data.mouse.wheel = mwheel;
                input_queue_push(&event);
                prev_buttons = mbuttons;
            }
        }
    }

    /* USB touch/tablet polling */
    if (usb_touch_available()) {
        static int prev_btn;
        int tx, ty, tbtn;
        if (usb_touch_poll(&tx, &ty, &tbtn)) {
            #include "../kernel/kprintf.h"
            kprintf("uhci-touch: polled x=%d y=%d btn=%d\n", tx, ty, tbtn);
            /* Scale tablet absolute coordinates (0..32767) to screen
             * resolution.  The kernel defaults to 1920x1080 VBE mode. */
            int sx = tx * 1920 / 32768;
            int sy = ty * 1080 / 32768;
            kprintf("uhci-mouse: set_pos(%d,%d)\n", sx, sy);
            mouse_set_pos(sx, sy);

            uint64_t now = timer_get_milliseconds();
            gesture_finger_t *finger = gesture_get_finger(&gesture_tracker, 0);

            input_event_t ev;
            ev.type = INPUT_EVENT_TOUCH;
            ev.data.touch.x = tx;
            ev.data.touch.y = ty;
            ev.data.touch.touch_id = 0;
            ev.data.touch.dx = finger ? (tx - finger->cur_x) : 0;
            ev.data.touch.dy = finger ? (ty - finger->cur_y) : 0;
            ev.data.touch.pressure = tbtn ? 100 : 0;
            input_queue_push(&ev);

            /* Gesture tracking */
            if (tbtn && !prev_btn) {
                gesture_touch_down(&gesture_tracker, tx, ty, 0, now);
            } else if (tbtn && prev_btn) {
                gesture_touch_move(&gesture_tracker, tx, ty, 0, now);
            } else if (!tbtn && prev_btn) {
                gesture_type_t gtype;
                int gdx, gdy;
                if (gesture_touch_up(&gesture_tracker, 0, now, &gtype, &gdx, &gdy) && gtype != GESTURE_NONE) {
                    gesture_finger_t *f = gesture_get_finger(&gesture_tracker, 0);
                    int sx = f ? f->start_x : tx;
                    int sy = f ? f->start_y : ty;
                    input_push_gesture(gtype, sx, sy, gdx, gdy);
                }
            }
            prev_btn = tbtn;
        }
    }
}

int input_available(void) {
    return input_count;
}

int input_get_event(input_event_t *event) {
    if (input_count == 0) return 0;
    *event = input_queue[input_head];
    input_head = (input_head + 1) % INPUT_QUEUE_SIZE;
    input_count--;
    return 1;
}

void input_flush_events(void) {
    input_head = 0;
    input_tail = 0;
    input_count = 0;
}
