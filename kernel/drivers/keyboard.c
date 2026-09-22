#include "keyboard.h"
#include "usb.h"
#include "../arch/x86_64/io.h"
#include "../kernel/string.h"

#define KB_DATA  0x60
#define KB_CMD   0x64
#define KB_STAT  0x64

/* ───── Ring buffer ───── */

static key_event_t key_buffer[KEY_BUFFER_SIZE];
static volatile int key_buf_head;
static volatile int key_buf_tail;

static void key_buf_push(int scancode, int keycode, int pressed) {
    int next = (key_buf_head + 1) % KEY_BUFFER_SIZE;
    if (next == key_buf_tail) return;
    key_buffer[key_buf_head].scancode = scancode;
    key_buffer[key_buf_head].keycode  = keycode;
    key_buffer[key_buf_head].pressed  = pressed;
    key_buffer[key_buf_head].shift    = shift_down;
    key_buffer[key_buf_head].ctrl     = ctrl_down;
    key_buffer[key_buf_head].alt      = alt_down;
    key_buffer[key_buf_head].super    = super_down;
    key_buf_head = next;
}

int keyboard_get_event(key_event_t *out) {
    if (key_buf_head == key_buf_tail) return 0;
    *out = key_buffer[key_buf_tail];
    key_buf_tail = (key_buf_tail + 1) % KEY_BUFFER_SIZE;
    return 1;
}

int keyboard_has_event(void) {
    return key_buf_head != key_buf_tail;
}

/* ───── Low-level I/O ───── */

static void kb_delay(void) {
    for (volatile int i = 0; i < 500; i++) asm volatile("pause");
}

static int kb_wait_output(int timeout_us) {
    for (int i = 0; i < timeout_us / 10; i++) {
        if (inb(KB_STAT) & 0x01) return 1;
        kb_delay();
    }
    return 0;
}

/* ───── PS/2 scancode set 1 tables (QEMU/8042 translated mode) ───── */

static const unsigned char normal_char[0x3A] = {
    /* 0x00-0x0F */ 0, 0x1B, '1','2','3','4','5','6','7','8','9','0','-','=',0x08,'\t',
    /* 0x10-0x1F */ 'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    /* 0x20-0x2F */ 'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    /* 0x30-0x39 */ 'b','n','m',',','.','/',0,'*',0,' '
};

static const unsigned char shift_char[0x3A] = {
    /* 0x00-0x0F */ 0, 0x1B, '!','@','#','$','%','^','&','*','(',')','_','+',0,'\t',
    /* 0x10-0x1F */ 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    /* 0x20-0x2F */ 'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    /* 0x30-0x39 */ 'B','N','M','<','>','?',0,'*',0,' '
};

/* E0-prefixed navigation keys (set 1): 0x47..0x53 */
static const unsigned char ext_nav[13] = {
    KEY_HOME, KEY_UP, KEY_PGUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0,
    KEY_END, KEY_DOWN, KEY_PGDN, KEY_INS, KEY_DEL
};

/* ───── Keyboard state ───── */

int shift_down;
int ctrl_down;
int alt_down;
int super_down;
static int caps_lock;

static int repeat_delay_ms = 500;
static int repeat_rate_ms  = 33;
static int active_scancode_set = 1;

static int custom_keymap[128];
static int use_custom_keymap;

/* ───── Scancode processing ───── */

static int process_scancode(uint8_t sc) {
    if (sc == 0xE0) {
        if (!kb_wait_output(50000)) return 0;
        uint8_t ext = inb(KB_DATA);
        uint8_t key = ext & 0x7F;

        if (key == 0x1D) { ctrl_down = !(ext & 0x80); return 0; }
        if (key == 0x38) { alt_down  = !(ext & 0x80); return 0; }
        if (key == 0x5B || key == 0x5C || key == 0x5D) {
            super_down = !(ext & 0x80);
            if (!(ext & 0x80)) {
                key_buf_push(sc, KEY_SUPER, 1);
                return KEY_SUPER;
            }
            return 0;
        }
        if (ext & 0x80) return 0;
        if (key >= 0x47 && key <= 0x53) return ext_nav[key - 0x47];
        if (key == 0x57 || key == 0x58) {
            int kf = (key == 0x57) ? KEY_F11 : KEY_F12;
            key_buf_push(sc, kf, 1);
            return kf;
        }
        return 0;
    }

    uint8_t key = sc & 0x7F;
    int pressed = !(sc & 0x80);

    if (key == 0x2A || key == 0x36) { shift_down = pressed; return 0; }
    if (key == 0x3A) {
        if (pressed) caps_lock = !caps_lock;
        return 0;
    }
    if (key == 0x1D) { ctrl_down = pressed; return 0; }
    if (key == 0x38) { alt_down  = pressed; return 0; }

    /* F1-F10 (set 1 scancodes 0x3B..0x44) */
    if (key >= 0x3B && key <= 0x44) {
        if (!pressed) return 0;
        int kf = KEY_F1 + (key - 0x3B);
        key_buf_push(sc, kf, 1);
        return kf;
    }
    /* F11/F12 (0x57/0x58) also arrive unprefixed from QEMU monitor sendkey,
       which omits the E0 byte that physical keyboards emit. 0x57/0x58 have
       no other meaning in set 1, so this is safe and matches hardware. */
    if (key == 0x57 || key == 0x58) {
        if (!pressed) return 0;
        int kf = (key == 0x57) ? KEY_F11 : KEY_F12;
        key_buf_push(sc, kf, 1);
        return kf;
    }
    if (!pressed) return 0;
    if (key > 0x39) return 0;

    char c = normal_char[key];
    if (shift_down) {
        c = shift_char[key];
        if (!c) c = normal_char[key];
    }

    if (caps_lock) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }

    if (ctrl_down) {
        if (c >= 'a' && c <= 'z') return c - 'a' + 1;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
        if (c == '[') return 0x1B;
        if (c == '\\') return 0x1C;
        if (c == ']') return 0x1D;
        if (c == '^') return 0x1E;
        if (c == '_') return 0x1F;
        if (c == '`') return 0x20;
    }

    int keycode = (unsigned char)c;
    if (keycode) key_buf_push(sc, keycode, 1);
    return keycode;
}

/* ───── Init ───── */

void keyboard_init(void) {
    shift_down = 0;
    ctrl_down = 0;
    alt_down = 0;
    super_down = 0;
    caps_lock = 0;
    key_buf_head = 0;
    key_buf_tail = 0;
    use_custom_keymap = 0;

    /* Drain stale data */
    for (int i = 0; i < 16; i++) {
        if (inb(KB_STAT) & 0x01) (void)inb(KB_DATA);
    }

    /* Enable the keyboard interface (8042 command 0xAE). QEMU's i8042
       defaults to translated scancode set 1, so no set switching here. */
    while (inb(KB_STAT) & 0x02) ;
    outb(KB_CMD, 0xAE);
}

/* ───── Public API ───── */

int keyboard_getchar(void) {
    int c;
    while ((c = keyboard_poll()) == 0);
    return c;
}

int keyboard_poll(void) {
    uint8_t status = inb(KB_STAT);
    if ((status & 0x01) && !(status & 0x20)) {
        uint8_t sc = inb(KB_DATA);
        return process_scancode(sc);
    }
    if (usb_kbd_available()) return usb_kbd_poll();
    return 0;
}

int keyboard_has_input(void) {
    uint8_t status = inb(KB_STAT);
    return ((status & 0x01) && !(status & 0x20)) || usb_kbd_available() || keyboard_has_event();
}

void keyboard_flush(void) {
    key_buf_head = 0;
    key_buf_tail = 0;
    for (int i = 0; i < 256; i++) {
        if (!(inb(KB_STAT) & 0x01)) break;
        (void)inb(KB_DATA);
    }
}

int keyboard_is_alt_down(void)   { return alt_down; }
int keyboard_is_ctrl_down(void)  { return ctrl_down; }
int keyboard_is_super_down(void) { return super_down; }
int keyboard_is_shift_down(void) { return shift_down; }

void keyboard_set_repeat(int delay_ms, int rate_ms) {
    if (delay_ms < 100) delay_ms = 100;
    if (delay_ms > 2000) delay_ms = 2000;
    if (rate_ms < 15) rate_ms = 15;
    if (rate_ms > 500) rate_ms = 500;
    repeat_delay_ms = delay_ms;
    repeat_rate_ms  = rate_ms;
}

int keyboard_get_repeat_delay(void) { return repeat_delay_ms; }
int keyboard_get_repeat_rate(void)  { return repeat_rate_ms; }

void keyboard_set_scancode_set(int set) {
    if (set < 1 || set > 3) return;
    active_scancode_set = set;
}

int keyboard_get_scancode_set(void) {
    return active_scancode_set;
}

void keyboard_set_keymap(const int *map) {
    for (int i = 0; i < 128; i++) custom_keymap[i] = map[i];
    use_custom_keymap = 1;
}

void keyboard_reset_keymap(void) {
    use_custom_keymap = 0;
}

int *keyboard_get_keymap(void) {
    return custom_keymap;
}
