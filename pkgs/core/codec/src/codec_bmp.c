/* BMP decoder.
 *
 * Uncompressed 8-bit paletted, 24-bit BGR and 32-bit BGRA, in both bottom-up
 * (the normal case) and top-down layouts. RLE4/RLE8 are recognised and
 * refused by name rather than rendered as garbage.
 *
 * The trap worth naming: a BMP pixel is BGR(A) in file order, so a naive
 * read produces a red/blue swapped image. That is a silent failure, so the
 * channel order is asserted explicitly against a known fixture in the host
 * test rather than eyeballed.
 */

#include "codec.h"
#include "codec_alloc.h"
#include "codec_str.h"

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

codec_err_t codec_bmp_decode(const uint8_t *d, size_t n, codec_image_t *out) {
    if (!d || !out) return CODEC_ERR_ARG;
    memset(out, 0, sizeof(*out));

    if (n < 14) return CODEC_ERR_TRUNCATED;
    if (d[0] != 'B' || d[1] != 'M') return CODEC_ERR_FORMAT;

    uint32_t data_off = le32(d + 10);

    /* BITMAPINFOHEADER is 40 bytes; the older 12-byte BITMAPCOREHEADER has a
     * 16-bit width/height and no compression field. Refusing it by name beats
     * misreading the fields. */
    if (n < 14 + 40) return CODEC_ERR_TRUNCATED;
    uint32_t hdr_size = le32(d + 14);
    if (hdr_size == 12) {
        codec_log("codec: BITMAPCOREHEADER (12 byte) bmp is not implemented");
        return CODEC_ERR_UNSUPPORTED;
    }
    if (hdr_size < 40) return CODEC_ERR_FORMAT;

    int32_t width  = (int32_t)le32(d + 18);
    int32_t height = (int32_t)le32(d + 22);
    unsigned planes = le16(d + 26);
    unsigned bpp    = le16(d + 28);
    uint32_t compr  = le32(d + 30);

    if (planes != 1) {
        codec_log("codec: bmp colour planes != 1");
        return CODEC_ERR_FORMAT;
    }
    if (compr != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "codec: bmp compression %u is not implemented (only "
                 "BI_RGB is)", (unsigned)compr);
        codec_log(buf);
        return CODEC_ERR_UNSUPPORTED;
    }

    int top_down = 0;
    if (height < 0) {
        top_down = 1;
        /* Negating INT32_MIN would overflow. */
        if (height == (int32_t)0x80000000) return CODEC_ERR_FORMAT;
        height = -height;
    }

    if (width <= 0 || height == 0) return CODEC_ERR_FORMAT;
    if ((uint32_t)width > CODEC_MAX_DIM || (uint32_t)height > CODEC_MAX_DIM)
        return CODEC_ERR_TOO_BIG;
    if ((uint64_t)width * (uint64_t)height > CODEC_MAX_PIXELS)
        return CODEC_ERR_TOO_BIG;
    if (bpp != 8 && bpp != 24 && bpp != 32) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "codec: %u-bit bmp is not implemented (8, 24 and 32 are)",
                 (unsigned)bpp);
        codec_log(buf);
        return CODEC_ERR_UNSUPPORTED;
    }

    if (data_off >= n) return CODEC_ERR_TRUNCATED;

    /* Each scanline is padded to a 4-byte boundary. Getting this wrong shifts
     * every row after the first, which shows up as a diagonal skew rather than
     * an obviously broken image. */
    uint32_t src_row = ((uint32_t)width * bpp + 31u) / 32u * 4u;
    size_t need = (size_t)src_row * (size_t)height;
    if ((size_t)data_off + need > n) return CODEC_ERR_TRUNCATED;

    uint8_t palette[256 * 4];
    unsigned pal_entries = 0;
    if (bpp == 8) {
        uint32_t pal_off = 14 + hdr_size;
        pal_entries = le32(d + 46);
        if (pal_entries == 0 || pal_entries > 256) return CODEC_ERR_FORMAT;
        if ((size_t)pal_off + (size_t)pal_entries * 4 > n) return CODEC_ERR_TRUNCATED;
        for (unsigned i = 0; i < pal_entries; i++) {
            palette[i * 4 + 0] = d[pal_off + i * 4 + 2];   /* B */
            palette[i * 4 + 1] = d[pal_off + i * 4 + 1];   /* G */
            palette[i * 4 + 2] = d[pal_off + i * 4 + 0];   /* R */
            palette[i * 4 + 3] = 255;
        }
    }

    /* 32-bit BI_RGB leaves the fourth byte formally undefined, and in practice two
 * different populations of file exist: writers that put a real alpha value
 * there, and writers that leave it zero because they never intended an alpha
 * channel at all. Those two cases are byte-for-byte indistinguishable at any
 * single pixel, so honouring alpha per-pixel forces a choice between rendering
 * such a file as fully transparent (which reads as "the decoder drew nothing")
 * and ignoring alpha (which breaks genuine transparency).
 *
 * The disambiguation used here is at image scope, not pixel scope: if every
 * alpha byte in the image is zero the channel is absent and the image is
 * opaque; if any pixel has non-zero alpha then the channel is meaningful and
 * every pixel's alpha is honoured, including zeros among them.
 *
 * This is a heuristic, not something the format guarantees. It is stated here
 * and covered by both cases in the host test so the behaviour is at least
 * known rather than accidental. BITMAPV4/V5, which define the channel
 * explicitly, are not implemented. */
int alpha_meaningful = 0;
    if (bpp == 32) {
        /* Need total pixel count, not total byte count: `need` counts src_row
         * bytes including per-row padding, and at 32-bit there is no padding, so
         * the two differ only if that ever changes. Iterate pixels anyway so
         * the intent survives it. */
        size_t pixels = (size_t)width * (size_t)height;
        for (size_t i = 0; i < pixels; i++) {
            if (d[data_off + i * 4 + 3] != 0) { alpha_meaningful = 1; break; }
        }
    }

    uint32_t *px = (uint32_t *)codec_alloc_zero((size_t)width * (size_t)height * 4);
    if (!px) return CODEC_ERR_NOMEM;

    for (int32_t y = 0; y < height; y++) {
        /* bottom-up stores row 0 last, so map file row -> image row */
        uint32_t fy = top_down ? (uint32_t)y : (uint32_t)(height - 1 - y);
        const uint8_t *row = d + data_off + (size_t)fy * src_row;
        uint32_t *dst = px + (size_t)y * width;

        for (int32_t x = 0; x < width; x++) {
            uint32_t r, g, b, a = 255;

            if (bpp == 32) {
                const uint8_t *p = row + (size_t)x * 4;
                b = p[0]; g = p[1]; r = p[2];
                a = alpha_meaningful ? p[3] : 255;
            } else if (bpp == 24) {
                const uint8_t *p = row + (size_t)x * 3;
                b = p[0]; g = p[1]; r = p[2];
            } else {
                uint32_t idx = row[x];
                if (idx >= pal_entries) { codec_free(px); return CODEC_ERR_CORRUPT; }
                b = palette[idx * 4 + 0];
                g = palette[idx * 4 + 1];
                r = palette[idx * 4 + 2];
            }

            /* NEGATIVE CONTROL (build with -DCODEC_NEG_SWAP_BGR): deliberately
             * wrong channel order. The host test asserts this build FAILS the
             * BGR fixtures, which is what proves those checks can detect a
             * red/blue swap at all. Otherwise "bmp 24-bit: all pixels exact"
             * would be a check that cannot fail. */
#ifdef CODEC_NEG_SWAP_BGR
            { uint32_t t = r; r = b; b = t; }
#endif

            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;   /* 0xAARRGGBB */
        }
    }

    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    out->argb = px;
    return CODEC_OK;
}