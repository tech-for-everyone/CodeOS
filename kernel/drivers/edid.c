#include "edid.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

/*
 * EDID 1.4 Detailed Timing Descriptor (DTD) layout:
 *   Byte 0-1:  pixel clock (little-endian, in 10 kHz units)
 *   Byte 2:    horizontal addrable pixels low 8 bits
 *   Byte 3:    horizontal blanking pixels (low 8)
 *   Byte 4:    [7:4] h addrable high, [7:4] h blanking high
 *   Byte 5:    vertical addrable lines low 8
 *   Byte 6:    vertical blanking lines low 8
 *   Byte 7:    [7:4] v addrable high, [7:4] v blanking high
 *   ...
 *
 * For a preferred mode, pixel clock is non-zero.
 */

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int edid_validate(const uint8_t *block) {
    if (!block) return 0;

    /* Check header: 00 FF FF FF FF FF FF 00 */
    static const uint8_t edid_header[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
    if (memcmp(block, edid_header, 8) != 0) return 0;

    /* Checksum: sum of all 128 bytes should be 0 */
    uint8_t sum = 0;
    for (int i = 0; i < 128; i++) sum += block[i];
    return sum == 0;
}

int edid_parse(const uint8_t *block, edid_info_t *info) {
    if (!block || !info) return 0;
    memset(info, 0, sizeof(*info));

    if (!edid_validate(block)) {
        kprintf("EDID: invalid block (header or checksum)\n");
        return 0;
    }

    /* Manufacturer ID (3 bytes packed as 5-bit each) */
    uint16_t mfg = read_le16(block + EDID_MANUFACTURER);
    info->vendor_id = mfg;

    /* Serial number */
    info->serial = read_le32(block + EDID_SERIAL);

    info->edid_version  = block[EDID_VERSION];
    info->edid_revision = block[EDID_REVISION];

    /* Physical size (in cm) */
    info->pref_hsize_cm = block[21];
    info->pref_vsize_cm = block[22];

    /* Scan the 4 x 18-byte descriptor blocks (offset 54, 72, 90, 108) */
    for (int i = 0; i < 4; i++) {
        const uint8_t *d = block + EDID_DETAILEDTiming + i * 18;
        uint16_t pixel_clock = read_le16(d);

        /* If pixel_clock == 0, this is a non-timing descriptor */
        if (pixel_clock == 0) continue;

        /* DTD: extract horizontal and vertical addressable */
        uint32_t h_addr = (uint32_t)d[2] | ((uint32_t)(d[4] & 0xF0) << 4);
        uint32_t v_addr = (uint32_t)d[5] | ((uint32_t)(d[7] & 0xF0) << 4);

        if (h_addr > 0 && v_addr > 0) {
            info->pref_width  = h_addr;
            info->pref_height = v_addr;

            /* Calculate refresh: pixel_clock * 10000 / ((h_total)*(v_total)) */
            uint32_t h_blank = (uint32_t)d[3] | ((uint32_t)(d[4] & 0x0F) << 8);
            uint32_t v_blank = (uint32_t)d[6] | ((uint32_t)(d[7] & 0x0F) << 8);
            uint32_t h_total = h_addr + h_blank;
            uint32_t v_total = v_addr + v_blank;
            if (h_total > 0 && v_total > 0) {
                info->pref_refresh_hz = (uint32_t)((uint64_t)pixel_clock * 10000 /
                                                   ((uint64_t)h_total * v_total));
            }

            info->valid = 1;
            kprintf("EDID: preferred mode %ux%u @ %u Hz\n",
                    info->pref_width, info->pref_height, info->pref_refresh_hz);
            break;  /* Use first DTD as preferred */
        }
    }

    return info->valid;
}

/* Fallback: pick the highest resolution from established timings */
void edid_fallback_resolution(const uint8_t *block, uint32_t *w, uint32_t *h) {
    /* Established timing table: highest resolution last */
    static const struct { uint16_t mask; uint32_t w; uint32_t h; } est[] = {
        { EDID_EST_720x400_70,   720,  400 },
        { EDID_EST_720x400_88,   720,  400 },
        { EDID_EST_640x480_60,   640,  480 },
        { EDID_EST_640x480_67,   640,  480 },
        { EDID_EST_640x480_72,   640,  480 },
        { EDID_EST_640x480_75,   640,  480 },
        { EDID_EST_800x600_56,   800,  600 },
        { EDID_EST_800x600_60,   800,  600 },
        { EDID_EST_800x600_72,   800,  600 },
        { EDID_EST_800x600_75,   800,  600 },
        { EDID_EST_832x624_75,   832,  624 },
        { EDID_EST_1024x768_60,  1024, 768 },
        { EDID_EST_1024x768_70,  1024, 768 },
        { EDID_EST_1024x768_75,  1024, 768 },
        { EDID_EST_1280x1024_75, 1280, 1024 },
    };

    /* Established timings bitmap is bytes 23-24 */
    uint16_t est_timings = (uint16_t)block[23] | ((uint16_t)block[24] << 8);

    /* Walk from highest to lowest, return the first match */
    *w = 640; *h = 480;  /* ultimate fallback */
    for (int i = (int)(sizeof(est)/sizeof(est[0])) - 1; i >= 0; i--) {
        if (est_timings & est[i].mask) {
            *w = est[i].w;
            *h = est[i].h;
            return;
        }
    }
}
