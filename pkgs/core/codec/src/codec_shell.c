/* `img` -- the kernel-side consumer of the codec package.
 *
 * Two jobs, both deliberately small:
 *
 *   img                  print the codec registry: what this build can open,
 *                         what it recognises but cannot decode, and why
 *   img show <path>      decode an image file and blit it to the framebuffer
 *   img probe <path>     identify a file without decoding it
 *
 * This file exists because of the gc-sections trap that bit lgame and jengine in
 * this tree: decoders with no caller are silently dropped from the linked image,
 * and "it compiles" then means nothing. `img show` is the live caller that keeps
 * the PNG/BMP/inflate path in the binary, and the marker line `img: decoded ...`
 * is what a boot test can assert on.
 *
 * ── The 64 KiB ceiling ─────────────────────────────────────────────────────
 *
 * fs_read() reads at most FS_CONTENT_MAX (65536) bytes, and sys_lseek clamps to
 * the same value, so any file on this system is truncated to 64 KiB before a
 * decoder sees it. That is the kernel's limit, not this package's, and it is
 * why the fixtures used for boot verification are tiny. A 640x480 PNG will not
 * load here no matter how correct the decoder is; the error message below says
 * so rather than reporting a decode failure.
 */

#include "codec.h"
#include "codec_str.h"      /* memset/strcmp: kernel string.h under -nostdinc */
#include "kprintf.h"
#include "fs.h"
#include "fb.h"
#include "mm.h"

#include "ext2.h"

/* FS_CONTENT_MAX is 65536, the same ceiling fs_read() enforces. Allocated once
 * rather than per call: this runs from a shell builtin on a kernel stack. */
static uint8_t g_img_buf[FS_CONTENT_MAX];

static void img_print_registry(void) {
    kprintf("codec registry (%d formats)\n", codec_registry_count);
    kprintf("  %-6s %-24s %-9s %s\n", "fmt", "mime", "state", "note");
    for (int i = 0; i < codec_registry_count; i++) {
        const codec_entry_t *e = &codec_registry[i];
        kprintf("  %-6s %-24s %-9s %s\n",
                e->name, e->mime,
                e->decode ? "decodes" : "recognised",
                e->note ? e->note : "");
    }
    kprintf("\nlimits: max %u x %u, max %u pixels\n",
            (unsigned)CODEC_MAX_DIM, (unsigned)CODEC_MAX_DIM,
            (unsigned)CODEC_MAX_PIXELS);
    kprintf("files are limited to %u bytes by the kernel fs layer\n",
            (unsigned)FS_CONTENT_MAX);
}

/* Distinguish "this file is longer than the fs layer will give us" from "this
 * file is broken". They look identical from the decoder -- both are short input
 * -- but only one is the decoder's fault, and telling the user so is the whole
 * point of having error codes. */
static int img_read(const char *path, size_t *out_len) {
    /* Try the in-memory table first, then ext2, which is the same order
     * sys_open() uses.
     *
     * fs_read() alone only sees what initramfs_populate() put in the node
     * table, so every image on the real mounted disk would read as "no such
     * file" -- the same gap that made els/ecat necessary alongside cat. */
    int n = fs_read(path, (char *)g_img_buf, FS_CONTENT_MAX);
    if (n < 0 && ext2_mounted())
        n = ext2_read_file_path(path, (char *)g_img_buf, FS_CONTENT_MAX);

    if (n < 0) {
        kprintf("img: %s: no such file\n", path);
        return 0;
    }
    if (n == 0) {
        kprintf("img: %s: empty\n", path);
        return 0;
    }
    *out_len = (size_t)n;
    return 1;
}

static void img_probe(const char *path) {
    size_t n = 0;
    if (!img_read(path, &n)) return;

    codec_fmt_t f = codec_probe(g_img_buf, n);
    if (f == CODEC_FMT_UNKNOWN) {
        kprintf("img: %s: no known format (first bytes:", path);
        for (size_t i = 0; i < 4 && i < n; i++)
            kprintf(" %02x", g_img_buf[i]);
        kprintf(")\n");
        return;
    }
    kprintf("img: %s: %s, %u bytes, %s\n", path, codec_fmt_name(f),
            (unsigned)n,
            codec_fmt_supported(f) ? "decodable" : "recognised, not decodable");
}

static int img_show(const char *path) {
    size_t n = 0;
    if (!img_read(path, &n)) return 0;

    codec_image_t img;
    memset(&img, 0, sizeof(img));

    codec_err_t e = codec_decode_auto(g_img_buf, n, &img);
    if (e != CODEC_OK) {
        kprintf("img: %s: %s\n", path, codec_strerror(e));
        if (e == CODEC_ERR_TRUNCATED && n == FS_CONTENT_MAX) {
            kprintf("     the file filled the %u byte limit, so it may simply "
                    "be too large\n", (unsigned)FS_CONTENT_MAX);
        }
        return 0;
    }

    if (!fb_available()) {
        kprintf("img: %s: no framebuffer (boot with -vga std, not -vga none)\n",
                path);
        codec_image_free(&img);
        return 0;
    }

    uint32_t fw = fb_getwidth();
    uint32_t fh = fb_getheight();

    kprintf("img: decoded %s %ux%u from %u bytes\n", codec_fmt_name(img.fmt),
            (unsigned)img.width, (unsigned)img.height, (unsigned)n);
    kprintf("img: blitting %ux%u to a %ux%u framebuffer\n",
            (unsigned)img.width, (unsigned)img.height,
            (unsigned)fw, (unsigned)fh);

    /* fb_putpixel, not a direct back-buffer write.
     *
     * A direct write would be faster, but fb_backbuffer_end() clears the back
     * buffer after blitting (documented in the LGame notes in AGENTS.md), so
     * anything drawn after a present lands in memory that is about to be
     * overwritten. fb_putpixel goes through the same clipping the rest of the
     * text console relies on. The cost is irrelevant here: FS_CONTENT_MAX caps
     * the input, so images reaching this path are small. */
    for (uint32_t y = 0; y < img.height && y < fh; y++) {
        for (uint32_t x = 0; x < img.width && x < fw; x++) {
            uint32_t px = img.argb[(size_t)y * img.width + x];
            uint32_t r = (px >> 16) & 0xFF;
            uint32_t g = (px >> 8) & 0xFF;
            uint32_t b = px & 0xFF;
            uint32_t a = (px >> 24) & 0xFF;

            /* The framebuffer is opaque, so alpha has to be resolved against
             * black rather than written out: a decoder that produced a real
             * alpha channel would otherwise composite against whatever
             * happened to be in memory. */
            if (a != 255)
                fb_putpixel(x, y, ((r * a) >> 8) << 16 |
                                    ((g * a) >> 8) << 8 |
                                    ((b * a) >> 8));
            else
                /* NEGATIVE CONTROL: transpose red and blue when writing to the
                 * framebuffer. The host suite cannot detect this because it
                 * never touches fb_putpixel. The boot check (img_check.py) must
                 * be able to assert the exact (r,g,b) that land on screen,
                 * which is exactly what its "decoded colours survive the blit"
                 * step does. Do not "fix" this by accident. */
                #ifdef CODEC_FB_SWAP_RB
                fb_putpixel(x, y, FB_RGB(b, g, r));
#else
fb_putpixel(x, y, FB_RGB(r, g, b));
#endif
        }
    }

    /* Probe pixels are read back out of the framebuffer after the blit and
     * printed. Not decoration, and not debug leftover -- it is what makes this
     * checkable from a boot log at all.
     *
     * The alternative is to screendump on the host and read pixels there, but
     * the text console repaints over the top-left corner where the image lands,
     * so by the time a screendump arrives the prompt has overwritten part of
     * it. The kernel is the only place that can read the buffer before the
     * repaint, and reading it back separates "the writes never landed" from
     * "they landed and were then overwritten" -- two different bugs that look
     * identical in a host screenshot.
     *
     * Printed as x,y=r,g,b triples in the framebuffer's own 0x00RRGGBB packing, so
     * a host check compares them against the fixture's expected colours exactly.
     *
     * The candidate set is the four corners plus the centre, deduplicated -- the
     * boot fixture is 2x2, where "centre" and "last row" are the same pixel, and
     * a probe line that printed the same coordinate three times would look like
     * coverage it does not have. Only points inside both the image and the
     * framebuffer are emitted, so a fixture smaller than the screen cannot make
     * the check read off the edge into unrelated pixels. */
    {
        uint32_t px[5], py[5];
        int np = 0;
        uint32_t cand_x[5] = { 0, img.width ? img.width - 1 : 0,
                               0, img.width ? img.width - 1 : 0,
                               img.width / 2 };
        uint32_t cand_y[5] = { 0, 0,
                               img.height ? img.height - 1 : 0,
                               img.height ? img.height - 1 : 0,
                               img.height / 2 };

        for (int i = 0; i < 5; i++) {
            if (cand_x[i] >= img.width || cand_y[i] >= img.height) continue;
            if (cand_x[i] >= fw || cand_y[i] >= fh) continue;
            int dup = 0;
            for (int j = 0; j < np; j++)
                if (px[j] == cand_x[i] && py[j] == cand_y[i]) { dup = 1; break; }
            if (dup) continue;
            px[np] = cand_x[i];
            py[np] = cand_y[i];
            np++;
        }

        kprintf("img: probe");
        for (int i = 0; i < np; i++) {
            uint32_t v = fb_getpixel(px[i], py[i]);
            kprintf(" %u,%u=%u,%u,%u", (unsigned)px[i], (unsigned)py[i],
                    (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
        }
        kprintf("\n");
    }

    codec_image_free(&img);
    return 1;
}

void cmd_img(int argc, char **argv) {
    if (argc < 2) {
        img_print_registry();
        return;
    }
    if (strcmp(argv[1], "probe") == 0 && argc >= 3) { img_probe(argv[2]); return; }
    if (strcmp(argv[1], "show") == 0 && argc >= 3)  { img_show(argv[2]);  return; }

    kprintf("usage: img                    list supported formats\n");
    kprintf("       img probe <path>       identify a file\n");
    kprintf("       img show <path>        decode and blit to the framebuffer\n");
}