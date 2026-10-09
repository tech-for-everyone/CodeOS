#include "hid_parser.h"
#include "../kernel/string.h"
#include "../kernel/kprintf.h"

/* ─── Item header parsing ─── */

/* HID item format: bSize(2 bits) | bType(2 bits) | bTag(4 bits) */
static int hid_item_size(uint8_t header) {
    int sz = header & 0x03;
    return (sz == 3) ? 4 : sz;
}

static int hid_item_type(uint8_t header) {
    return (header >> 2) & 0x03;
}

static uint8_t hid_item_tag(uint8_t header) {
    return (header >> 4) & 0x0F;
}

/* Read a short item value */
static uint32_t read_item(const uint8_t *data, int size) {
    switch (size) {
        case 0: return 0;
        case 1: return data[0];
        case 2: return (uint32_t)data[0] | ((uint32_t)data[1] << 8);
        case 4: return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
                      | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        default: return 0;
    }
}

/* Sign-extend a value to int32_t */
static int32_t sign_extend(uint32_t val, int bits) {
    if (bits >= 32) return (int32_t)val;
    int32_t mask = 1 << (bits - 1);
    return (val ^ mask) - mask;
}

/* ─── Main parser ─── */

int hid_parse_report_descriptor(const uint8_t *desc, int desc_len,
                                struct hid_report *report) {
    if (!desc || !report) return -1;
    memset(report, 0, sizeof(*report));

    struct hid_parser_state state;
    memset(&state, 0, sizeof(state));
    state.report = report;
    state.has_usage_min_max = 0;

    int i = 0;
    while (i < desc_len) {
        uint8_t header = desc[i];

        /* Long item (0xFE) — skip */
        if (header == 0xFE) {
            if (i + 1 >= desc_len) break;
            int data_size = desc[i + 1];
            i += 3 + data_size;
            continue;
        }

        /* Short item */
        int size = hid_item_size(header);
        int type = hid_item_type(header);
        uint8_t tag = hid_item_tag(header);
        i++;

        if (i + size > desc_len) break;

        uint32_t val = read_item(desc + i, size);
        i += size;

        switch (type) {
        case HID_ITEM_TYPE_GLOBAL:
            switch (tag) {
            case 0x04: /* Usage Page */
                state.usage_page = val;
                state.report->usage_page = val;
                break;
            case 0x14: /* Logical Minimum */
                state.logical_min = sign_extend(val, size * 8);
                break;
            case 0x24: /* Logical Maximum */
                state.logical_max = sign_extend(val, size * 8);
                break;
            case 0x34: /* Physical Minimum */
                state.physical_min = sign_extend(val, size * 8);
                break;
            case 0x44: /* Physical Maximum */
                state.physical_max = sign_extend(val, size * 8);
                break;
            case 0x74: /* Report Size */
                state.report_size = val;
                break;
                case 0x84: /* Report ID */
                state.report_id = val;
                break;
            case 0x94: /* Report Count */
                state.report_count = val;
                break;
            default:
                break;
            }
            break;

        case HID_ITEM_TYPE_LOCAL:
            switch (tag) {
            case 0x08: /* Usage */
                state.usage = val;
                state.report->usage = val;
                state.has_usage_min_max = 0;
                break;
            case 0x18: /* Usage Minimum */
                state.usage_min = val;
                state.has_usage_min_max = 1;
                break;
            case 0x28: /* Usage Maximum */
                state.usage_max = val;
                state.has_usage_min_max = 1;
                break;
            default:
                break;
            }
            break;

        case HID_ITEM_TYPE_MAIN:
            switch (tag) {
            case HID_TAG_INPUT: /* 0x80 */
            case HID_TAG_OUTPUT: /* 0x90 */
            case HID_TAG_FEATURE: /* 0xB0 */
            {
                /* Create fields for this report item */
                uint32_t usage = state.usage;
                for (uint32_t f = 0; f < state.report_count; f++) {
                    if (report->field_count >= HID_MAX_FIELDS) break;

                    struct hid_field *field = &report->fields[report->field_count];
                    field->usage_page = state.usage_page;
                    field->flags = val; /* input/output/feature flags */
                    field->report_id = state.report_id;
                    field->report_offset = state.bit_offset;
                    field->report_size = state.report_size;
                    field->report_count = state.report_count;
                    field->logical_min = state.logical_min;
                    field->logical_max = state.logical_max;
                    field->physical_min = state.physical_min;
                    field->physical_max = state.physical_max;

                    /* Determine usage */
                    if (state.has_usage_min_max) {
                        field->usage = state.usage_min + f;
                    } else {
                        field->usage = usage + f;
                    }

                    state.bit_offset += state.report_size;
                    report->field_count++;
                }

                report->report_id = state.report_id;
                report->report_size_bits = state.bit_offset;

                /* Reset local state after INPUT/OUTPUT/FEATURE */
                state.has_usage_min_max = 0;
                state.usage_min = 0;
                state.usage_max = 0;
                state.usage = 0;
                break;
            }

            case HID_TAG_COLLECTION: /* 0xA0 */
                /* Reset local items */
                state.has_usage_min_max = 0;
                state.usage_min = 0;
                state.usage_max = 0;
                break;

            case HID_TAG_END_COL: /* 0xC0 */
                break;

            default:
                break;
            }
            break;
        }
    }

    kprintf("hid_parser: parsed %d fields, %d bits report\n",
            report->field_count, report->report_size_bits);
    return 0;
}

/* ─── Field extraction ─── */

int32_t hid_get_field_value(const struct hid_field *field,
                            const uint8_t *report_buf, int buf_len) {
    if (!field || !report_buf) return 0;

    int bit_offset = field->report_offset;
    int bit_size = field->report_size;
    int byte_offset = bit_offset / 8;
    int bit_shift = bit_offset % 8;

    if (byte_offset >= buf_len) return 0;

    /* Read up to 4 bytes */
    uint32_t raw = 0;
    for (int b = 0; b < 4 && (byte_offset + b) < buf_len; b++) {
        raw |= (uint32_t)report_buf[byte_offset + b] << (b * 8);
    }
    raw >>= bit_shift;

    /* Mask to report_size bits */
    if (bit_size < 32)
        raw &= (1U << bit_size) - 1;

    /* Sign extend */
    return sign_extend(raw, bit_size);
}

const struct hid_field *hid_find_field(const struct hid_report *report,
                                       uint32_t usage_page, uint32_t usage) {
    if (!report) return NULL;
    for (int i = 0; i < report->field_count; i++) {
        if (report->fields[i].usage_page == usage_page &&
            report->fields[i].usage == usage)
            return &report->fields[i];
    }
    return NULL;
}

int hid_is_nkro(const struct hid_report *report) {
    if (!report) return 0;

    /* Count keyboard usage fields — NKRO has more than 6 */
    int kbd_count = 0;
    for (int i = 0; i < report->field_count; i++) {
        const struct hid_field *f = &report->fields[i];
        if (f->usage_page == HID_UP_KEYBOARD && f->report_size == 8) {
            kbd_count += f->report_count;
        }
    }

    return kbd_count > 6;
}

int hid_get_max_keys(const struct hid_report *report) {
    if (!report) return 0;
    int keys = 0;
    for (int i = 0; i < report->field_count; i++) {
        const struct hid_field *f = &report->fields[i];
        if (f->usage_page == HID_UP_KEYBOARD && f->report_size == 8) {
            keys += f->report_count;
        }
    }
    return keys;
}
