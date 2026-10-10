/* Xcursor theme loader -- see cursor_theme.h for the format notes.
 *
 * Kept out of fb.cpp so the framebuffer driver stays about drawing. This file
 * owns the file-format knowledge and the decoded surfaces; fb.cpp asks it for
 * the surface of the current style and blends that over the framebuffer.
 */
extern "C" {
#include "fb.h"
#include "cursor_theme.h"
#include "../kernel/ext2.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
}

/* The eight styles are the ones fb.h already exposes, in the same order. The
 * mapping to theme file names follows the freedesktop cursor-name convention,
 * using the regular (non-symlink) members of the Bibata Modern Classic set. */
static const char *const k_files[CURSOR_THEME_STYLES] = {
    "left_ptr",           /* arrow      */
    "hand2",              /* hand       */
    "crosshair",          /* crosshair  */
    "xterm",              /* text       */
    "sb_h_double_arrow",  /* resize_h   */
    "sb_v_double_arrow",  /* resize_v   */
    "bd_double_arrow",    /* resize_nwse */
    "fd_double_arrow",    /* resize_nesw */
};

static const char *const k_names[CURSOR_THEME_STYLES] = {
    "arrow", "hand", "crosshair", "text",
    "resize_h", "resize_v", "resize_nwse", "resize_nesw",
};

static_assert(CURSOR_THEME_STYLES == CURSOR_STYLE_COUNT,
              "cursor theme table must cover exactly the fb cursor styles");

#define XCUR_MAGIC 0x72756358u  /* "Xcur", little-endian */
#define XCUR_IMAGE 0xfffd0002u
#define XCUR_HDR   36u

/* Per-style decoded surfaces. Sized for the largest cursor we will load, so the
 * loader never allocates: a cursor must be available before the heap is
 * necessarily healthy, and a failed allocation here would mean no pointer. */
static cursor_theme_image_t g_img[CURSOR_THEME_STYLES];
static uint32_t             g_pool[CURSOR_THEME_STYLES][CURSOR_THEME_MAX_DIM * CURSOR_THEME_MAX_DIM];
static int                  g_target = 24;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t absdiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

/* FNV-1a over the raw pixel bytes exactly as they sit in the file. The boot
 * check recomputes this from the same file, so a loader that picks the wrong
 * chunk, the wrong size or the wrong hotspot cannot agree with it. */
static uint32_t fnv1a(const uint8_t *p, unsigned n) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static int load_one(const char *dir, int style) {
    char path[192];
    snprintf(path, sizeof path, "%s/%s", dir, k_files[style]);

    uint8_t hdr[4096];
    int n = ext2_read_file_path(path, hdr, (int)sizeof hdr);
    if (n < 16 || rd32(hdr) != XCUR_MAGIC) {
        kprintf("cursor: %s %s missing\n", k_names[style], k_files[style]);
        return 0;
    }

    uint32_t hsize = rd32(hdr + 4);
    uint32_t ntoc  = rd32(hdr + 12);
    if (hsize < 16 || ntoc == 0 || ntoc > 256 ||
        (uint32_t)n < hsize + ntoc * 12u) {
        kprintf("cursor: %s %s bad header\n", k_names[style], k_files[style]);
        return 0;
    }

    /* Choose the image chunk nearest the target. Ties prefer the larger size,
     * which is the safer failure direction (a touch too big, never too small). */
    int      have = 0;
    uint32_t bestsub = 0, bestpos = 0;
    for (uint32_t i = 0; i < ntoc; i++) {
        const uint8_t *e = hdr + hsize + i * 12u;
        if (rd32(e) != XCUR_IMAGE) continue;
        uint32_t sub = rd32(e + 4);
        if (!have || absdiff(sub, (uint32_t)g_target) < absdiff(bestsub, (uint32_t)g_target) ||
            (absdiff(sub, (uint32_t)g_target) == absdiff(bestsub, (uint32_t)g_target) && sub > bestsub)) {
            have = 1;
            bestsub = sub;
            bestpos = rd32(e + 8);
        }
    }
    if (!have) {
        kprintf("cursor: %s %s no image chunks\n", k_names[style], k_files[style]);
        return 0;
    }

    uint8_t ch[XCUR_HDR];
    if (ext2_read_file_at(path, (int)bestpos, ch, (int)sizeof ch) < (int)sizeof ch)
        { kprintf("cursor: %s %s truncated chunk\n", k_names[style], k_files[style]); return 0; }
    if (rd32(ch) < XCUR_HDR || rd32(ch + 4) != XCUR_IMAGE)
        { kprintf("cursor: %s %s bad chunk\n", k_names[style], k_files[style]); return 0; }

    uint32_t w  = rd32(ch + 16);
    uint32_t h  = rd32(ch + 20);
    uint32_t xh = rd32(ch + 24);
    uint32_t yh = rd32(ch + 28);
    if (w == 0 || h == 0 || w > CURSOR_THEME_MAX_DIM || h > CURSOR_THEME_MAX_DIM)
        { kprintf("cursor: %s %s unsupported %ux%u\n", k_names[style], k_files[style],
                  (unsigned)w, (unsigned)h); return 0; }

    unsigned bytes = w * h * 4u;
    if (ext2_read_file_at(path, (int)bestpos + (int)XCUR_HDR, g_pool[style], (int)bytes) < (int)bytes)
        { kprintf("cursor: %s %s short pixel read\n", k_names[style], k_files[style]); return 0; }

    g_img[style].width  = (int)w;
    g_img[style].height = (int)h;
    g_img[style].hotx   = (int)xh;
    g_img[style].hoty   = (int)yh;
    g_img[style].argb   = g_pool[style];

    kprintf("cursor: %s %s %ux%u hot=%u,%u crc=%08x\n",
            k_names[style], k_files[style], (unsigned)w, (unsigned)h,
            (unsigned)xh, (unsigned)yh,
            (unsigned)fnv1a((const uint8_t *)g_pool[style], bytes));
    return 1;
}

int cursor_theme_load(const char *dir) {
    cursor_theme_unload();
    if (!dir || !dir[0] || !ext2_mounted()) {
        kprintf("cursor: no theme filesystem, using built-in glyphs\n");
        return 0;
    }

    int loaded = 0;
    for (int s = 0; s < CURSOR_THEME_STYLES; s++)
        loaded += load_one(dir, s);

    kprintf("cursor: loaded %d/%d styles from %s\n", loaded, CURSOR_THEME_STYLES, dir);
    return loaded;
}

void cursor_theme_unload(void) {
    for (int s = 0; s < CURSOR_THEME_STYLES; s++) {
        g_img[s].width = g_img[s].height = 0;
        g_img[s].hotx = g_img[s].hoty = 0;
        g_img[s].argb = 0;
    }
}

const cursor_theme_image_t *cursor_theme_get(int style) {
    if (style < 0 || style >= CURSOR_THEME_STYLES) return 0;
    return g_img[style].argb ? &g_img[style] : 0;
}

int cursor_theme_loaded_count(void) {
    int c = 0;
    for (int s = 0; s < CURSOR_THEME_STYLES; s++) if (g_img[s].argb) c++;
    return c;
}

int cursor_theme_any_loaded(void) { return cursor_theme_loaded_count() > 0; }

void cursor_theme_set_target(int px) {
    if (px < 8)   px = 8;
    if (px > CURSOR_THEME_MAX_DIM) px = CURSOR_THEME_MAX_DIM;
    g_target = px;
}

int cursor_theme_get_target(void) { return g_target; }

const char *cursor_theme_style_name(int style) {
    return (style >= 0 && style < CURSOR_THEME_STYLES) ? k_names[style] : "?";
}

const char *cursor_theme_style_file(int style) {
    return (style >= 0 && style < CURSOR_THEME_STYLES) ? k_files[style] : "?";
}
