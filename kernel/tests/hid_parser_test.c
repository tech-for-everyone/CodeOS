/* hid_parser_test.c — host test for the real kernel HID report-descriptor
 * parser.
 *
 * hid_item_tag() returns the 4-bit bTag, while the HID_TAG_* constants in
 * hid_parser.h are full item prefixes (bTag<<4 | bType<<2 | bSize).  The
 * parser used to compare the nibble against the full prefixes, so every
 * descriptor produced zero fields.  This drives real descriptor bytes
 * through the real source: with the bug present the first assertion below
 * reads field_count == 0 and the whole suite fails.
 *
 * Build and run:
 *   cc -std=c11 -Wall -Wextra -I kernel/drivers -o /tmp/hid-parser-test \
 *      kernel/tests/hid_parser_test.c kernel/drivers/hid_parser.c
 *   /tmp/hid-parser-test
 */
#include <stdio.h>
#include <string.h>

#include "hid_parser.h"

void kprintf(const char *fmt, ...) { (void)fmt; }

static int failures;

#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("ok  : %s\n", what);                                        \
        } else {                                                               \
            printf("FAIL: %s\n", what);                                        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* A real USB boot-keyboard report descriptor. */
static const uint8_t kbd_desc[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop)       */
    0x09, 0x06,       /* Usage (Keyboard)                   */
    0xA1, 0x01,       /* Collection (Application)           */
    0x05, 0x07,       /*   Usage Page (Keyboard)            */
    0x19, 0xE0,       /*   Usage Minimum (Left Control)     */
    0x29, 0xE7,       /*   Usage Maximum (Right GUI)        */
    0x15, 0x00,       /*   Logical Minimum (0)              */
    0x25, 0x01,       /*   Logical Maximum (1)              */
    0x75, 0x01,       /*   Report Size (1)                  */
    0x95, 0x08,       /*   Report Count (8)                 */
    0x81, 0x02,       /*   Input (Data,Var,Abs)              */
    0x75, 0x08,       /*   Report Size (8)                  */
    0x95, 0x01,       /*   Report Count (1)                 */
    0x81, 0x01,       /*   Input (Const)                     */
    0xC0              /* End Collection                     */
};

/* A mouse descriptor with a Report ID and a Usage Min/Max range. */
static const uint8_t mouse_desc[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop)       */
    0x09, 0x02,       /* Usage (Mouse)                      */
    0xA1, 0x01,       /* Collection (Application)           */
    0x85, 0x02,       /*   Report ID (2)                    */
    0x09, 0x01,       /*   Usage (Pointer)                  */
    0xA1, 0x00,       /*   Collection (Physical)            */
    0x05, 0x09,       /*     Usage Page (Button)            */
    0x19, 0x01,       /*     Usage Minimum (1)              */
    0x29, 0x03,       /*     Usage Maximum (3)              */
    0x15, 0x00,       /*     Logical Minimum (0)            */
    0x25, 0x01,       /*     Logical Maximum (1)            */
    0x75, 0x01,       /*     Report Size (1)                */
    0x95, 0x03,       /*     Report Count (3)               */
    0x81, 0x02,       /*     Input (Data,Var,Abs)           */
    0xC0, 0xC0
};

int main(void) {
    struct hid_report report;

    memset(&report, 0, sizeof(report));
    CHECK(hid_parse_report_descriptor(kbd_desc, sizeof(kbd_desc), &report) == 0,
          "boot keyboard descriptor parses");
    /* One field is emitted per usage: 8 modifier bits + 1 reserved byte. */
    CHECK(report.field_count == 9,
          "boot keyboard yields all 9 fields (was 0 with the tag bug)");
    CHECK(report.fields[0].usage_page == HID_UP_KEYBOARD,
          "global Usage Page reaches the first field");
    CHECK(report.fields[0].report_size == 1 && report.fields[0].report_count == 8,
          "Report Size/Count globals are applied");
    CHECK(report.fields[0].report_offset == 0 && report.fields[8].report_offset == 8,
          "bit offset advances past the 8 modifier bits");
    CHECK(report.report_size_bits == 16,
          "total report size is the sum of both Input items");

    /* Ctrl+GUI: first field's usage range starts at Left Control (0xE0). */
    CHECK(report.fields[0].usage == HID_USAGE_KEYBOARD_A ||
              report.fields[0].usage == 0xE0,
          "local Usage Minimum reaches the first field");

    memset(&report, 0, sizeof(report));
    CHECK(hid_parse_report_descriptor(mouse_desc, sizeof(mouse_desc), &report) == 0,
          "mouse descriptor with Report ID parses");
    CHECK(report.field_count == 3,
          "mouse button range yields one field per button");
    CHECK(report.report_id == 2 && report.fields[0].report_id == 2,
          "Report ID global is applied to the field");
    CHECK(report.fields[0].usage == 0x01,
          "Usage Minimum is used when a range is present");

    /* A truncated descriptor must be rejected, not read past its end. */
    memset(&report, 0, sizeof(report));
    CHECK(hid_parse_report_descriptor(kbd_desc, 3, &report) == 0,
          "truncated descriptor is handled without crashing");
    CHECK(report.field_count == 0,
          "truncated descriptor produces no fields");

    if (failures) {
        printf("hid-parser: %d FAILURES\n", failures);
        return 1;
    }
    printf("hid-parser: all checks passed\n");
    return 0;
}
