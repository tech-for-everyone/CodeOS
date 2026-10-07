/* codec registry: format identification and dispatch.
 *
 * Support is a table, not a switch. The point is that "what can CodeOS open"
 * has exactly one answer, and the shell, the viewer and the host tests all ask
 * this table rather than each hardcoding a format list.
 *
 * A row with decode == NULL is a format this build recognises but cannot
 * convert. That is a real, reportable state -- better than pretending the
 * format is unknown, and better than a decoder that fails halfway through a
 * file. codec_fmt_supported() and the `note` column exist so a caller can say
 * which of the two situations it is in.
 *
 * The (void (*)(void)) casts around the decode pointers are the standard way to
 * silence -Wcast-function-type when one decoder takes codec_image_t* and
 * another takes codec_audio_t*. The signature is checked at the single place
 * it is actually invoked, in dispatch_img()/dispatch_aud() below.
 */

#include "codec.h"
#include "codec_alloc.h"
#include "codec_str.h"

static codec_log_fn g_log;

void codec_set_log(codec_log_fn fn) { g_log = fn; }

void codec_log(const char *msg) {
    if (g_log && msg) g_log(msg);
}

#define AS_FN(f) ((codec_decode_fn)(void (*)(void))(f))

const codec_entry_t codec_registry[] = {
    { CODEC_FMT_PNG,  "png",  "image/png",
      (const uint8_t *)"\x89PNG\r\n\x1a\n", 8, 0, CODEC_MEDIA_IMAGE,
      AS_FN(codec_png_decode),
      "8 and 16 bit, colour types 0/2/3/4/6. Adam7 interlaced is not implemented." },

    { CODEC_FMT_BMP,  "bmp",  "image/bmp", (const uint8_t *)"BM", 2, 0,
      CODEC_MEDIA_IMAGE, AS_FN(codec_bmp_decode),
      "Uncompressed 8/24/32-bit, top-down and bottom-up. RLE is not implemented." },

    { CODEC_FMT_WAV,  "wav",  "audio/wav", (const uint8_t *)"WAVE", 4, 8,
      CODEC_MEDIA_AUDIO, AS_FN(codec_wav_decode),
      "RIFF PCM and IEEE float, 8/16/24/32-bit, downmixed to mono s16. "
      "Compressed codecs carried inside a .wav are not implemented." },

    { CODEC_FMT_JPEG, "jpeg", "image/jpeg", (const uint8_t *)"\xff\xd8\xff", 3, 0,
      CODEC_MEDIA_IMAGE, NULL,
      "Baseline sequential JPEG (SOF0) is not implemented in this build." },

    { CODEC_FMT_GIF,  "gif",  "image/gif", (const uint8_t *)"GIF8", 4, 0,
      CODEC_MEDIA_IMAGE, NULL, "LZW and animation are not implemented." },

    { CODEC_FMT_QOI,  "qoi",  "image/qoi", (const uint8_t *)"qoif", 4, 0,
      CODEC_MEDIA_IMAGE, NULL, "Quite OK Image is not implemented." },

    { CODEC_FMT_AIFF, "aiff", "audio/aiff", (const uint8_t *)"AIFF", 4, 8,
      CODEC_MEDIA_AUDIO, NULL, "Not implemented." },

    { CODEC_FMT_FLAC, "flac", "audio/flac", (const uint8_t *)"fLaC", 4, 0,
      CODEC_MEDIA_AUDIO, NULL, "Not implemented." },

    /* PPM is identified only by a two-character P1..P6 tag, so it is listed but
     * not probed: matching "P" alone would misidentify unrelated text. */
    { CODEC_FMT_PPM,  "ppm",  "image/x-portable-pixmap", NULL, 0, 0,
      CODEC_MEDIA_IMAGE, NULL, "Not implemented, and deliberately not probed." },

    /* TGA is identified by a trailing TRUEVISION-XFILE footer at len-18, which a
     * positive magic_offset cannot express. Listed but not probed, rather than
     * given a probe that would misidentify other files. */
    { CODEC_FMT_TGA,  "tga",  "image/x-tga", NULL, 0, 0,
      CODEC_MEDIA_IMAGE, NULL,
      "Identified by a trailing TRUEVISION-XFILE footer; not probed." },
};

const int codec_registry_count =
    (int)(sizeof(codec_registry) / sizeof(codec_registry[0]));

static const codec_entry_t *lookup(codec_fmt_t f) {
    for (int i = 0; i < codec_registry_count; i++)
        if (codec_registry[i].fmt == f) return &codec_registry[i];
    return NULL;
}

const char *codec_fmt_name(codec_fmt_t f) {
    const codec_entry_t *e = lookup(f);
    return e ? e->name : "unknown";
}

const char *codec_strerror(codec_err_t e) {
    switch (e) {
    case CODEC_OK:              return "ok";
    case CODEC_ERR_ARG:         return "bad argument";
    case CODEC_ERR_NOMEM:       return "out of memory";
    case CODEC_ERR_TRUNCATED:   return "input truncated";
    case CODEC_ERR_FORMAT:      return "not the expected format";
    case CODEC_ERR_UNSUPPORTED: return "valid but unsupported variant";
    case CODEC_ERR_TOO_BIG:     return "dimensions too large";
    case CODEC_ERR_CORRUPT:     return "internally inconsistent data";
    default:                    return "unknown error";
    }
}

int codec_fmt_supported(codec_fmt_t f) {
    const codec_entry_t *e = lookup(f);
    return e && e->decode != NULL;
}

codec_fmt_t codec_probe(const uint8_t *data, size_t len) {
    if (!data) return CODEC_FMT_UNKNOWN;
    for (int i = 0; i < codec_registry_count; i++) {
        const codec_entry_t *e = &codec_registry[i];
        if (!e->magic || e->magic_len == 0) continue;
        if (len < e->magic_offset + e->magic_len) continue;
        if (memcmp(data + e->magic_offset, e->magic, e->magic_len) == 0)
            return e->fmt;
    }
    return CODEC_FMT_UNKNOWN;
}

/* A caller gets here only after the magic matched, so "unrecognised" would be a
 * lie. Say which variant is missing instead, quoting the table's own note. */
static codec_err_t reject(codec_fmt_t fmt) {
    const codec_entry_t *e = lookup(fmt);
    const char *note = (e && e->note) ? e->note : "no decoder for this format";
    codec_log("codec: format recognised but not decodable in this build -- ");
    codec_log(note);
    return CODEC_ERR_UNSUPPORTED;
}

/* Refuse a format whose decoder does not produce what the caller asked for.
 *
 * Both dispatchers share one `codec_decode_fn` type, which is what makes the
 * mismatch possible in the first place. Checking the table's media tag before
 * the call is the whole reason this function exists; the fuzz run reached
 * codec_wav_decode() through codec_decode_image() and the caller then
 * interpreted a codec_audio_t as a codec_image_t. */
static codec_err_t reject_media(const codec_entry_t *e, codec_media_t want) {
    char buf[128];
    extern int snprintf(char *b, size_t n, const char *f, ...);
    snprintf(buf, sizeof(buf),
             "codec: %s is an %s format, not an %s one -- use the matching "
             "codec_decode_* entry point",
             e->name,
             e->media == CODEC_MEDIA_AUDIO ? "audio" : "image",
             want == CODEC_MEDIA_AUDIO ? "audio" : "image");
    codec_log(buf);
    return CODEC_ERR_ARG;
}

/* The decoder functions are one common type so they fit one table column; these
 * two wrappers restore the real signature at the call. The (void *) round trip
 * through the common pointer type is what makes that conversion defined. */
static codec_err_t dispatch_img(const codec_entry_t *e, const uint8_t *d, size_t n,
                                codec_image_t *out) {
    codec_err_t (*fn)(const uint8_t *, size_t, codec_image_t *) =
        (codec_err_t (*)(const uint8_t *, size_t, codec_image_t *))(void *)e->decode;
    codec_err_t rc = fn(d, n, out);
    /* Recorded whatever the outcome, so a caller can report "this was a jpeg
     * and we do not decode those" rather than only "something failed". */
    out->fmt = e->fmt;
    return rc;
}

static codec_err_t dispatch_aud(const codec_entry_t *e, const uint8_t *d, size_t n,
                                codec_audio_t *out) {
    codec_err_t (*fn)(const uint8_t *, size_t, codec_audio_t *) =
        (codec_err_t (*)(const uint8_t *, size_t, codec_audio_t *))(void *)e->decode;
    codec_err_t rc = fn(d, n, out);
    out->fmt = e->fmt;
    return rc;
}

codec_err_t codec_decode_image(codec_fmt_t fmt, const uint8_t *data, size_t len,
                               codec_image_t *out) {
    if (!data || len == 0 || !out) return CODEC_ERR_ARG;
    const codec_entry_t *e = lookup(fmt);
    if (!e || !e->decode) return reject(fmt);
    if (e->media != CODEC_MEDIA_IMAGE) return reject_media(e, CODEC_MEDIA_IMAGE);
    return dispatch_img(e, data, len, out);
}

codec_err_t codec_decode_audio(codec_fmt_t fmt, const uint8_t *data, size_t len,
                               codec_audio_t *out) {
    if (!data || len == 0 || !out) return CODEC_ERR_ARG;
    const codec_entry_t *e = lookup(fmt);
    if (!e || !e->decode) return reject(fmt);
    if (e->media != CODEC_MEDIA_AUDIO) return reject_media(e, CODEC_MEDIA_AUDIO);
    return dispatch_aud(e, data, len, out);
}

codec_err_t codec_decode_auto(const uint8_t *data, size_t len, codec_image_t *out) {
    if (!data || len == 0 || !out) return CODEC_ERR_ARG;
    codec_fmt_t f = codec_probe(data, len);
    if (f == CODEC_FMT_UNKNOWN) {
        codec_log("codec: no known image format matched this header");
        return CODEC_ERR_FORMAT;
    }
    return codec_decode_image(f, data, len, out);
}

codec_err_t codec_decode_auto_audio(const uint8_t *data, size_t len, codec_audio_t *out) {
    if (!data || len == 0 || !out) return CODEC_ERR_ARG;
    codec_fmt_t f = codec_probe(data, len);
    if (f == CODEC_FMT_UNKNOWN) {
        codec_log("codec: no known audio format matched this header");
        return CODEC_ERR_FORMAT;
    }
    return codec_decode_audio(f, data, len, out);
}

codec_media_t codec_fmt_media(codec_fmt_t f) {
    const codec_entry_t *e = lookup(f);
    return e ? e->media : CODEC_MEDIA_NONE;
}

void codec_image_free(codec_image_t *img) {
    if (!img) return;
    codec_free(img->argb);
    img->argb = NULL;
    img->width = 0;
    img->height = 0;
}

void codec_audio_free(codec_audio_t *aud) {
    if (!aud) return;
    codec_free(aud->samples);
    aud->samples = NULL;
    aud->frames = 0;
    aud->rate = 0;
}