#include "mouse.h"
#include "ps2.h"
#include "../arch/x86_64/io.h"
#include "../drivers/timer.h"

#define KB_STATUS 0x64
#define KB_CMD    0x64
#define KB_DATA   0x60

#define MOUSE_ACK 0xFA
#define DEBOUNCE_MS 50
#define DOUBLE_CLICK_MS 350

static int mouse_x;
static int mouse_y;
static int mouse_max_x;
static int mouse_max_y;
static int mouse_dx;
static int mouse_dy;
static int mouse_wheel_delta;
static int mouse_buttons;
static int mouse_exists;
static int mouse_has_wheel_var;
static int mouse_has_extra_var;
static int mouse_packet_phase;
static int mouse_packet[4];

/* Sensitivity: 100 = 1:1 */
static int mouse_sensitivity = 100;
/* Acceleration level */
static int mouse_accel_level = 1;

/* Scroll speed multiplier */
static int mouse_scroll_speed = 1;

/* Previous button state for edge detection */
static int prev_buttons;
static int just_pressed_buttons;
static int just_released_buttons;

/* Debounce state: per-button last-press timestamp */
static uint64_t last_press_ms[5];  /* indexed by button bit position */
static uint64_t last_release_ms[5];

/* Double-click state: per-button last-click timestamp */
static uint64_t last_click_ms[5];
static int double_click_pending[5];

/* Event buffer */
static mouse_event_t event_buf[MOUSE_EVENT_BUF_SIZE];
static volatile int event_head;
static volatile int event_tail;

static void event_push(int x, int y, int dx, int dy, int buttons, int changed, int wheel) {
    int next = (event_head + 1) % MOUSE_EVENT_BUF_SIZE;
    if (next == event_tail) return;
    event_buf[event_head].x = x;
    event_buf[event_head].y = y;
    event_buf[event_head].dx = dx;
    event_buf[event_head].dy = dy;
    event_buf[event_head].buttons = buttons;
    event_buf[event_head].buttons_changed = changed;
    event_buf[event_head].wheel = wheel;
    event_buf[event_head].timestamp = timer_get_milliseconds();
    event_head = next;
}

static int mouse_write(uint8_t b) {
    int timeout = 100000;
    while (--timeout) {
        if (!(inb(KB_STATUS) & 0x02)) {
            outb(KB_DATA, b);
            return 1;
        }
        if (timeout % 100 == 0) asm volatile("pause");
    }
    return 0;
}

static int mouse_write_cmd(uint8_t cmd) {
    int timeout = 100000;
    while (--timeout) {
        if (!(inb(KB_STATUS) & 0x02)) {
            outb(KB_CMD, 0xD4);
            break;
        }
        if (timeout % 100 == 0) asm volatile("pause");
    }
    if (!timeout) return 0;
    return mouse_write(cmd);
}

static int mouse_read_ack(void) {
    int timeout = 100000;
    while (--timeout) {
        if (inb(KB_STATUS) & 0x01) {
            if (inb(KB_DATA) == MOUSE_ACK) return 1;
            return 0;
        }
        if (timeout % 100 == 0) asm volatile("pause");
    }
    return 0;
}

static int mouse_write_param(uint8_t cmd, uint8_t param) {
    if (!mouse_write_cmd(cmd)) return 0;
    if (!mouse_read_ack()) return 0;
    if (!mouse_write_cmd(param)) return 0;
    return mouse_read_ack();
}

static int mouse_read_device_id(void) {
    if (!mouse_write_cmd(0xF2)) return -1;
    if (!mouse_read_ack()) return -1;

    int timeout = 100000;
    while (--timeout) {
        if (inb(KB_STATUS) & 0x01) {
            return inb(KB_DATA);
        }
    }
    return -1;
}

static int mouse_enable_wheel(void) {
    if (!mouse_write_param(0xF3, 200)) return 0;
    {
        if (!mouse_write_param(0xF3, 200)) return 0;
        if (!mouse_write_param(0xF3, 80)) return 0;
        int id = mouse_read_device_id();
        if (id == 0x04) return 2;
    }
    {
        if (!mouse_write_param(0xF3, 100)) return 0;
        if (!mouse_write_param(0xF3, 80)) return 0;
        int id = mouse_read_device_id();
        if (id == 0x03) return 1;
    }
    return 0;
}

static int mouse_enable_reporting(void) {
    return mouse_write_cmd(0xF4) && mouse_read_ack();
}

/* ── Acceleration curve ── */

static int apply_acceleration(int raw_delta) {
    if (mouse_accel_level == 0 || raw_delta == 0) return raw_delta;

    int abs_delta = raw_delta < 0 ? -raw_delta : raw_delta;
    int sign = raw_delta < 0 ? -1 : 1;

    /* Quadratic-ish acceleration: delta * (1 + level * abs_delta / 1000) */
    int accel;
    switch (mouse_accel_level) {
        case 1: /* low: linear boost */
            accel = abs_delta + (abs_delta * abs_delta + 500) / 1000;
            break;
        case 2: /* medium: quadratic */
            accel = abs_delta + (abs_delta * abs_delta + 200) / 400;
            break;
        case 3: /* high: aggressive */
            accel = abs_delta + (abs_delta * abs_delta) / 150;
            break;
        default:
            accel = abs_delta;
            break;
    }

    return sign * accel;
}

/* ── Sensitivity scaling ── */

static int apply_sensitivity(int delta) {
    return (delta * mouse_sensitivity + 50) / 100;
}

/* ── Scroll speed scaling ── */

static int apply_scroll_speed(int raw_wheel) {
    if (raw_wheel == 0) return 0;
    int sign = raw_wheel < 0 ? -1 : 1;
    int abs_w = raw_wheel < 0 ? -raw_wheel : raw_wheel;
    int speed;
    switch (mouse_scroll_speed) {
        case 1: speed = abs_w; break;         /* normal */
        case 2: speed = abs_w * 3; break;     /* fast */
        case 3: speed = abs_w * 5; break;     /* very fast */
        default: speed = abs_w; break;
    }
    return sign * speed;
}

void mouse_init(void) {
    mouse_x = 0;
    mouse_y = 0;
    mouse_max_x = 4096;
    mouse_max_y = 4096;
    mouse_dx = 0;
    mouse_dy = 0;
    mouse_wheel_delta = 0;
    mouse_buttons = 0;
    prev_buttons = 0;
    just_pressed_buttons = 0;
    just_released_buttons = 0;
    mouse_exists = 0;
    mouse_has_wheel_var = 0;
    mouse_has_extra_var = 0;
    mouse_packet_phase = 0;
    event_head = 0;
    event_tail = 0;

    for (int i = 0; i < 5; i++) {
        last_press_ms[i] = 0;
        last_release_ms[i] = 0;
        last_click_ms[i] = 0;
        double_click_pending[i] = 0;
    }

    ps2_flush();

    int w = mouse_enable_wheel();
    mouse_has_wheel_var = w > 0;
    mouse_has_extra_var = (w == 2);
    if (!mouse_enable_reporting()) return;

    mouse_exists = 1;
}

void mouse_reset(void) {
    mouse_init();
}

void mouse_poll(void) {
    if (!mouse_exists) return;
    static int stall;
    int packet_size = mouse_has_wheel_var ? 4 : 3;

    uint8_t stat;
    while ((stat = inb(KB_STATUS)) & 0x20) {
        if (!(stat & 0x01)) break;
        uint8_t data = inb(KB_DATA);

        if (data == MOUSE_ACK) { mouse_packet_phase = 0; continue; }

        if (mouse_packet_phase == 0 && !(data & 0x08)) continue;
        if (mouse_packet_phase >= 4) { mouse_packet_phase = 0; continue; }

        mouse_packet[mouse_packet_phase++] = data;
        if (mouse_packet_phase < packet_size) continue;

        mouse_packet_phase = 0;
        uint8_t b0 = mouse_packet[0];

        if (!(b0 & 0x08)) continue;

        int raw_dx = (int)(int8_t)mouse_packet[1];
        int raw_dy = (int)(int8_t)mouse_packet[2];

        if (b0 & 0x40) raw_dx = 0;
        if (b0 & 0x80) raw_dy = 0;

        /* Apply acceleration */
        int dx = apply_acceleration(raw_dx);
        int dy = apply_acceleration(raw_dy);

        /* Apply sensitivity */
        dx = apply_sensitivity(dx);
        dy = apply_sensitivity(dy);

        mouse_x += dx;
        mouse_y -= dy;

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x > mouse_max_x) mouse_x = mouse_max_x;
        if (mouse_y > mouse_max_y) mouse_y = mouse_max_y;

        mouse_dx = dx;
        mouse_dy = dy;

        int raw_wheel = mouse_has_wheel_var ? (int)(int8_t)mouse_packet[3] : 0;
        mouse_wheel_delta = apply_scroll_speed(raw_wheel);

        int new_buttons = 0;
        if (b0 & 0x01) new_buttons |= MOUSE_LEFT;
        if (b0 & 0x02) new_buttons |= MOUSE_RIGHT;
        if (b0 & 0x04) new_buttons |= MOUSE_MIDDLE;

        if (mouse_has_extra_var) {
            uint8_t b3 = (uint8_t)mouse_packet[3];
            if (b3 & 0x10) new_buttons |= MOUSE_X1;
            if (b3 & 0x20) new_buttons |= MOUSE_X2;
        }

        /* Edge detection */
        int changed = new_buttons ^ prev_buttons;
        int pressed = changed & new_buttons;
        int released = changed & prev_buttons;

        /* Debounce: only register press if enough time since last press */
        uint64_t now = timer_get_milliseconds();
        just_pressed_buttons = 0;
        just_released_buttons = 0;

        for (int bit = 0; bit < 5; bit++) {
            int mask = 1 << bit;
            if (pressed & mask) {
                if (now - last_press_ms[bit] > DEBOUNCE_MS) {
                    just_pressed_buttons |= mask;
                    last_press_ms[bit] = now;

                    /* Double-click detection */
                    if (now - last_click_ms[bit] < DOUBLE_CLICK_MS) {
                        double_click_pending[bit] = 1;
                    }
                    last_click_ms[bit] = now;
                }
            }
            if (released & mask) {
                if (now - last_release_ms[bit] > DEBOUNCE_MS) {
                    just_released_buttons |= mask;
                    last_release_ms[bit] = now;
                }
            }
        }

        prev_buttons = new_buttons;
        mouse_buttons = new_buttons;

        /* Push event */
        event_push(mouse_x, mouse_y, dx, dy, new_buttons, changed, mouse_wheel_delta);

        stall = 0;
    }

    if (mouse_packet_phase) {
        if (++stall > 100) { mouse_packet_phase = 0; stall = 0; }
    } else {
        stall = 0;
    }
}

void mouse_set_pos(int x, int y) { mouse_x = x; mouse_y = y; }
void mouse_set_bounds(int max_x, int max_y) { mouse_max_x = max_x; mouse_max_y = max_y; }
void mouse_zero_deltas(void) { mouse_dx = 0; mouse_dy = 0; mouse_wheel_delta = 0; }

int mouse_get_x(void)       { return mouse_x; }
int mouse_get_y(void)       { return mouse_y; }
int mouse_get_dx(void)      { return mouse_dx; }
int mouse_get_dy(void)      { return mouse_dy; }
int mouse_get_wheel(void)   { return mouse_wheel_delta; }
int mouse_get_buttons(void) { return mouse_buttons; }
int mouse_available(void)   { return mouse_exists; }
int mouse_has_wheel(void)   { return mouse_has_wheel_var; }
int mouse_has_extra_buttons(void) { return mouse_has_extra_var; }

void mouse_set_sensitivity(int percent) {
    if (percent < 25) percent = 25;
    if (percent > 400) percent = 400;
    mouse_sensitivity = percent;
}

int mouse_get_sensitivity(void) { return mouse_sensitivity; }

void mouse_set_acceleration(int level) {
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    mouse_accel_level = level;
}

int mouse_get_acceleration(void) { return mouse_accel_level; }

void mouse_set_scroll_speed(int speed) {
    if (speed < 1) speed = 1;
    if (speed > 3) speed = 3;
    mouse_scroll_speed = speed;
}

int mouse_get_scroll_speed(void) { return mouse_scroll_speed; }

int mouse_double_clicked(int button) {
    for (int bit = 0; bit < 5; bit++) {
        if (button & (1 << bit)) {
            if (double_click_pending[bit]) {
                double_click_pending[bit] = 0;
                return 1;
            }
        }
    }
    return 0;
}

uint64_t mouse_last_click_ms(int button) {
    for (int bit = 0; bit < 5; bit++) {
        if (button & (1 << bit)) return last_click_ms[bit];
    }
    return 0;
}

int mouse_button_just_pressed(int button) {
    return (just_pressed_buttons & button) != 0;
}

int mouse_button_just_released(int button) {
    return (just_released_buttons & button) != 0;
}

int mouse_get_event(mouse_event_t *out) {
    if (event_head == event_tail) return 0;
    *out = event_buf[event_tail];
    event_tail = (event_tail + 1) % MOUSE_EVENT_BUF_SIZE;
    return 1;
}

int mouse_has_event(void) {
    return event_head != event_tail;
}
