#ifndef HID_PARSER_H
#define HID_PARSER_H

#include "types.h"

/* Maximum number of fields (usages) in a single report */
#define HID_MAX_FIELDS 32
/* Maximum report size in bytes */
#define HID_MAX_REPORT_SIZE 64

/* HID report field types */
#define HID_ITEM_TYPE_MAIN   0
#define HID_ITEM_TYPE_GLOBAL 1
#define HID_ITEM_TYPE_LOCAL  2

/* HID Main item tags */
#define HID_TAG_INPUT        0x80
#define HID_TAG_OUTPUT       0x90
#define HID_TAG_FEATURE      0xB0
#define HID_TAG_COLLECTION   0xA0
#define HID_TAG_END_COL      0xC0

/* HID Global item tags */
#define HID_TAG_USAGE_PAGE   0x04
#define HID_TAG_LOGICAL_MIN  0x14
#define HID_TAG_LOGICAL_MAX  0x24
#define HID_TAG_PHYS_MIN    0x34
#define HID_TAG_PHYS_MAX    0x44
#define HID_TAG_UNIT_EXP    0x54
#define HID_TAG_UNIT         0x64
#define HID_TAG_REPORT_SIZE  0x74
#define HID_TAG_REPORT_ID   0x84
#define HID_TAG_REPORT_COUNT 0x94

/* HID Local item tags */
#define HID_TAG_USAGE        0x08
#define HID_TAG_USAGE_MIN    0x18
#define HID_TAG_USAGE_MAX    0x28

/* Common HID Usage Pages */
#define HID_UP_KEYBOARD      0x07
#define HID_UP_CONSUMER      0x0C  /* Media keys */
#define HID_UP_LED           0x08
#define HID_UP_BUTTON        0x09
#define HID_UP_GENERIC_DESKTOP 0x01

/* Keyboard Usages */
#define HID_USAGE_KEYBOARD_A 0x04
#define HID_USAGE_KEYBOARD_Z 0x1D
#define HID_USAGE_KEYBOARD_1 0x1E
#define HID_USAGE_KEYBOARD_0 0x27
#define HID_USAGE_KEYBOARD_RETURN 0x28
#define HID_USAGE_KEYBOARD_ESCAPE 0x29
#define HID_USAGE_KEYBOARD_DELETE 0x2C
#define HID_USAGE_KEYBOARD_SPACE 0x2C
#define HID_USAGE_KEYBOARD_TAB 0x2B
#define HID_USAGE_KEYBOARD_F1 0x3A
#define HID_USAGE_KEYBOARD_F12 0x45
#define HID_USAGE_KEYPAD_NUMLOCK 0x53
#define HID_USAGE_KEYPAD_DIVIDE 0x54
#define HID_USAGE_KEYPAD_MULTIPLY 0x55
#define HID_USAGE_KEYPAD_MINUS 0x56
#define HID_USAGE_KEYPAD_PLUS 0x57
#define HID_USAGE_KEYPAD_ENTER 0x58
#define HID_USAGE_KEYPAD_1 0x59
#define HID_USAGE_KEYPAD_0 0x62
#define HID_USAGE_KEYPAD_DOT 0x63

/* Consumer Usage Page — Media Keys */
#define HID_USAGE_CONSUMER_MENU 0x0183  /* Main Menu */
#define HID_USAGE_CONSUMER_POWER 0x0030
#define HID_USAGE_CONSUMER_RESET 0x0031
#define HID_USAGE_CONSUMER_SLEEP 0x0032
#define HID_USAGE_CONSUMER_RECORD 0x00B7
#define HID_USAGE_CONSUMER_FAST_FORWARD 0x00B3
#define HID_USAGE_CONSUMER_REWIND 0x00B4
#define HID_USAGE_CONSUMER_SCAN_NEXT 0x00B5
#define HID_USAGE_CONSUMER_SCAN_PREV 0x00B6
#define HID_USAGE_CONSUMER_STOP 0x00B7
#define HID_USAGE_CONSUMER_EJECT 0x00B8
#define HID_USAGE_CONSUMER_PLAY_PAUSE 0x00CD
#define HID_USAGE_CONSUMER_MUTE 0x00E2
#define HID_USAGE_CONSUMER_VOL_UP 0x00E9
#define HID_USAGE_CONSUMER_VOL_DOWN 0x00EA
#define HID_USAGE_CONSUMER_BASS_BOOST 0x00E5
#define HID_USAGE_CONSUMER_BASS_EQUALIZER 0x00E7

/* LED Usage Page */
#define HID_USAGE_LED_NUM_LOCK    0x01
#define HID_USAGE_LED_CAPS_LOCK   0x02
#define HID_USAGE_LED_SCROLL_LOCK 0x03

/* Collection types */
#define HID_COLLECTION_APPLICATION 0x01

/* Parsed field from a HID report descriptor */
struct hid_field {
    uint32_t usage_page;
    uint32_t usage;
    int32_t  logical_min;
    int32_t  logical_max;
    int32_t  physical_min;
    int32_t  physical_max;
    uint32_t report_size;
    uint32_t report_count;
    uint32_t report_id;
    uint32_t report_offset;  /* bit offset in report */
    uint32_t flags;          /* input/output/feature flags */
};

/* Parsed HID report descriptor */
struct hid_report {
    struct hid_field fields[HID_MAX_FIELDS];
    int field_count;
    int report_id;
    int report_size_bits;  /* total report size in bits */
    uint8_t usage_page;    /* current global usage page */
    uint8_t usage;         /* current local usage */
};

/* Parser state */
struct hid_parser_state {
    struct hid_report *report;
    /* Global items */
    uint32_t usage_page;
    int32_t  logical_min;
    int32_t  logical_max;
    int32_t  physical_min;
    int32_t  physical_max;
    uint32_t report_size;
    uint32_t report_count;
    uint32_t report_id;
    uint32_t bit_offset;
    /* Local items */
    uint32_t usage;
    uint32_t usage_min;
    uint32_t usage_max;
    int      has_usage_min_max;
};

/* Parse a HID report descriptor and populate a hid_report.
 * Returns 0 on success, -1 on failure. */
int hid_parse_report_descriptor(const uint8_t *desc, int desc_len,
                                struct hid_report *report);

/* Extract a field value from a HID report buffer.
 * Returns the value of the field, or 0 on error. */
int32_t hid_get_field_value(const struct hid_field *field,
                            const uint8_t *report_buf, int buf_len);

/* Find a field by usage page and usage in a report.
 * Returns pointer to field, or NULL if not found. */
const struct hid_field *hid_find_field(const struct hid_report *report,
                                       uint32_t usage_page, uint32_t usage);

/* Check if a HID report has an NKRO (N-Key Rollover) keyboard layout.
 * NKRO keyboards have multiple keyboard usage arrays in their reports. */
int hid_is_nkro(const struct hid_report *report);

/* Count how many keyboard keys can be simultaneously reported. */
int hid_get_max_keys(const struct hid_report *report);

#endif
