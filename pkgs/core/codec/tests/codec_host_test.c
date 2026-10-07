/* codec host test driver.
 *
 * Compiles the *real* decoders (CODEC_HOST_TEST switches codec_alloc.h to the
 * host libc) and checks them against fixtures whose expected pixels were
 * computed by tests/gen_fixtures.py in a separate implementation. Nothing here
 * calls into the guest, so this runs in well under a second -- the contrast
 * with "boot the ISO and look" is the whole reason it exists.
 *
 * Usage: codec_host_test <fixture-dir>
 * Exits non-zero if any check fails.
 */

#define CODEC_HOST_TEST 1

#include "codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expected.h"

static int checks = 0, fails = 0;

static void ok(const char *m)  { checks++; printf("  ok   %s\n", m); }
static void bad(const char *m) { checks++; fails++; printf("  FAIL %s\n", m); }
static void check(int cond, const char *m) { if (cond) ok(m); else bad(m); }

static void sink(const char *msg) { printf("       [codec] %s\n", msg); }

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    uint8_t *b = (uint8_t *)malloc((size_t)len ? (size_t)len : 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); fclose(f); return NULL; }
    fclose(f);
    *n = (size_t)len;
    return b;
}

static const char *dir;

/* Decode `name` and require the pixels to equal `expect` exactly, with a
 * message naming the first pixel that differs. "Exactly" matters: a tolerance
 * would let a wrong alpha or an off-by-one channel through, which is precisely
 * the class of bug these decoders are prone to. */
static void expect_image(const char *name, const char *what,
                         const uint32_t *expect, size_t want_len,
                         int want_w, int want_h) {
    char path[512];
    size_t n = 0;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    uint8_t *buf = slurp(path, &n);
    if (!buf) { bad(what); return; }

    codec_image_t img;
    memset(&img, 0, sizeof(img));
    codec_err_t e = codec_decode_auto(buf, n, &img);
    if (e != CODEC_OK) {
        char m[256];
        snprintf(m, sizeof(m), "%s: decode failed: %s", what, codec_strerror(e));
        bad(m);
        free(buf);
        return;
    }

    char m[256];
    if ((int)img.width != want_w || (int)img.height != want_h) {
        snprintf(m, sizeof(m), "%s: got %ux%u, want %dx%d", what,
                 (unsigned)img.width, (unsigned)img.height, want_w, want_h);
        bad(m);
    } else {
        ok(what);
    }

    size_t have = (size_t)img.width * img.height;
    if (have != want_len) {
        snprintf(m, sizeof(m), "%s: %u pixels, expected %u", what,
                 (unsigned)have, (unsigned)want_len);
        bad(m);
    } else if (memcmp(img.argb, expect, have * 4) != 0) {
        size_t i;
        for (i = 0; i < have; i++)
            if (img.argb[i] != expect[i]) break;
        snprintf(m, sizeof(m),
                 "%s: pixel %u is 0x%08x, expected 0x%08x", what,
                 (unsigned)i, (unsigned)img.argb[i], (unsigned)expect[i]);
        bad(m);
    } else {
        snprintf(m, sizeof(m), "%s: all %u pixels exact", what, (unsigned)have);
        ok(m);
    }

    codec_image_free(&img);
    free(buf);
}

/* Refusal tests that need to name the format: expect_err() assumes PNG, which
 * is wrong for the BMP and RLE fixtures. */
static codec_err_t decode_err_as(codec_fmt_t fmt, const char *name) {
    char path[512];
    size_t n = 0;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    uint8_t *buf = slurp(path, &n);
    if (!buf) return CODEC_ERR_ARG;

    codec_image_t img;
    memset(&img, 0, sizeof(img));
    codec_err_t e = codec_decode_image(fmt, buf, n, &img);
    codec_image_free(&img);
    free(buf);
    return e;
}

static void expect_err_as(codec_fmt_t fmt, const char *name, const char *what,
                          codec_err_t want) {
    codec_err_t e = decode_err_as(fmt, name);
    char m[256];
    if (want == CODEC_ERR_ANY) {
        check(e != CODEC_OK, what);
    } else {
        snprintf(m, sizeof(m), "%s (want %s, got %s)", what,
                 codec_strerror(want), codec_strerror(e));
        check(e == want, m);
    }
}

static void expect_err_png(const char *name, const char *what, codec_err_t want) {
    expect_err_as(CODEC_FMT_PNG, name, what, want);
}

static void expect_err(const char *name, const char *what, codec_err_t want) {
    codec_err_t e = decode_err_as(CODEC_FMT_PNG, name);
    char m[256];

    /* Accept either the specific code or simply "not ok": the point of these
     * fixtures is that malformed input is refused, not which of several honest
     * refusal codes it gets. */
    if (want == CODEC_ERR_ANY) {
        check(e != CODEC_OK, what);
    } else {
        snprintf(m, sizeof(m), "%s (want %s, got %s)", what,
                 codec_strerror(want), codec_strerror(e));
        check(e == want, m);
    }
}

static void expect_probe(const char *name, codec_fmt_t want, const char *what) {
    char path[512];
    size_t n = 0;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    uint8_t *buf = slurp(path, &n);
    if (!buf) { bad(what); return; }
    codec_fmt_t f = codec_probe(buf, n);
    char m[256];
    snprintf(m, sizeof(m), "%s: probed %s, want %s", what,
             codec_fmt_name(f), codec_fmt_name(want));
    check(f == want, m);
    free(buf);
}

static void expect_audio(const char *name, const char *what,
                         const uint32_t *expect, size_t want_frames,
                         uint32_t want_rate) {
    char path[512];
    size_t n = 0;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    uint8_t *buf = slurp(path, &n);
    if (!buf) { bad(what); return; }

    codec_audio_t a;
    memset(&a, 0, sizeof(a));
    codec_err_t e = codec_decode_auto_audio(buf, n, &a);
    char m[256];
    if (e != CODEC_OK) {
        snprintf(m, sizeof(m), "%s: decode failed: %s", what, codec_strerror(e));
        bad(m);
        free(buf);
        return;
    }

    if (a.rate != want_rate) {
        snprintf(m, sizeof(m), "%s: rate %u, want %u", what,
                 (unsigned)a.rate, (unsigned)want_rate);
        bad(m);
    } else {
        ok(what);
    }

    if (a.frames != want_frames) {
        snprintf(m, sizeof(m), "%s: %u frames, want %u", what,
                 (unsigned)a.frames, (unsigned)want_frames);
        bad(m);
    } else {
        size_t bad_i = 0;
        for (size_t i = 0; i < want_frames; i++)
            if ((uint32_t)(int32_t)a.samples[i] != expect[i]) { bad_i = i; break; }
        if (bad_i) {
            snprintf(m, sizeof(m),
                     "%s: frame %u is %d, expected %d", what, (unsigned)bad_i,
                     (int)a.samples[bad_i], (int)(int32_t)expect[bad_i]);
            bad(m);
        } else {
            snprintf(m, sizeof(m), "%s: all %u frames exact", what,
                     (unsigned)want_frames);
            ok(m);
        }
    }

    codec_audio_free(&a);
    free(buf);
}

int main(int argc, char **argv) {
    dir = (argc > 1) ? argv[1] : "fixtures";
    codec_set_log(sink);

    printf("codec host test (fixtures from %s)\n", dir);

    /* ── registry: what does this build claim to support? ── */
    check(codec_fmt_supported(CODEC_FMT_PNG),  "png is supported");
    check(codec_fmt_supported(CODEC_FMT_BMP),  "bmp is supported");
    check(codec_fmt_supported(CODEC_FMT_WAV),  "wav is supported");
    check(!codec_fmt_supported(CODEC_FMT_JPEG), "jpeg is reported unsupported");
    check(!codec_fmt_supported(CODEC_FMT_GIF),  "gif is reported unsupported");
    check(strcmp(codec_fmt_name(CODEC_FMT_PNG), "png") == 0, "png name");
    check(strcmp(codec_strerror(CODEC_ERR_UNSUPPORTED), "valid but unsupported variant") == 0,
          "unsupported has its own message, distinct from unknown");

    /* A format that is recognised but has no decoder must be refused with
     * UNSUPPORTED, never FORMAT -- the caller matched the magic already. */
    {
        static const uint8_t jpeg[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 0 };
        codec_image_t img;
        memset(&img, 0, sizeof(img));
        codec_err_t e = codec_decode_image(CODEC_FMT_JPEG, jpeg, sizeof(jpeg), &img);
        check(e == CODEC_ERR_UNSUPPORTED, "jpeg refused as unsupported, not as invalid");
    }

    check(codec_fmt_media(CODEC_FMT_PNG) == CODEC_MEDIA_IMAGE, "png is an image format");
    check(codec_fmt_media(CODEC_FMT_WAV) == CODEC_MEDIA_AUDIO, "wav is an audio format");

    /* Cross-media dispatch must be refused. The two dispatchers share one
     * function-pointer type, so without an explicit media check
     * codec_decode_image() on a wav runs the audio decoder and hands the caller
     * a codec_audio_t to read as a codec_image_t -- width, height and the pixel
     * pointer all come from the wrong offsets. The fuzz run found this; the
     * check below is what stops it coming back. */
    {
        size_t n = 0;
        char path[512];
        snprintf(path, sizeof(path), "%s/m16.wav", dir);
        uint8_t *buf = slurp(path, &n);
        if (buf) {
            codec_image_t img;
            memset(&img, 0, sizeof(img));
            codec_err_t e = codec_decode_image(CODEC_FMT_WAV, buf, n, &img);
            check(e == CODEC_ERR_ARG, "wav refused by the image entry point");
            check(img.argb == NULL && img.width == 0 && img.height == 0,
                  "the refused image output is untouched");
            codec_image_free(&img);
            free(buf);
        } else {
            bad("m16.wav readable");
        }

        snprintf(path, sizeof(path), "%s/rgb8.png", dir);
        uint8_t *pbuf = slurp(path, &n);
        if (pbuf) {
            codec_audio_t aud;
            memset(&aud, 0, sizeof(aud));
            check(codec_decode_audio(CODEC_FMT_PNG, pbuf, n, &aud) == CODEC_ERR_ARG,
                  "png refused by the audio entry point");
            check(aud.samples == NULL && aud.frames == 0,
                  "the refused audio output is untouched");
            codec_audio_free(&aud);
            free(pbuf);
        } else {
            bad("rgb8.png readable");
        }
    }

    /* ── probing ── */
    expect_probe("rgb8.png", CODEC_FMT_PNG, "png magic");
    expect_probe("bgr24.bmp", CODEC_FMT_BMP, "bmp magic");
    expect_probe("m16.wav", CODEC_FMT_WAV, "wav magic (WAVE at offset 8)");
    {
        uint8_t junk[64];
        memset(junk, 0xA5, sizeof(junk));
        check(codec_probe(junk, sizeof(junk)) == CODEC_FMT_UNKNOWN,
              "random bytes probe as unknown");
        check(codec_probe(junk, 2) == CODEC_FMT_UNKNOWN,
              "a 2-byte buffer matches nothing, even if the tail would");
    }

    /* ── PNG ── */
    expect_image("rgb8.png", "png truecolour 8-bit",
                 rgb8, RGB8_W, 4, 2);
    expect_image("rgb8_filters.png", "png all five filters",
                 rgb8_filters, RGB8_FILTERS_W, 4, 8);
    expect_image("rgb8_stored.png", "png deflate stored blocks",
                 rgb8_stored, RGB8_STORED_W, 4, 2);
    expect_image("rgb8_dynamic.png", "png deflate dynamic huffman",
                 rgb8_dynamic, RGB8_DYNAMIC_W, 64, 16);
    expect_image("rgba8.png", "png rgba 8-bit",
                 rgba8, RGBA8_W, 2, 2);
    expect_image("grey8.png", "png grey 8-bit",
                 grey8, GREY8_W, 4, 1);
    expect_image("grey1.png", "png grey 1-bit (bit unpacking)",
                 grey1, GREY1_W, 4, 1);
    expect_image("pal8.png", "png palette 8-bit with tRNS",
                 pal8, PAL8_W, 4, 1);
    expect_image("pal4.png", "png palette 4-bit with tRNS",
                 pal4, PAL4_W, 4, 1);

    /* PLTE declares three entries but the image references index 3. Reading
     * that would run past the copied palette, so it must be refused. */
    expect_err_png("pal_short.png", "palette index beyond PLTE is refused",
                   CODEC_ERR_CORRUPT);
    expect_image("rgb16.png", "png truecolour 16-bit (high byte kept)",
                 rgb16, RGB16_W, 2, 1);
    expect_image("grey16.png", "png grey 16-bit (high byte kept)",
                 grey16, GREY16_W, 2, 1);
    expect_image("rgba16.png", "png rgba 16-bit (high byte kept for alpha too)",
                 rgba16, RGBA16_W, 1, 1);

    /* Stated literally as well as through the table. A decoder that assigns
     * the full 16-bit sample and lets the final shift truncate keeps the low
     * byte, so 0x1234 becomes 0x34 -- plausible-looking, wrong on every pixel. */
    check(rgb16[0] == 0xFF12569Au && rgb16[1] == 0xFFDE1133u,
          "16-bit samples reduce to the high byte, not the low one");

    /* The alpha-in-the-top-byte convention, asserted directly rather than only
     * through the generated table, because getting it wrong produces plausible
     * colours rather than a visible failure. */
    /* Asserted against the literal rather than only against the generated table.
 * rgba8[1] is r=40 g=50 b=60 a=128, so the correct word is 0x80403C28: alpha
 * 0x80 in the top byte. Reading it as 0xRRGGBBAA would give 0x283C3C80, a
 * different colour that still looks like a picture. */
/* rgba8[1] is the fixture's second pixel: bytes 40,50,60 with alpha 128, i.e.
 * r=0x28 g=0x32 b=0x3C a=0x80. Packed 0xAARRGGBB that is 0x8028323C. Reading
 * the same bytes as 0xRRGGBBAA would give 0x28323C80 -- a different, still
 * picture-like colour, which is why this is asserted as a literal and not only
 * through the generated table. */
check(rgba8[1] == 0x8028323Cu,
      "0x80 alpha packs into the top byte, not the bottom");

    expect_err("interlaced.png", "adam7 interlaced refused",
               CODEC_ERR_UNSUPPORTED);
    expect_err("huge.png", "20000x20000 refused before allocating",
               CODEC_ERR_TOO_BIG);
    expect_err("corrupt.png", "corrupt IDAT caught by CRC",
               CODEC_ERR_CORRUPT);
    expect_err("trunc.png", "truncated png refused", CODEC_ERR_ANY);

    {
        static const uint8_t badsig[] =
            { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
        codec_image_t img;
        memset(&img, 0, sizeof(img));
        check(codec_decode_image(CODEC_FMT_PNG, badsig, sizeof(badsig), &img)
                  == CODEC_ERR_TRUNCATED,
              "a header with nothing after it is truncated, not corrupt");
    }

    /* ── BMP ── */
    expect_image("bgr24.bmp", "bmp 24-bit, BGR order",
                 bgr24, BGR24_W, 2, 2);
    expect_image("bgra32_topdown.bmp", "bmp 32-bit top-down with alpha",
                 bgra32_topdown, BGRA32_TOPDOWN_W, 2, 2);
    /* The two halves of the 32-bit alpha ambiguity, which are the same bytes at
     * pixel scope and opposite at image scope. */
    check(bgra32_topdown[2] == 0x000000FFu,
          "a zero alpha next to a non-zero one is real transparency");
    expect_image("bgra32_noalpha.bmp", "bmp 32-bit with no alpha channel is opaque",
                 bgra32_noalpha, BGRA32_NOALPHA_W, 2, 2);
    expect_image("pal8.bmp", "bmp 8-bit paletted",
                 pal8_bmp, PAL8_BMP_W, 4, 1);
    expect_err_as(CODEC_FMT_BMP, "trunc.bmp", "truncated bmp refused",
                  CODEC_ERR_ANY);
    expect_err_as(CODEC_FMT_BMP, "rle8.bmp",
                  "RLE bmp refused by name rather than rendered as noise",
                  CODEC_ERR_UNSUPPORTED);

    /* ── WAV ── */
    expect_audio("m16.wav", "wav 16-bit mono", m16, M16_W, 8000);
    expect_audio("s16.wav", "wav 16-bit stereo downmixed to mono",
                 s16, S16_W, 44100);
    expect_audio("m8.wav", "wav 8-bit unsigned", m8, M8_W, 8000);
    expect_audio("m24.wav", "wav 24-bit", m24, M24_W, 8000);
    expect_audio("f32.wav", "wav float32 with no float ABI",
                 f32, F32_W, 8000);
    expect_audio("padded.wav", "wav with an odd-sized chunk before data",
                 padded, PADDED_W, 8000);

    {
        char path[512];
        size_t n = 0;
        snprintf(path, sizeof(path), "%s/mp3.wav", dir);
        uint8_t *buf = slurp(path, &n);
        if (buf) {
            codec_audio_t a;
            memset(&a, 0, sizeof(a));
            check(codec_decode_auto_audio(buf, n, &a) == CODEC_ERR_UNSUPPORTED,
                  "wav claiming an mp3 payload is refused");
            codec_audio_free(&a);
            free(buf);
        } else {
            bad("mp3.wav readable");
        }
    }

    /* ── argument handling ── */
    {
        codec_image_t img;
        memset(&img, 0, sizeof(img));
        check(codec_decode_image(CODEC_FMT_PNG, NULL, 0, &img) == CODEC_ERR_ARG,
              "null input is a bad argument, not a crash");
        check(codec_decode_image(CODEC_FMT_PNG, (const uint8_t *)"x", 1, NULL)
                  == CODEC_ERR_ARG,
              "null output is a bad argument, not a crash");
    }

    printf("codec: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}