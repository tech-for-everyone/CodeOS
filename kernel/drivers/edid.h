#ifndef EDID_H
#define EDID_H

#include "types.h"

/* EDID base block layout (128 bytes) */
#define EDID_HEADER_SIZE    8
#define EDID_MANUFACTURER   8
#define EDID_SERIAL         12
#define EDID_WEEK           16
#define EDID_YEAR           17
#define EDID_VERSION        18
#define EDID_REVISION       19
#define EDID_BASIC_DISPLAY  20
#define EDID_TIMING_1       21   /* 18 bytes of established/timing descriptors */
#define EDID_DETAILEDTiming 54   /* 4 x 18-byte descriptors */
#define EDID_EXTENSION      126
#define EDID_CHECKSUM       127
#define EDID_BLOCK_SIZE     128

/* Established timings (bytes 23-24) */
#define EDID_EST_720x400_70   (1 << 0)
#define EDID_EST_720x400_88   (1 << 1)
#define EDID_EST_640x480_60   (1 << 2)
#define EDID_EST_640x480_67   (1 << 3)
#define EDID_EST_640x480_72   (1 << 4)
#define EDID_EST_640x480_75   (1 << 5)
#define EDID_EST_800x600_56   (1 << 6)
#define EDID_EST_800x600_60   (1 << 7)
#define EDID_EST_800x600_72   (1 << 8)
#define EDID_EST_800x600_75   (1 << 9)
#define EDID_EST_832x624_75   (1 << 10)
#define EDID_EST_1024x768_87i (1 << 11)
#define EDID_EST_1024x768_60  (1 << 12)
#define EDID_EST_1024x768_70  (1 << 13)
#define EDID_EST_1024x768_75  (1 << 14)
#define EDID_EST_1280x1024_75 (1 << 15)

/* Descriptor tag (in 18-byte detailed timing blocks) */
#define EDID_DESC_DT    0xFC   /* Display descriptor — monitor name */
#define EDID_DESC_RANGES 0xFD  /* Display descriptor — timing ranges */
#define EDID_DESC_NAME  0xFC
#define EDID_DESC_END   0x10   /* End of EDID extensions / empty */

/* Parsed EDID info */
typedef struct {
    uint16_t vendor_id;
    uint32_t serial;
    uint8_t  edid_version;
    uint8_t  edid_revision;
    uint16_t pref_hsize_cm;   /* preferred horizontal size in cm */
    uint16_t pref_vsize_cm;   /* preferred vertical size in cm */
    /* Preferred (native) resolution from DTD[0] or fallback */
    uint32_t pref_width;
    uint32_t pref_height;
    uint32_t pref_refresh_hz;
    int      valid;
} edid_info_t;

/*
 * Parse a 128-byte EDID block and fill edid_info_t.
 * Returns 1 if a valid preferred resolution was found, 0 otherwise.
 */
int edid_parse(const uint8_t *block, edid_info_t *info);

/*
 * Decode established and standard timing fallback resolutions.
 * Used when no DTD provides a clear preferred mode.
 */
void edid_fallback_resolution(const uint8_t *block, uint32_t *w, uint32_t *h);

/*
 * Validate EDID checksum. Returns 1 if checksum is correct.
 */
int edid_validate(const uint8_t *block);

#endif
