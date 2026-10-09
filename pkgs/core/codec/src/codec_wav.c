/* WAV / RIFF audio decoder.
 *
 * Handles uncompressed WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT at 8, 16, 24
 * and 32 bits, mono through 8 channels, downmixed to mono signed 16-bit.
 *
 * Downmixing is not a convenience. kernel/kernel/audio_mixer.c reads one int16_t
 * per output sample and does no channel or rate conversion at all, even though
 * mixer_open_source() stores a channel count. Handing it interleaved stereo
 * plays the left channel, then the right, at the right pitch -- a fast
 * warbling chipmunk, not a bug anyone would recognise as a bug. So a decoder
 * that is going to be played through this mixer must emit mono.
 *
 * Compressed payloads (mp3, aac, adpcm, ...) are detected and refused by name.
 * Their four-byte format tags are far more likely to appear by accident than a
 * PCM tag, and guessing would produce noise.
 */

#include "codec.h"
#include "codec_alloc.h"
#include "codec_str.h"

#define WAVE_FORMAT_PCM        0x0001
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void fail(const char *why, unsigned a, unsigned b) {
    char buf[128];
    snprintf(buf, sizeof(buf), "codec: wav %s (%u, %u)", why, a, b);
    codec_log(buf);
}

/* IEEE 754 binary32 -> int16, using integer arithmetic only.
 *
 * The kernel is compiled -mno-sse -mno-sse2, so there is no float ABI to lean
 * on: a `float` local plus a comparison can pull in compiler-rt softfloat
 * helpers that this freestanding link does not have. This is the same reason
 * lgame uses 16.16 fixed point. (pkgs/core/panels/src/lgame.h documents that
 * constraint; do not reintroduce float into kernel-compiled code.)
 *
 * value = (-1)^sign * (1 + mant/2^23) * 2^exp, clamped to [-1, 1] and scaled
 * by 32767. Working in a 24-bit fixed mantissa means the common cases are exact
 * shifts, not multiplies.
 */
static int16_t f32_to_s16(uint32_t b) {
    unsigned sign = (b >> 31) & 1u;
    int      exp  = (int)((b >> 23) & 0xFFu) - 127;
    uint32_t mant = b & 0x7FFFFFu;

    if (exp == 128) return 0;                       /* Inf or NaN -> silence */
    if (exp >= 0)   return sign ? -32767 : 32767;   /* |v| >= 1.0, saturate */
    if (exp < -15)  return 0;                       /* below ~1/32768, inaudible */

    uint32_t mag = (mant | 0x800000u) >> (unsigned)(8 - exp);   /* |v| * 32768 */
    if (mag > 32767u) mag = 32767u;
    return sign ? (int16_t)(-(int32_t)mag) : (int16_t)mag;
}

/* Normalise one sample of the given width to signed 16-bit. */
static int16_t sample_at(const uint8_t *p, unsigned bits, int is_float) {
    if (is_float) return f32_to_s16(le32(p));

    switch (bits) {
    case 8:
        return (int16_t)(((int)p[0] - 128) * 256);      /* unsigned, centred */
    case 16: {
        int16_t v = (int16_t)le16(p);
        return v;
    }
    case 24: {
        int32_t v = (int32_t)(((uint32_t)p[0]) |
                              ((uint32_t)p[1] << 8) |
                              ((uint32_t)p[2] << 16));
        if (v & 0x800000) v |= ~0xFFFFFF;               /* sign-extend */
        return (int16_t)(v >> 8);                       /* keep the high 16 bits */
    }
    case 32: {
        int32_t v = (int32_t)le32(p);
        return (int16_t)(v >> 16);
    }
    default:
        return 0;
    }
}

codec_err_t codec_wav_decode(const uint8_t *d, size_t n, codec_audio_t *out) {
    if (!d || !out) return CODEC_ERR_ARG;
    memset(out, 0, sizeof(*out));

    if (n < 12) return CODEC_ERR_TRUNCATED;
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0)
        return CODEC_ERR_FORMAT;

    uint32_t riff_size = le32(d + 4);
    /* Trust whichever bound is smaller. A truncated file has riff_size past the
     * end; an over-declared one may not. Either way, never read past n. */
    size_t avail = (size_t)riff_size + 8;
    if (avail > n) avail = n;

    unsigned fmt_tag = 0, channels = 0, bits = 0;
    unsigned rate = 0;
    size_t data_off = 0, data_len = 0;
    int have_fmt = 0, have_data = 0;
    int is_float = 0;

    size_t pos = 12;
    while (pos + 8 <= avail) {
        const uint8_t *cid = d + pos;
        uint32_t csz = le32(d + pos + 4);
        size_t body = pos + 8;

        if (csz > avail - body) {
            /* The final data chunk is routinely declared larger than the bytes
             * actually present on a truncated transfer. Clamp and keep going,
             * then let the frame count work out. */
            csz = (uint32_t)(avail - body);
        }

        if (memcmp(cid, "fmt ", 4) == 0 && csz >= 16) {
            fmt_tag  = le16(d + body);
            channels = le16(d + body + 2);
            rate     = le32(d + body + 4);
            bits     = le16(d + body + 14);

            if (fmt_tag == WAVE_FORMAT_EXTENSIBLE && csz >= 26) {
                /* The real format sits in the first two bytes of the SubFormat
                 * GUID, which otherwise reads as 0x0001 for almost everything. */
                fmt_tag = le16(d + body + 24);
            }
            if (fmt_tag != WAVE_FORMAT_PCM && fmt_tag != WAVE_FORMAT_IEEE_FLOAT) {
                fail("compressed format tag is not decodable", fmt_tag, bits);
                return CODEC_ERR_UNSUPPORTED;
            }
            is_float = (fmt_tag == WAVE_FORMAT_IEEE_FLOAT);
            have_fmt = 1;
        } else if (memcmp(cid, "data", 4) == 0) {
            data_off = body;
            data_len = csz;
            have_data = 1;
        }

        /* Chunks are word-aligned; an odd size is followed by a pad byte. */
        pos = body + csz + (csz & 1u);
    }

    if (!have_fmt)  { codec_log("codec: wav has no fmt chunk");   return CODEC_ERR_FORMAT; }
    if (!have_data) { codec_log("codec: wav has no data chunk");  return CODEC_ERR_TRUNCATED; }

    if (channels == 0 || channels > 8) { fail("channel count is out of range", channels, 0); return CODEC_ERR_FORMAT; }
    if (rate == 0 || rate > 384000)    { fail("sample rate is out of range", rate, 0);     return CODEC_ERR_FORMAT; }
    if (is_float && bits != 32)        { fail("float sample must be 32-bit", bits, 0);     return CODEC_ERR_FORMAT; }
    if (!is_float && bits != 8 && bits != 16 && bits != 24 && bits != 32) {
        fail("unsupported bit depth", bits, channels);
        return CODEC_ERR_UNSUPPORTED;
    }

    unsigned bytes_per_frame = (bits / 8u) * channels;
    if (bytes_per_frame == 0) return CODEC_ERR_FORMAT;

    uint32_t frames = (uint32_t)(data_len / bytes_per_frame);
    if (frames == 0) {
        codec_log("codec: wav data chunk holds no complete sample frames");
        return CODEC_ERR_TRUNCATED;
    }
    /* One minute of 48 kHz mono is ~5.8 MB; anything beyond an hour is not a
     * file, it is a length field that was never checked. */
    if (frames > rate * 3600u) {
        fail("declared length exceeds an hour at this rate", frames, rate);
        return CODEC_ERR_TOO_BIG;
    }

    int16_t *mono = (int16_t *)codec_alloc((size_t)frames * sizeof(int16_t));
    if (!mono) return CODEC_ERR_NOMEM;

    for (uint32_t f = 0; f < frames; f++) {
        const uint8_t *p = d + data_off + (size_t)f * bytes_per_frame;

        if (channels == 1) {
            mono[f] = sample_at(p, bits, is_float);
        } else {
            /* Average the channels rather than summing: summing two full-scale
             * samples clips, and clipping is audible where averaging is not. */
            int32_t acc = 0;
            for (unsigned c = 0; c < channels; c++)
                acc += sample_at(p + (size_t)c * (bits / 8u), bits, is_float);
            mono[f] = (int16_t)(acc / (int32_t)channels);
        }
    }

    out->rate = rate;
    out->frames = frames;
    out->samples = mono;
    return CODEC_OK;
}