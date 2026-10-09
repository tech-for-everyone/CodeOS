/* PNG decoder (ISO/IEC 15948).
 *
 * Supports colour types 0 (grey), 2 (truecolour), 3 (palette), 4 (grey+alpha)
 * and 6 (truecolour+alpha) at bit depths 1/2/4/8/16 as the format allows, plus
 * PLTE and tRNS. Adam7 interlacing is rejected explicitly rather than
 * half-handled.
 *
 * Output is 0xAARRGGBB -- see the pixel-order note at the top of codec.h. This
 * is the easiest thing to get wrong in the whole package, because emitting
 * 0xRRGGBBAA produces colours that look like a plausible image rather than an
 * obvious fault.
 *
 * CRCs are verified. That is not decoration: it is what lets a truncated file
 * be reported as truncated rather than as "malformed", and it is the only
 * check that catches a corrupted IDAT before DEFLATE turns it into nonsense
 * decoded pixels. A CRCs-off decoder will happily emit a grey smear from a
 * file whose compressed data was damaged.
 */

#include "codec.h"
#include "codec_alloc.h"
#include "codec_str.h"

#define PNG_SIG_LEN 8

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}

/* CRC-32 (IEEE), table built on first use. The kernel has no room for a
 * 1 KiB const table it does not need most of the time, and computing it once
 * costs nothing measurable. */
static uint32_t s_crc_tab[256];
static int s_crc_ready;

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        s_crc_tab[n] = c;
    }
    s_crc_ready = 1;
}

static uint32_t crc32_of(const uint8_t *buf, size_t len) {
    if (!s_crc_ready) crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = s_crc_tab[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static int channels_for(int color_type) {
    switch (color_type) {
    case 0: return 1;   /* grey            */
    case 2: return 3;   /* truecolour      */
    case 3: return 1;   /* palette index   */
    case 4: return 2;   /* grey + alpha    */
    case 6: return 4;   /* truecolour + a  */
    default: return 0;
    }
}

/* Which bit depths a colour type is allowed to use. */
static int depth_ok(int color_type, int depth) {
    switch (color_type) {
    case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 2: case 4: case 6: return depth == 8 || depth == 16;
    default: return 0;
    }
}

static int paeth(int a, int b, int c) {
    int p  = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    /* NEGATIVE CONTROL (build with -DCODEC_NEG_WRONG_PAETH): always return the
     * up-left neighbour. A plausible-looking simplification that is wrong for
     * most pixels, so it is exactly the kind of bug that survives review. The
     * host test asserts that build fails the Paeth scanlines. */
#ifdef CODEC_NEG_WRONG_PAETH
    (void)a; (void)b; (void)p; (void)pa; (void)pb; (void)pc;
    return c;
#else
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
#endif
}

/* Reverse the per-scanline filters in place, turning filtered data into
 * raw samples. `bpp` is bytes per complete pixel, rounded up, which is what
 * the filter spec means by "bytes per pixel". */
static void unfilter(uint8_t *raw, uint32_t height, uint32_t rowbytes, int filter_bpp) {
    uint8_t *prev = NULL;

    for (uint32_t y = 0; y < height; y++) {
        uint8_t *line = raw + (size_t)y * (rowbytes + 1);
        uint8_t ft = line[0];
        uint8_t *cur = line + 1;

        switch (ft) {
        case 0:
            break;
        case 1:
            for (size_t i = (size_t)filter_bpp; i < rowbytes; i++)
                cur[i] = (uint8_t)(cur[i] + cur[i - filter_bpp]);
            break;
        case 2:
            if (prev)
                for (size_t i = 0; i < rowbytes; i++)
                    cur[i] = (uint8_t)(cur[i] + prev[i]);
            break;
        case 3:
            if (!prev) {
                for (size_t i = (size_t)filter_bpp; i < rowbytes; i++)
                    cur[i] = (uint8_t)(cur[i] + (cur[i - filter_bpp] >> 1));
            } else {
                for (size_t i = 0; i < rowbytes; i++) {
                    int left = (i >= (size_t)filter_bpp) ? cur[i - filter_bpp] : 0;
                    cur[i] = (uint8_t)(cur[i] + ((left + prev[i]) >> 1));
                }
            }
            break;
        case 4:
            if (!prev) {
                for (size_t i = (size_t)filter_bpp; i < rowbytes; i++)
                    cur[i] = (uint8_t)(cur[i] + cur[i - filter_bpp]);
            } else {
                for (size_t i = 0; i < rowbytes; i++) {
                    int left = (i >= (size_t)filter_bpp) ? cur[i - filter_bpp] : 0;
                    int upleft = (i >= (size_t)filter_bpp) ? prev[i - filter_bpp] : 0;
                    cur[i] = (uint8_t)(cur[i] + paeth(left, prev[i], upleft));
                }
            }
            break;
        default:
            /* An unknown filter type is a hard error; leaving the row as-is
             * would produce plausible-looking garbage. */
            return;
        }
        prev = cur;
    }
}

/* Pull sample `idx` of a sub-byte-depth scanline, MSB first. */
static unsigned unpack_bits(const uint8_t *line, uint32_t idx, int depth) {
    unsigned per_byte = (unsigned)(8 / depth);
    unsigned byte = idx / per_byte;
    unsigned shift = (per_byte - 1 - (idx % per_byte)) * (unsigned)depth;
    return (line[byte] >> shift) & ((1u << depth) - 1u);
}

codec_err_t codec_png_decode(const uint8_t *d, size_t n, codec_image_t *out) {
    static const uint8_t sig[PNG_SIG_LEN] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    if (!d || !out) return CODEC_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (n < PNG_SIG_LEN + 12) return CODEC_ERR_TRUNCATED;
    if (memcmp(d, sig, PNG_SIG_LEN) != 0) return CODEC_ERR_FORMAT;

    uint32_t width = 0, height = 0;
    int depth = 0, color_type = 0, interlace = 0;
    int have_ihdr = 0;

    uint8_t *idat = NULL;
    size_t idat_len = 0, idat_cap = 0;

    uint8_t palette[256 * 3];
    unsigned palette_entries = 0;   /* PLTE length / 3, not always 256 */
    uint8_t pal_alpha[256];
    int have_plte = 0;

    int t_rns_gray = -1;
    int t_rns_r = -1, t_rns_g = -1, t_rns_b = -1;
    uint8_t t_rns_pal[256];
    int have_t_rns = 0;
    memset(pal_alpha, 255, sizeof(pal_alpha));
    memset(t_rns_pal, 255, sizeof(t_rns_pal));

    size_t pos = PNG_SIG_LEN;

    while (pos + 8 <= n) {
        uint32_t clen  = be32(d + pos);
        const uint8_t *ctype = d + pos + 4;
        const uint8_t *cdata = d + pos + 8;

        if (clen > n || pos + 12 + clen > n) {
            codec_log("codec: png chunk runs past the end of the file");
            codec_free(idat);
            return CODEC_ERR_TRUNCATED;
        }

        /* Verify CRC over type+data before trusting the payload. */
        uint32_t want = be32(d + pos + 8 + clen);
        uint32_t got  = crc32_of(d + pos + 4, clen + 4);
        if (want != got) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "codec: png CRC mismatch in chunk (want %08x got %08x)",
                     (unsigned)want, (unsigned)got);
            codec_log(buf);
            codec_free(idat);
            return CODEC_ERR_CORRUPT;
        }

        if (memcmp(ctype, "IHDR", 4) == 0) {
            if (clen != 13) { codec_free(idat); return CODEC_ERR_FORMAT; }
            width     = be32(cdata);
            height    = be32(cdata + 4);
            depth     = cdata[8];
            color_type = cdata[9];
            interlace = cdata[12];
            have_ihdr = 1;
        } else if (memcmp(ctype, "PLTE", 4) == 0) {
            if (clen == 0 || clen > 256 * 3 || (clen % 3) != 0) {
                codec_free(idat);
                return CODEC_ERR_FORMAT;
            }
            memcpy(palette, cdata, clen);
            /* The palette may hold fewer entries than 256, and an image is free
             * to reference a higher index than the palette provides. Recording
             * the real count is what lets the pixel loop reject that instead of
             * reading uninitialised stack past the copied bytes. */
            palette_entries = clen / 3;
            have_plte = 1;
        } else if (memcmp(ctype, "tRNS", 4) == 0) {
            have_t_rns = 1;
            if (color_type == 3 && clen <= 256)
                memcpy(t_rns_pal, cdata, clen);
            else if (color_type == 0 && clen >= 2)
                t_rns_gray = (int)be16(cdata);
            else if (color_type == 2 && clen >= 6) {
                /* Three 16-bit samples; a pixel matches when all three equal
                 * them, compared against the high byte of each sample. */
                t_rns_r = (int)be16(cdata);
                t_rns_g = (int)be16(cdata + 2);
                t_rns_b = (int)be16(cdata + 4);
            }
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            if (idat_len + clen > idat_cap) {
                size_t want = idat_len + clen + 8192;
                uint8_t *grown = (uint8_t *)codec_alloc(want);
                if (!grown) { codec_free(idat); return CODEC_ERR_NOMEM; }
                if (idat_len) memcpy(grown, idat, idat_len);
                codec_free(idat);
                idat = grown;
                idat_cap = want;
            }
            memcpy(idat + idat_len, cdata, clen);
            idat_len += clen;
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            break;
        }

        pos += 12 + clen;
    }

    if (!have_ihdr) { codec_free(idat); return CODEC_ERR_FORMAT; }

    if (width == 0 || height == 0) { codec_free(idat); return CODEC_ERR_FORMAT; }
    if (width > CODEC_MAX_DIM || height > CODEC_MAX_DIM) {
        codec_free(idat);
        return CODEC_ERR_TOO_BIG;
    }
    if ((uint64_t)width * height > CODEC_MAX_PIXELS) {
        codec_free(idat);
        return CODEC_ERR_TOO_BIG;
    }

    int channels = channels_for(color_type);
    if (channels == 0 || !depth_ok(color_type, depth)) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "codec: png colour type %d at %d bits is not a valid combination",
                 color_type, depth);
        codec_log(buf);
        codec_free(idat);
        return CODEC_ERR_FORMAT;
    }
    if (color_type == 3 && !have_plte) {
        codec_log("codec: palette png has no PLTE chunk");
        codec_free(idat);
        return CODEC_ERR_FORMAT;
    }
    if (interlace != 0) {
        codec_log("codec: Adam7 interlaced png is not implemented "
                  "(registry note says so too)");
        codec_free(idat);
        return CODEC_ERR_UNSUPPORTED;
    }
    if (!idat || idat_len == 0) { codec_free(idat); return CODEC_ERR_FORMAT; }

    /* IDAT payloads are a zlib stream (RFC 1950): 2-byte header, raw DEFLATE,
     * 4-byte Adler-32. codec_inflate does the DEFLATE half, so step over the
     * wrapper. A bad zlib header is worth catching by name -- it is the usual
     * result of passing a gzipped .gz that was renamed to .png. */
    if (idat_len < 6) { codec_free(idat); return CODEC_ERR_TRUNCATED; }
    unsigned cmf = idat[0], flg = idat[1];
    if ((cmf & 0x0F) != 8) {
        codec_log("codec: png IDAT is not a zlib stream (compression method "
                  "is not deflate) -- is this actually a .gz?");
        codec_free(idat);
        return CODEC_ERR_FORMAT;
    }
    if (((cmf << 8) | flg) % 31 != 0) {
        codec_log("codec: png IDAT zlib header check failed");
        codec_free(idat);
        return CODEC_ERR_FORMAT;
    }

    uint32_t bpp_bits = (uint32_t)channels * (uint32_t)depth;
    uint32_t rowbytes = (uint32_t)(((uint64_t)width * bpp_bits + 7) / 8);
    int filter_bpp = (int)((bpp_bits + 7) / 8);
    if (filter_bpp < 1) filter_bpp = 1;

    size_t raw_len = (size_t)(rowbytes + 1) * height;
    uint8_t *raw = (uint8_t *)codec_alloc(raw_len);
    if (!raw) { codec_free(idat); return CODEC_ERR_NOMEM; }

    size_t got = raw_len;
    codec_err_t err = codec_inflate(idat + 2, idat_len - 2 - 4, raw, &got);
    codec_free(idat);

    if (err != CODEC_OK) { codec_free(raw); return err; }
    if (got < raw_len) {
        char buf[112];
        snprintf(buf, sizeof(buf),
                 "codec: png inflated to %u bytes but %u were needed for "
                 "%ux%u", (unsigned)got, (unsigned)raw_len,
                 (unsigned)width, (unsigned)height);
        codec_log(buf);
        codec_free(raw);
        return CODEC_ERR_TRUNCATED;
    }

    unfilter(raw, height, rowbytes, filter_bpp);

    uint32_t *px = (uint32_t *)codec_alloc_zero((size_t)width * height * 4);
    if (!px) { codec_free(raw); return CODEC_ERR_NOMEM; }

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *line = raw + (size_t)y * (rowbytes + 1) + 1;
        uint32_t *dst = px + (size_t)y * width;

        for (uint32_t x = 0; x < width; x++) {
            uint32_t r = 0, g = 0, b = 0, a = 255;

            switch (color_type) {
            case 0: {                                   /* grey */
                /* `full` keeps every bit the file supplied, for the tRNS
                 * comparison; the channel itself is reduced to 8 bits. */
                unsigned full, v;
                if (depth == 16)      full = be16(line + (size_t)x * 2);
                else if (depth == 8)  full = line[x];
                else                  full = unpack_bits(line, x, depth) *
                                             255u / ((1u << depth) - 1u);
                v = (depth == 16) ? (full >> 8) : full;
                r = g = b = v;
                if (have_t_rns && t_rns_gray >= 0 && (int)full == t_rns_gray)
                    a = 0;
                break;
            }
            case 2: {                                   /* truecolour */
                const uint8_t *p = line + (size_t)x * (depth == 16 ? 6 : 3);
                /* 16 -> 8 takes the HIGH byte, explicitly.
                 *
                 * This has to be an explicit shift. Assigning the full 16-bit
                 * value and relying on the final `(r << 16)` to truncate keeps
                 * the *low* byte instead, because the 8 bits that survive that
                 * shift are the bottom 8 of r. For a sample of 0x1234 that
                 * yields channel 0x34 instead of 0x12 -- a one-bit-ish colour
                 * error on every 16-bit PNG, which looks like a mildly wrong
                 * image rather than a failure. */
                if (depth == 16) {
                    r = (uint32_t)be16(p) >> 8;
                    g = (uint32_t)be16(p + 2) >> 8;
                    b = (uint32_t)be16(p + 4) >> 8;
                } else { r = p[0]; g = p[1]; b = p[2]; }
                if (have_t_rns && t_rns_r >= 0 &&
                    r == (uint32_t)(t_rns_r >> 8) &&
                    g == (uint32_t)(t_rns_g >> 8) &&
                    b == (uint32_t)(t_rns_b >> 8))
                    a = 0;
                break;
            }
            case 3: {                                   /* palette */
                unsigned idx = (depth == 8) ? line[x] : unpack_bits(line, x, depth);
                if (idx >= palette_entries) {
                    char buf[112];
                    extern int snprintf(char *b, size_t sz, const char *f, ...);
                    snprintf(buf, sizeof(buf),
                             "codec: png pixel references palette index %u but "
                             "PLTE holds only %u entries", idx, palette_entries);
                    codec_log(buf);
                    codec_free(raw);
                    codec_free(px);
                    return CODEC_ERR_CORRUPT;
                }
                r = palette[idx * 3 + 0];
                g = palette[idx * 3 + 1];
                b = palette[idx * 3 + 2];
                a = t_rns_pal[idx];
                break;
            }
            case 4: {                                   /* grey + alpha */
                const uint8_t *p = line + (size_t)x * 2;
                if (depth == 16) {
                    r = g = b = (uint32_t)be16(p) >> 8;      /* high byte */
                    a = (uint32_t)be16(p + 2) >> 8;
                } else {
                    r = g = b = p[0];
                    a = p[1];
                }
                break;
            }
            default: {                                  /* 6: truecolour + alpha */
                const uint8_t *p = line + (size_t)x * (depth == 16 ? 8 : 4);
                if (depth == 16) {
                    r = (uint32_t)be16(p) >> 8;
                    g = (uint32_t)be16(p + 2) >> 8;
                    b = (uint32_t)be16(p + 4) >> 8;
                    a = (uint32_t)be16(p + 6) >> 8;
                } else {
                    r = p[0]; g = p[1]; b = p[2]; a = p[3];
                }
                break;
            }
            }

            /* NEGATIVE CONTROL (build with -DCODEC_NEG_RGBA): packs 0xRRGGBBAA, the
             * intuitive order that this codebase does NOT use. The host test
             * asserts that build fails the RGBA fixtures. Alpha in the bottom
             * byte is the single most likely mistake in this package and it
             * looks like a working image, so it needs a control, not a comment. */
#ifdef CODEC_NEG_RGBA
            dst[x] = (r << 24) | (g << 16) | (b << 8) | a;
#else
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;   /* 0xAARRGGBB */
#endif
        }
    }

    codec_free(raw);
    out->width = width;
    out->height = height;
    out->argb = px;
    return CODEC_OK;
}