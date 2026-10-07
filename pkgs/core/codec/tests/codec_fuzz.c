/* Fuzz driver for the codec decoders.
 *
 * Feeds every file in a directory to codec_decode_auto() (image) and
 * codec_decode_auto_audio() (audio) and requires only that the process
 * survives and does not report a sanitizer error. No particular result is
 * expected from mutated input -- a decoder is free to reject almost all of it.
 * The check is that rejection happens without reading out of bounds, writing
 * out of bounds, overflowing, or dividing by zero.
 *
 * Built with -fsanitize=address,undefined by tests/fuzz.sh; a plain build
 * cannot detect any of that.
 *
 * Usage: codec_fuzz <dir>  (runs <dir> itself, then <dir>/fuzz if present)
 */

#define CODEC_HOST_TEST 1

#include "codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static int files, images_ok, images_refused, audios;

/* Fixed-capacity name table rather than strdup: -std=c99 hides strdup behind a
 * POSIX feature-test macro, and a bounded table suits a fuzz driver better
 * anyway -- it cannot fail on allocation and needs no cleanup logic. */
#define MAX_NAMES 16384
#define NAME_MAX_LEN 255

static char name_table[MAX_NAMES][NAME_MAX_LEN];

static void quiet(const char *msg) { (void)msg; }

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
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

static int cmp(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void run_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "codec fuzz: cannot open %s\n", dir);
        return;
    }

    /* Collected and sorted so that a crash is reproducible from the printed
     * order: readdir returns entries in filesystem order, which changes
     * between machines and between runs on some filesystems. */
    char *names[MAX_NAMES];
    int count = 0;
    struct dirent *de;
    while (count < MAX_NAMES && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;   /* skip . .. and dotfiles */
        /* Split: writing name_table[count] and reading it in the same
         * expression would make count++'s evaluation order undefined. */
        strncpy(name_table[count], de->d_name, NAME_MAX_LEN - 1);
        name_table[count][NAME_MAX_LEN - 1] = 0;
        names[count] = name_table[count];
        count++;
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof(names[0]), cmp);

    for (int i = 0; i < count; i++) {
        size_t n = 0;
        char full[1200];
        snprintf(full, sizeof(full), "%s/%s", dir, names[i]);
        uint8_t *buf = slurp(full, &n);
        if (!buf) continue;
        files++;

        /* Both entry points, regardless of what the content claims to be. The
         * audio path is given image bytes on purpose: a decoder that trusts
         * length fields without bounding them is where those bugs live. */
        codec_image_t img;
        memset(&img, 0, sizeof(img));
        codec_err_t e = codec_decode_auto(buf, n, &img);
        if (e == CODEC_OK) {
            images_ok++;
            /* Touch the whole buffer so ASAN validates every decoded byte. */
            volatile uint32_t acc = 0;
            for (size_t p = 0; p < (size_t)img.width * img.height; p++)
                acc += img.argb[p];
            (void)acc;
        } else {
            images_refused++;
        }
        codec_image_free(&img);

        codec_audio_t aud;
        memset(&aud, 0, sizeof(aud));
        if (codec_decode_auto_audio(buf, n, &aud) == CODEC_OK) {
            audios++;
            volatile int32_t acc = 0;
            for (uint32_t p = 0; p < aud.frames; p++) acc += aud.samples[p];
            (void)acc;
        }
        codec_audio_free(&aud);

        free(buf);
    }
}

int main(int argc, char **argv) {
    codec_set_log(quiet);   /* expected refusals are not diagnostics worth printing */
    if (argc < 2) {
        fprintf(stderr, "usage: codec_fuzz <dir> [dir...]\n");
        return 2;
    }
    for (int i = 1; i < argc; i++) run_dir(argv[i]);

    printf("codec fuzz: %d inputs, %d decoded as image, %d refused, %d as audio\n",
           files, images_ok, images_refused, audios);
    if (files == 0) {
        fprintf(stderr, "codec fuzz: no inputs -- the run proved nothing\n");
        return 1;
    }
    return 0;
}