#ifndef MOUSE_G305_H
#define MOUSE_G305_H

#include "types.h"

/* Logitech Vendor ID */
#define LOGITECH_VID                     0x046D

/* Logitech G305 and Lightspeed Receiver Product IDs */
#define LOGITECH_PID_G305_RECEIVER       0xC53F  /* Lightspeed receiver (G305 HERO) */
#define LOGITECH_PID_LIGHTSPEED_RECEIVER 0xC539  /* High-speed wireless receiver */
#define LOGITECH_PID_UNIFYING_LIGHTSPEED 0xC52B  /* Multi-device wireless receiver */
#define LOGITECH_PID_G305_DIRECT         0x4074  /* G305 mouse ID */

/* HID++ 2.0 Report IDs and Protocol definitions */
#define HIDPP_SHORT_REPORT_ID            0x10    /* 7-byte message */
#define HIDPP_LONG_REPORT_ID             0x11    /* 20-byte message */
#define HIDPP_VERY_LONG_REPORT_ID        0x20    /* 64-byte message */

#define HIDPP_DEVICE_DEFAULT             0x01
#define HIDPP_DEVICE_RECEIVER            0xFF

/* HID++ 2.0 Feature Indices */
#define HIDPP_FEATURE_ROOT               0x0000
#define HIDPP_FEATURE_FEATURE_SET        0x0001
#define HIDPP_FEATURE_DEVICE_INFO        0x0003
#define HIDPP_FEATURE_BATTERY_STATUS     0x1000
#define HIDPP_FEATURE_HIRES_WHEEL        0x2121
#define HIDPP_FEATURE_ADJUSTABLE_DPI     0x2201
#define HIDPP_FEATURE_POLLING_RATE       0x2000   /* Performance/polling rate control */

/* G305 Performance Modes */
#define G305_MODE_ENDURANCE              0       /* 125 Hz (8ms) power-save */
#define G305_MODE_PERFORMANCE            1       /* 1000 Hz (1ms) gaming */
#define G305_MODE_UNKNOWN               -1

/* G305 Button bitmask */
#define G305_BTN_LEFT                    (1 << 0)
#define G305_BTN_RIGHT                   (1 << 1)
#define G305_BTN_MIDDLE                  (1 << 2)
#define G305_BTN_BACK                    (1 << 3) /* Side back button */
#define G305_BTN_FORWARD                 (1 << 4) /* Side forward button */
#define G305_BTN_DPI_SWITCH              (1 << 5) /* Top DPI toggle button */

/* HID++ 2.0 helpers (testable, pure framing/parsing; no USB I/O). */

/* G305 Internal State allocated in RAM */
struct g305_state {
    int      present;
    int      bus_type;         /* 1 = UHCI, 2 = EHCI, 3 = XHCI */
    int      dev_address;
    uint16_t vendor_id;
    uint16_t product_id;
    int      current_dpi;      /* e.g. 400, 800, 1600, 3200 */
    int      dpi_preset_idx;   /* 0..3 */
    int      battery_percent;  /* 0 - 100% */
    int      battery_charging; /* 1 if charging, 0 if battery */
    int      performance_mode; /* 1 = 1000Hz, 0 = 125Hz */
    int      last_buttons;
    int      total_reports;
    uint64_t last_report_ms;
};

static inline int g305_device_matches(uint16_t vendor_id, uint16_t product_id) {
    return vendor_id == LOGITECH_VID &&
           product_id == LOGITECH_PID_G305_RECEIVER;
}

/* G305 Driver Interface */
int  g305_probe(void);
int  g305_init(void *ram_instance);
void g305_cleanup(void);
int  g305_is_available(void);
int  g305_poll(int *dx, int *dy, int *buttons, int *wheel);
int  g305_set_dpi(int dpi);
int  g305_get_dpi(void);
int  g305_cycle_dpi(void);
int  g305_get_battery_level(void);
int  g305_refresh_battery(void);
int  g305_refresh_device_info(void);
int  g305_set_performance_mode(int mode);
int  g305_get_performance_mode(void);
void g305_print_info(void);
struct g305_state *g305_get_state(void);

/* HID++ Protocol Helpers (testable, no USB I/O) */
int hidpp_short_build(uint8_t *buf, size_t len, uint8_t dev_index,
                      uint16_t feature_idx, uint8_t param1, uint8_t param2, uint8_t param3);
int hidpp_long_build(uint8_t *buf, size_t len, uint8_t dev_index,
                     uint16_t feature_idx, uint8_t reg_addr, const uint8_t *params, int param_count);
int hidpp_parse_battery(const uint8_t *resp, int len, int *percent, int *charging);
int hidpp_parse_dpi(const uint8_t *resp, int len, int *dpi);
int hidpp_parse_polling_rate(const uint8_t *resp, int len, int *mode);

#endif /* MOUSE_G305_H */
