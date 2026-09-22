#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DEL    0x86
#define KEY_PGUP   0x87
#define KEY_PGDN   0x88
#define KEY_INS    0x89
#define KEY_SUPER  0x8A
#define KEY_F1     0x8B
#define KEY_F2     0x8C
#define KEY_F3     0x8D
#define KEY_F4     0x8E
#define KEY_F5     0x8F
#define KEY_F6     0x90
#define KEY_F7     0x91
#define KEY_F8     0x92
#define KEY_F9     0x93
#define KEY_F10    0x94
#define KEY_F11    0x95
#define KEY_F12    0x96
#define KEY_PRTSC  0x97
#define KEY_SCRLK  0x98
#define KEY_PAUSE  0x99
#define KEY_NUM    0x9A
#define KEY_KP_0   0x9B
#define KEY_KP_1   0x9C
#define KEY_KP_2   0x9D
#define KEY_KP_3   0x9E
#define KEY_KP_4   0x9F
#define KEY_KP_5   0xA0
#define KEY_KP_6   0xA1
#define KEY_KP_7   0xA2
#define KEY_KP_8   0xA3
#define KEY_KP_9   0xA4
#define KEY_KP_DOT 0xA5
#define KEY_KP_DIV 0xA6
#define KEY_KP_MUL 0xA7
#define KEY_KP_SUB 0xA8
#define KEY_KP_ADD 0xA9
#define KEY_KP_ENT 0xAA
#define KEY_MEDIA_VOL_UP    0xB0
#define KEY_MEDIA_VOL_DOWN  0xB1
#define KEY_MEDIA_MUTE      0xB2
#define KEY_MEDIA_PLAY      0xB3
#define KEY_MEDIA_PAUSE     0xB4
#define KEY_MEDIA_STOP      0xB5
#define KEY_MEDIA_NEXT      0xB6
#define KEY_MEDIA_PREV      0xB7
#define KEY_MEDIA_EJECT     0xB8

/* ── Key event (for ring buffer) ── */
typedef struct {
    int scancode;       /* raw scancode */
    int keycode;        /* translated keycode (KEY_* or ASCII) */
    int pressed;        /* 1 = press, 0 = release */
    int shift, ctrl, alt, super;  /* modifier state at time of event */
} key_event_t;

#define KEY_BUFFER_SIZE 128

void keyboard_init(void);
int  keyboard_getchar(void);
int  keyboard_poll(void);
int  keyboard_has_input(void);
void keyboard_flush(void);

/* Ring buffer API (for callers that need full key events) */
int  keyboard_get_event(key_event_t *out);
int  keyboard_has_event(void);

/* Typematic repeat configuration */
void keyboard_set_repeat(int delay_ms, int rate_ms);
int  keyboard_get_repeat_delay(void);
int  keyboard_get_repeat_rate(void);

/* Scancode set selection */
void keyboard_set_scancode_set(int set);
int  keyboard_get_scancode_set(void);

/* Keymap: 128-entry translation table (scancode → keycode) */
void keyboard_set_keymap(const int *map);
void keyboard_reset_keymap(void);
int *keyboard_get_keymap(void);

/* modifier lookup */
int keyboard_is_alt_down(void);
int keyboard_is_ctrl_down(void);
int keyboard_is_shift_down(void);
int keyboard_is_super_down(void);

/* Direct modifier state (written by USB keyboard drivers too) */
extern int shift_down, ctrl_down, alt_down, super_down;

#endif
