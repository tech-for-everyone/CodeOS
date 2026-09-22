#ifndef HID_KEYS_H
#define HID_KEYS_H

#include "keyboard.h"

/* USB HID Keyboard Usage Table (HID Usage Tables, Section 10)
 * These map USB HID usage IDs (from boot protocol report) to
 * internal KEY_* codes for special keys, or 0 for standard keys
 * that go through hid_to_ascii[]. */

/* USB HID Usage IDs for special keys */
#define HID_KEY_ESC       0x29
#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_TAB       0x2B
#define HID_KEY_ENTER     0x28
#define HID_KEY_SPACE     0x2C
#define HID_KEY_CAPS      0x39

#define HID_KEY_RIGHT     0x4F
#define HID_KEY_LEFT      0x50
#define HID_KEY_DOWN      0x51
#define HID_KEY_UP        0x52
#define HID_KEY_INS       0x49
#define HID_KEY_HOME      0x4A
#define HID_KEY_PGUP      0x4B
#define HID_KEY_DEL       0x4C
#define HID_KEY_END       0x4D
#define HID_KEY_PGDN      0x4E

#define HID_KEY_F1        0x3A
#define HID_KEY_F2        0x3B
#define HID_KEY_F3        0x3C
#define HID_KEY_F4        0x3D
#define HID_KEY_F5        0x3E
#define HID_KEY_F6        0x3F
#define HID_KEY_F7        0x40
#define HID_KEY_F8        0x41
#define HID_KEY_F9        0x42
#define HID_KEY_F10       0x43
#define HID_KEY_F11       0x44
#define HID_KEY_F12       0x45

/* Numpad keys */
#define HID_KEY_NUMLOCK   0x53
#define HID_KEY_KP_SLASH  0x54
#define HID_KEY_KP_STAR   0x55
#define HID_KEY_KP_MINUS  0x56
#define HID_KEY_KP_PLUS   0x57
#define HID_KEY_KP_ENTER  0x58
#define HID_KEY_KP_1      0x59
#define HID_KEY_KP_2      0x5A
#define HID_KEY_KP_3      0x5B
#define HID_KEY_KP_4      0x5C
#define HID_KEY_KP_5      0x5D
#define HID_KEY_KP_6      0x5E
#define HID_KEY_KP_7      0x5F
#define HID_KEY_KP_8      0x60
#define HID_KEY_KP_9      0x61
#define HID_KEY_KP_0      0x62
#define HID_KEY_KP_DOT    0x63

/* Print Screen, Scroll Lock, Pause */
#define HID_KEY_PRINT_SCREEN 0x46
#define HID_KEY_SCROLL_LOCK   0x47
#define HID_KEY_PAUSE         0x48

/* Consumer Control Usage Page (0x0C) — Media Keys */
#define HID_CONSUMER_VOL_UP      0x00E9
#define HID_CONSUMER_VOL_DOWN    0x00EA
#define HID_CONSUMER_MUTE        0x00E2
#define HID_CONSUMER_PLAY_PAUSE  0x00CD
#define HID_CONSUMER_STOP        0x00B7
#define HID_CONSUMER_NEXT_TRACK  0x00B5
#define HID_CONSUMER_PREV_TRACK  0x00B6
#define HID_CONSUMER_EJECT       0x00B8
#define HID_CONSUMER_POWER       0x0030
#define HID_CONSUMER_SLEEP       0x0032
#define HID_CONSUMER_WWW_SEARCH  0x221
#define HID_CONSUMER_WWW_HOME    0x223
#define HID_CONSUMER_WWW_BACK    0x224
#define HID_CONSUMER_WWW_FORWARD 0x225
#define HID_CONSUMER_APP_SELECT  0x0183

/* Modifier bit masks in report[0] */
#define HID_MOD_LCTRL     0x01
#define HID_MOD_LSHIFT    0x02
#define HID_MOD_LALT      0x04
#define HID_MOD_LGUI      0x08
#define HID_MOD_RCTRL     0x10
#define HID_MOD_RSHIFT    0x20
#define HID_MOD_RALT      0x40
#define HID_MOD_RGUI      0x80

#define HID_MOD_CTRL      (HID_MOD_LCTRL  | HID_MOD_RCTRL)
#define HID_MOD_SHIFT     (HID_MOD_LSHIFT | HID_MOD_RSHIFT)
#define HID_MOD_ALT       (HID_MOD_LALT   | HID_MOD_RALT)
#define HID_MOD_GUI       (HID_MOD_LGUI   | HID_MOD_RGUI)

/* Translate a USB HID usage ID for a special key to internal KEY_* code.
 * Returns 0 if the usage ID is not a special key (i.e., alphanumeric/punctuation). */
static inline int hid_special_key(uint16_t usage) {
    switch (usage) {
        /* Standard special keys */
        case HID_KEY_ESC:       return 0x1B;
        case HID_KEY_TAB:       return '\t';
        case HID_KEY_ENTER:     return '\n';
        case HID_KEY_SPACE:     return ' ';
        case HID_KEY_BACKSPACE: return '\b';
        case HID_KEY_CAPS:      return 0;  /* handled separately */

        /* Arrow keys */
        case HID_KEY_UP:        return KEY_UP;
        case HID_KEY_DOWN:      return KEY_DOWN;
        case HID_KEY_LEFT:      return KEY_LEFT;
        case HID_KEY_RIGHT:     return KEY_RIGHT;

        /* Navigation */
        case HID_KEY_HOME:      return KEY_HOME;
        case HID_KEY_END:       return KEY_END;
        case HID_KEY_PGUP:      return KEY_PGUP;
        case HID_KEY_PGDN:      return KEY_PGDN;
        case HID_KEY_DEL:       return KEY_DEL;
        case HID_KEY_INS:       return KEY_INS;

        /* Function keys */
        case HID_KEY_F1:        return KEY_F1;
        case HID_KEY_F2:        return KEY_F2;
        case HID_KEY_F3:        return KEY_F3;
        case HID_KEY_F4:        return KEY_F4;
        case HID_KEY_F5:        return KEY_F5;
        case HID_KEY_F6:        return KEY_F6;
        case HID_KEY_F7:        return KEY_F7;
        case HID_KEY_F8:        return KEY_F8;
        case HID_KEY_F9:        return KEY_F9;
        case HID_KEY_F10:       return KEY_F10;
        case HID_KEY_F11:       return KEY_F11;
        case HID_KEY_F12:       return KEY_F12;

        /* Print Screen, Scroll Lock, Pause */
        case HID_KEY_PRINT_SCREEN: return KEY_PRTSC;
        case HID_KEY_SCROLL_LOCK:  return KEY_SCRLK;
        case HID_KEY_PAUSE:        return KEY_PAUSE;

        /* Numpad */
        case HID_KEY_NUMLOCK:   return KEY_NUM;
        case HID_KEY_KP_0:      return KEY_KP_0;
        case HID_KEY_KP_1:      return KEY_KP_1;
        case HID_KEY_KP_2:      return KEY_KP_2;
        case HID_KEY_KP_3:      return KEY_KP_3;
        case HID_KEY_KP_4:      return KEY_KP_4;
        case HID_KEY_KP_5:      return KEY_KP_5;
        case HID_KEY_KP_6:      return KEY_KP_6;
        case HID_KEY_KP_7:      return KEY_KP_7;
        case HID_KEY_KP_8:      return KEY_KP_8;
        case HID_KEY_KP_9:      return KEY_KP_9;
        case HID_KEY_KP_DOT:    return KEY_KP_DOT;
        case HID_KEY_KP_SLASH:  return KEY_KP_DIV;
        case HID_KEY_KP_STAR:   return KEY_KP_MUL;
        case HID_KEY_KP_MINUS:  return KEY_KP_SUB;
        case HID_KEY_KP_PLUS:   return KEY_KP_ADD;
        case HID_KEY_KP_ENTER:  return KEY_KP_ENT;

        /* Media keys (Consumer Control page 0x0C) */
        case HID_CONSUMER_VOL_UP:     return KEY_MEDIA_VOL_UP;
        case HID_CONSUMER_VOL_DOWN:   return KEY_MEDIA_VOL_DOWN;
        case HID_CONSUMER_MUTE:       return KEY_MEDIA_MUTE;
        case HID_CONSUMER_PLAY_PAUSE: return KEY_MEDIA_PLAY;
        case HID_CONSUMER_STOP:       return KEY_MEDIA_STOP;
        case HID_CONSUMER_NEXT_TRACK: return KEY_MEDIA_NEXT;
        case HID_CONSUMER_PREV_TRACK: return KEY_MEDIA_PREV;
        case HID_CONSUMER_EJECT:      return KEY_MEDIA_EJECT;

        default:                return 0;
    }
}

/* Translate a consumer control usage ID (media keys) to internal KEY_* code.
 * Returns 0 if not a recognized media key. */
static inline int hid_consumer_key(uint16_t usage) {
    switch (usage) {
        case HID_CONSUMER_VOL_UP:     return KEY_MEDIA_VOL_UP;
        case HID_CONSUMER_VOL_DOWN:   return KEY_MEDIA_VOL_DOWN;
        case HID_CONSUMER_MUTE:       return KEY_MEDIA_MUTE;
        case HID_CONSUMER_PLAY_PAUSE: return KEY_MEDIA_PLAY;
        case HID_CONSUMER_STOP:       return KEY_MEDIA_STOP;
        case HID_CONSUMER_NEXT_TRACK: return KEY_MEDIA_NEXT;
        case HID_CONSUMER_PREV_TRACK: return KEY_MEDIA_PREV;
        case HID_CONSUMER_EJECT:      return KEY_MEDIA_EJECT;
        case HID_CONSUMER_WWW_HOME:   return KEY_MEDIA_STOP;  /* reuse */
        case HID_CONSUMER_WWW_SEARCH: return 0;
        case HID_CONSUMER_WWW_BACK:   return 0;
        case HID_CONSUMER_WWW_FORWARD:return 0;
        case HID_CONSUMER_APP_SELECT: return KEY_SUPER;
        default:                      return 0;
    }
}

#endif
