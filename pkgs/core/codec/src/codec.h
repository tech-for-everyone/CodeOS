/* codec -- image and audio decoding for CodeOS.
 *
 * ── Why this exists ───────────────────────────────────────────────────────
 *
 * The tree already contains PNG, BMP and baseline JPEG decoders, vendored
 * under kernel/lvgl/src/extra/libs/ (lodepng.c, lv_bmp.c, tjpgd.c). None of
 * them can be used here, for two independent reasons:
 *
 *   1. kernel/Makefile excludes them. SRC_C is built with a -not -path rule
 *      that drops the whole lvgl/src/extra/libs tree, so they are never
 *      compiled, and none of their symbols appear in the linked kernel
 *      (verified with nm on codeos-1-kernel.bin: lodepng_decode32, tjd_decomp
 *      and tjpgd_get_info are all absent).
 *   2. Even un-excluded they would not compile. The kernel is built with
 *      `-nostdinc` plus clang's own builtin headers only, so there is no
 *      libc header at all, and lodepng.c opens with `#include <limits.h>`
 *      and `#include <stdlib.h>`.
 *
 * FFmpeg, for video, is not vendored either, and there is no working package
 * manager that could fetch it. So every decoder here is written from scratch
 * against freestanding rules: no libc beyond the kernel's own string.h, and
 * nothing that assumes a hosted environment.
 *
 * ── Pixel order: 0xAARRGGBB, alpha in the TOP byte ────────────────────────
 *
 * This is not the intuitive RGBA order and it is the single easiest thing to
 * get wrong here, because the mistake produces plausible-looking colours
 * rather than an obvious failure. The convention is fixed by
 * lgame_color_pack() in pkgs/core/panels/src/lgame.h:
 *
 *     return ((c.a << 24) | (c.r << 16) | (c.g << 8) | c.b);
 *
 * Decoders must emit that layout, or images display with channels rotated.
 */

#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>
#include <stddef.h>

/* Refuse absurd dimensions before allocating. A 65535x65535 header is 17 GB,
 * and a corrupt width/height is the standard way a decoder turns into an
 * out-of-bounds write. */
#define CODEC_MAX_DIM      8192
#define CODEC_MAX_PIXELS   (16u * 1024u * 1024u)

typedef enum {
    CODEC_OK             =  0,
    CODEC_ERR_ARG        = -1,  /* null pointer, empty input */
    CODEC_ERR_NOMEM      = -2,  /* malloc failed, or image over CODEC_MAX_PIXELS */
    CODEC_ERR_TRUNCATED  = -3,  /* input ended mid-structure */
    CODEC_ERR_FORMAT     = -4,  /* magic matched but the body is not that format */
    CODEC_ERR_UNSUPPORTED= -5,  /* a real variant this decoder does not implement */
    CODEC_ERR_TOO_BIG    = -6,  /* dimensions exceed the caps above */
    CODEC_ERR_CORRUPT    = -7   /* internally inconsistent (bad Huffman table etc.) */
} codec_err_t;

/* Not a real result: a test-only sentinel meaning "this must be refused, and I
 * do not care which honest refusal code it is". Several distinct failures all
 * correctly answer "not ok" for a malformed file, and a test that pinned one
 * code would be asserting an implementation detail rather than the contract. */
#define CODEC_ERR_ANY  ((codec_err_t)0x7FFFFFFF)

typedef enum {
    CODEC_FMT_UNKNOWN = 0,
    CODEC_FMT_PNG,
    CODEC_FMT_BMP,
    CODEC_FMT_JPEG,
    CODEC_FMT_GIF,
    CODEC_FMT_QOI,
    CODEC_FMT_TGA,
    CODEC_FMT_PPM,
    CODEC_FMT_WAV,
    CODEC_FMT_AIFF,
    CODEC_FMT_FLAC
} codec_fmt_t;

/* Decoded image. argb is width*height 32-bit words in 0xAARRGGBB order, alpha
 * always 255 for opaque formats. Ownership passes to the caller on success and
 * must be released with codec_image_free(). */
typedef struct {
    uint32_t       width;
    uint32_t       height;
    uint32_t      *argb;
    /* Which format produced this. Set by the dispatcher, never by a decoder:
     * the decoder knows nothing about the registry entry it was reached
     * through. Without it a caller holding a `codec_image_t` has no way to
     * report what it decoded, and has to probe the buffer a second time. */
    codec_fmt_t    fmt;
} codec_image_t;

/* Decoded audio. Mono signed 16-bit host-endian PCM at `rate`.
 *
 * The kernel mixer (kernel/kernel/audio_mixer.c) takes interleaved int16_t and
 * emits one int16_t per output sample, with no resampling and no downmix, even
 * though mixer_open_source() records a channel count. Feeding it interleaved
 * stereo therefore plays at double speed with the channels alternating. Every
 * decoder here downmixes to mono for that reason; it is a property of the
 * mixer, not a shortcut. */
typedef struct {
    uint32_t rate;
    uint32_t frames;        /* mono sample frames */
    int16_t *samples;
    codec_fmt_t fmt;        /* see codec_image_t::fmt */
} codec_audio_t;

/* ── Registry ────────────────────────────────────────────────────────────
 *
 * A table of decoders, not a switch statement, so that "what can this OS
 * open" is data. codec_list() walks it, which is how the shell and the host
 * tests enumerate real support instead of hardcoding it.
 */
typedef codec_err_t (*codec_decode_fn)(const uint8_t *data, size_t len, void *out);

/* Which media kind a decoder consumes and produces.
 *
 * This is not decoration. The registry stores decode pointers as a common
 * type, so without this tag the two dispatchers are free to call the wrong
 * function: codec_decode_image() on a .wav reaches codec_wav_decode(), which
 * fills a codec_audio_t through the void*, and the caller then reads
 * width/height/argb from what is actually a codec_audio_t. The fuzz run found
 * exactly that, as an 8-byte heap allocation read as a 4-byte-per-pixel
 * image buffer. The tag makes the dispatchers refuse the mismatch. */
typedef enum {
    CODEC_MEDIA_IMAGE = 0,
    CODEC_MEDIA_AUDIO,
    CODEC_MEDIA_NONE       /* recognised, no decoder, media kind moot */
} codec_media_t;

typedef struct {
    codec_fmt_t   fmt;
    const char   *name;         /* "png", "wav", ... lowercase */
    const char   *mime;
    const uint8_t *magic;       /* byte string compared with memcmp, see magic_len */
    size_t        magic_len;
    size_t        magic_offset; /* >0 for formats whose magic is not at offset 0 */
    codec_media_t media;        /* what decode() consumes and produces */
    codec_decode_fn decode;     /* NULL = recognised but not implemented */
    const char   *note;         /* why it is NULL, or notable limits */
} codec_entry_t;

extern const codec_entry_t codec_registry[];
extern const int codec_registry_count;

/* Diagnostics. The kernel builds with no stdio, so the host test installs a
 * sink here and every decoder routes its complaints through it instead of
 * writing anywhere itself.
 *
 * There is deliberately no codec_logf(). The kernel's string.h has snprintf
 * but no vsnprintf, so a varargs wrapper would mean writing a printf clone to
 * format diagnostics. Call sites that want numbers format them with snprintf
 * and pass the finished string. */
typedef void (*codec_log_fn)(const char *msg);
void codec_set_log(codec_log_fn fn);
void codec_log(const char *msg);

/* Identify a buffer by magic. CODEC_FMT_UNKNOWN if nothing matches. */
codec_fmt_t codec_probe(const uint8_t *data, size_t len);

/* Identify and decode in one step. */
codec_err_t codec_decode_auto(const uint8_t *data, size_t len, codec_image_t *out);
codec_err_t codec_decode_image(codec_fmt_t fmt, const uint8_t *data, size_t len,
                               codec_image_t *out);
codec_err_t codec_decode_audio(codec_fmt_t fmt, const uint8_t *data, size_t len,
                               codec_audio_t *out);

codec_err_t codec_decode_auto_audio(const uint8_t *data, size_t len, codec_audio_t *out);

void codec_image_free(codec_image_t *img);
void codec_audio_free(codec_audio_t *aud);

const char     *codec_fmt_name(codec_fmt_t f);
const char     *codec_strerror(codec_err_t e);
int             codec_fmt_supported(codec_fmt_t f);   /* 1 if a decoder exists */
codec_media_t   codec_fmt_media(codec_fmt_t f);       /* CODEC_MEDIA_NONE if unknown */

/* ── Individual decoders ──────────────────────────────────────────────────
 * Exposed for the host tests, which call them directly to prove a decoder is
 * sensitive to a specific fault rather than only through the dispatch path.
 */
codec_err_t codec_png_decode(const uint8_t *d, size_t n, codec_image_t *out);
codec_err_t codec_bmp_decode(const uint8_t *d, size_t n, codec_image_t *out);
codec_err_t codec_jpeg_decode(const uint8_t *d, size_t n, codec_image_t *out);
codec_err_t codec_wav_decode(const uint8_t *d, size_t n, codec_audio_t *out);

/* Raw DEFLATE (RFC 1951). Declared here because PNG needs it and because a
 * gzip container later wants the same core. Not part of the registry: it
 * produces bytes, not pixels.
 *
 * out_len is in/out: on entry the capacity of out, on exit the bytes written.
 */
codec_err_t codec_inflate(const uint8_t *src, size_t src_len,
                          uint8_t *out, size_t *out_len);

#endif /* CODEC_H */