#ifndef KBD_AULA_F87_H
#define KBD_AULA_F87_H

#include "types.h"

/* Known AULA Vendor and Product IDs for F87 / F87 Pro mechanical keyboards */
#define AULA_VID_258A                    0x258A
#define AULA_PID_F87_PRO                 0x0049  /* AULA F87 Pro Tri-Mode / 2.4G */
#define AULA_PID_F87_STD                 0x003A  /* AULA F87 Standard */

#define AULA_VID_0C45                    0x0C45
#define AULA_PID_F87_MECH                0x8513  /* AULA Sonix mechanical MCU */

#define AULA_VID_342D                    0x342D
#define AULA_PID_F87_TRIMODE             0xE3A7  /* AULA F87 Pro Wireless */

#define AULA_VID_1A2C                    0x1A2C
#define AULA_PID_F87_WIRED               0x0C21  /* AULA Wired Controller */

#define AULA_VID_CX                      0x3554  /* CX 2.4G Receiver (AULA F87 Pro Wireless) */
#define AULA_PID_FA09                    0xFA09  /* AULA F87 2.4G Dongle */

/* AULA RGB Backlight Modes */
#define AULA_RGB_MODE_STATIC             0
#define AULA_RGB_MODE_BREATHING          1
#define AULA_RGB_MODE_RAINBOW            2
#define AULA_RGB_MODE_RIPPLE             3
#define AULA_RGB_MODE_NEON               4
#define AULA_RGB_MODE_OFF                5
#define AULA_RGB_MODE_UNKNOWN           -1

/* AULA Multimedia Extended Scancodes */
#define AULA_KEY_MEDIA_PLAYER            0xE001
#define AULA_KEY_VOL_DOWN                0xE002
#define AULA_KEY_VOL_UP                  0xE003
#define AULA_KEY_MUTE                    0xE004
#define AULA_KEY_STOP                    0xE005
#define AULA_KEY_PREV_TRACK              0xE006
#define AULA_KEY_PLAY_PAUSE              0xE007
#define AULA_KEY_NEXT_TRACK              0xE008
#define AULA_KEY_EMAIL                   0xE009
#define AULA_KEY_BROWSER_HOME            0xE00A
#define AULA_KEY_MY_COMPUTER             0xE00B
#define AULA_KEY_CALCULATOR              0xE00C

/* AULA F87 Internal State allocated in RAM */
struct aula_f87_state {
    int      present;
    int      bus_type;         /* 1 = UHCI, 2 = EHCI, 3 = XHCI */
    int      dev_address;
    uint16_t vendor_id;
    uint16_t product_id;
    int      win_lock;         /* 1 = Windows key disabled (game mode) */
    int      rgb_mode;         /* AULA_RGB_MODE_UNKNOWN until device control exists */
    int      rgb_brightness;   /* -1 until device control exists */
    int      rgb_speed;        /* -1 until device control exists */
    int      nkro_active;      /* 1 only when an NKRO report is parsed */
    uint8_t  prev_report[32];  /* Previous NKRO report */
    int      prev_valid;
    uint64_t total_keystrokes;
};

static inline int aula_f87_device_matches(uint16_t vendor_id, uint16_t product_id) {
    return (vendor_id == AULA_VID_258A &&
            (product_id == AULA_PID_F87_PRO || product_id == AULA_PID_F87_STD)) ||
           (vendor_id == AULA_VID_0C45 && product_id == AULA_PID_F87_MECH) ||
           (vendor_id == AULA_VID_342D && product_id == AULA_PID_F87_TRIMODE) ||
           (vendor_id == AULA_VID_1A2C && product_id == AULA_PID_F87_WIRED) ||
           (vendor_id == AULA_VID_CX && product_id == AULA_PID_FA09);
}

/* Driver Interface */
int  aula_f87_probe(void);
int  aula_f87_init(void *ram_instance);
void aula_f87_cleanup(void);
int  aula_f87_is_available(void);
int  aula_f87_poll(void);
int  aula_f87_set_rgb_mode(int mode);
int  aula_f87_get_rgb_mode(void);
int  aula_f87_set_brightness(int level);
int  aula_f87_toggle_win_lock(void);
int  aula_f87_is_win_locked(void);
void aula_f87_print_info(void);
struct aula_f87_state *aula_f87_get_state(void);

#endif /* KBD_AULA_F87_H */
