extern "C" {
#include "wallpaper.h"
#include "kprintf.h"
#include "fb.h"
#include "mm.h"
#include "string.h"
}

/* Extra color for tri-gradient wallpapers */
static const uint32_t aurora_c3 = 0xFF002a1a;
static const uint32_t tahoe_c3  = 0xFF0a3a4a;

/* ── Built-in presets ── */
wallpaper_t wallpaper_default = { WALLPAPER_SCENE, 0xFF0B1026, 0xFF2A1A4A, 0, 0 };
wallpaper_t wallpaper_dark_gradient = { WALLPAPER_GRADIENT_V, 0xFF060610, 0xFF1c1c3a, 0, 0 };
wallpaper_t wallpaper_light = { WALLPAPER_SOLID, 0xFFE8E8ED, 0xFFE8E8ED, 0, 0 };
wallpaper_t wallpaper_mountains = { WALLPAPER_SCENE, 0xFF0a1628, 0xFF1a3048, 0, 0 };
wallpaper_t wallpaper_thormium = { WALLPAPER_CHROMEOS, 0xFF212121, 0xFF111111, 0, 0 };
wallpaper_t wallpaper_macos_big_sur = { WALLPAPER_SCENE, 0xFF0A0E24, 0xFF3A1A50, 0, 0 };
wallpaper_t wallpaper_macos27 = { WALLPAPER_SCENE, 0xFF080818, 0xFF2A1860, 0, 0 };
wallpaper_t wallpaper_vibrant = { WALLPAPER_SCENE, 0xFF1a0a2a, 0xFF0a1a3a, 0, 0 };
wallpaper_t wallpaper_aurora = { WALLPAPER_GRADIENT_V3, 0xFF0a0a1a, 0xFF4a1a6a, (const char *)&aurora_c3, 4 };
wallpaper_t wallpaper_tahoe = { WALLPAPER_SCENE, 0xFF061018, 0xFF1A3060, (const char *)&tahoe_c3, 4 };

static wallpaper_t *workspaces[WALLPAPER_MAX_WORKSPACES];

/* Cached full-screen wallpaper bitmap */
static uint32_t *wp_cache;
static int wp_cache_w, wp_cache_h, wp_cache_ws;
static int wp_cache_dirty = 1;

static uint32_t lerp_color(uint32_t c1, uint32_t c2, int t, int max) {
    if (max <= 0) return c1;
    if (t < 0) t = 0;
    if (t > max) t = max;
    int r = ((int)((c1 >> 16) & 0xFF) * (max - t) + (int)((c2 >> 16) & 0xFF) * t) / max;
    int g = ((int)((c1 >> 8) & 0xFF) * (max - t) + (int)((c2 >> 8) & 0xFF) * t) / max;
    int b = ((int)(c1 & 0xFF) * (max - t) + (int)(c2 & 0xFF) * t) / max;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t lerp3_color(uint32_t c1, uint32_t c2, uint32_t c3, int t, int max) {
    int half = max / 2;
    if (half <= 0) return c1;
    if (t < half)
        return lerp_color(c1, c2, t, half);
    return lerp_color(c2, c3, t - half, max - half);
}

static uint32_t blend_over(uint32_t src, uint32_t dst, uint8_t a) {
    if (a == 0) return dst;
    if (a == 255) return (src & 0x00FFFFFF) | 0xFF000000;
    int inv = 255 - a;
    int sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    int r = (sr * a + dr * inv + 127) / 255;
    int g = (sg * a + dg * inv + 127) / 255;
    int b = (sb * a + db * inv + 127) / 255;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Fast integer square root (Babylonian method, 8 iterations) */
static int isqrt(int n) {
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

/* Soft radial orb — smooth Gaussian-like falloff by linear distance */
static void paint_orb(uint32_t *buf, int stride, int w, int h,
                      int cx, int cy, int radius, uint32_t color, uint8_t max_a) {
    if (radius <= 0) return;
    int r2 = radius * radius;
    int y0 = cy - radius; if (y0 < 0) y0 = 0;
    int y1 = cy + radius; if (y1 > h) y1 = h;
    int x0 = cx - radius; if (x0 < 0) x0 = 0;
    int x1 = cx + radius; if (x1 > w) x1 = w;
    for (int y = y0; y < y1; y++) {
        int dy = y - cy;
        int dy2 = dy * dy;
        uint32_t *line = buf + y * stride;
        for (int x = x0; x < x1; x++) {
            int dx = x - cx;
            int d2 = dx * dx + dy2;
            if (d2 >= r2) continue;
            /* Proper linear falloff: a = max_a * (1 - d/r)^2 */
            int d = isqrt(d2);
            int fall = radius - d;
            fall = (fall * 1000) / radius;
            fall = fall * fall / 1000;
            uint8_t a = (uint8_t)((int)max_a * fall / 1000);
            if (a < 2) continue;
            line[x] = blend_over(color, line[x], a);
        }
    }
}

static void render_scene(uint32_t *buf, int stride, int w, int h, wallpaper_t *wp) {
    uint32_t c1 = wp->color1;
    uint32_t c2 = wp->color2;
    uint32_t c3 = (wp->svg_len >= 4 && wp->svg_src)
                    ? *(const uint32_t *)wp->svg_src
                    : lerp_color(c1, c2, 1, 2);

    /* Base: multi-stop diagonal + vertical blend */
    int maxd = w + h;
    for (int row = 0; row < h; row++) {
        uint32_t *line = buf + row * stride;
        uint32_t vcol = lerp3_color(c1, c3, c2, row, h > 1 ? h - 1 : 1);
        for (int col = 0; col < w; col++) {
            int t = col + row;
            if (t > maxd) t = maxd;
            uint32_t dcol = lerp_color(c1, c2, t, maxd > 0 ? maxd : 1);
            line[col] = lerp_color(vcol, dcol, 45, 100);
        }
    }

    /* Soft color orbs (Big Sur depth) */
    paint_orb(buf, stride, w, h, w * 22 / 100, h * 28 / 100, w * 28 / 100, 0xFF5B3FA8, 70);
    paint_orb(buf, stride, w, h, w * 72 / 100, h * 22 / 100, w * 32 / 100, 0xFF0A84FF, 55);
    paint_orb(buf, stride, w, h, w * 55 / 100, h * 68 / 100, w * 36 / 100, 0xFFBF5AF2, 45);
    paint_orb(buf, stride, w, h, w * 18 / 100, h * 75 / 100, w * 22 / 100, 0xFF30D158, 28);
    paint_orb(buf, stride, w, h, w * 85 / 100, h * 80 / 100, w * 18 / 100, 0xFF64D2FF, 35);

    /* Soft horizon glow */
    int hy = h * 62 / 100;
    for (int row = hy; row < h; row++) {
        int t = row - hy;
        int max = h - hy;
        if (max <= 0) break;
        uint8_t a = (uint8_t)(18 * t / max);
        uint32_t *line = buf + row * stride;
        for (int col = 0; col < w; col++)
            line[col] = blend_over(0xFF1A3060, line[col], a);
    }

    /* Vignette — smooth dark tint blend */
    int cx = w / 2, cy = h / 2;
    int maxr2 = cx * cx + cy * cy;
    if (maxr2 < 1) maxr2 = 1;
    for (int row = 0; row < h; row++) {
        int dy = row - cy;
        uint32_t *line = buf + row * stride;
        for (int col = 0; col < w; col++) {
            int dx = col - cx;
            long long d2 = (long long)dx * dx + (long long)dy * dy;
            int t = (int)(d2 * 1000 / maxr2);
            if (t < 300) continue;
            int fall = t - 300;
            if (fall > 700) fall = 700;
            uint8_t a = (uint8_t)(fall * 110 / 700);
            line[col] = blend_over(0xFF000000, line[col], a);
        }
    }

    /* Subtle top highlight band */
    for (int row = 0; row < h / 8 && row < h; row++) {
        uint8_t a = (uint8_t)(12 * (h / 8 - row) / (h / 8 > 0 ? h / 8 : 1));
        uint32_t *line = buf + row * stride;
        for (int col = 0; col < w; col++)
            line[col] = blend_over(0xFFFFFFFF, line[col], a);
    }
}

static void render_simple(uint32_t *buf, int stride, int w, int h, wallpaper_t *wp) {
    switch (wp->type) {
        case WALLPAPER_SOLID: {
            for (int row = 0; row < h; row++) {
                uint32_t *line = buf + row * stride;
                for (int col = 0; col < w; col++)
                    line[col] = wp->color1;
            }
            break;
        }
        case WALLPAPER_GRADIENT_V:
        case WALLPAPER_CHROMEOS:
        default: {
            for (int row = 0; row < h; row++) {
                uint32_t c = lerp_color(wp->color1, wp->color2, row, h > 1 ? h - 1 : 1);
                uint32_t *line = buf + row * stride;
                for (int col = 0; col < w; col++)
                    line[col] = c;
            }
            break;
        }
        case WALLPAPER_GRADIENT_V3: {
            uint32_t c3 = (wp->svg_len && wp->svg_src) ? *(const uint32_t *)wp->svg_src : 0xFF000000;
            for (int row = 0; row < h; row++) {
                uint32_t c = lerp3_color(wp->color1, wp->color2, c3, row, h > 1 ? h - 1 : 1);
                uint32_t *line = buf + row * stride;
                for (int col = 0; col < w; col++)
                    line[col] = c;
            }
            break;
        }
        case WALLPAPER_GRADIENT_H: {
            for (int col = 0; col < w; col++) {
                uint32_t c = lerp_color(wp->color1, wp->color2, col, w > 1 ? w - 1 : 1);
                for (int row = 0; row < h; row++)
                    buf[row * stride + col] = c;
            }
            break;
        }
        case WALLPAPER_GRADIENT_DIAG: {
            int max = w > h ? w : h;
            for (int row = 0; row < h; row++) {
                uint32_t *line = buf + row * stride;
                int diag = (row * max / (h > 0 ? h : 1)) + (w * 2 / 5);
                if (diag > max) diag = max;
                for (int col = 0; col < w; col++) {
                    int t = col + diag;
                    if (t > max) t = max;
                    line[col] = lerp_color(wp->color1, wp->color2, t, max > 0 ? max : 1);
                }
            }
            break;
        }
        case WALLPAPER_SCENE:
            render_scene(buf, stride, w, h, wp);
            break;
    }
}

void wallpaper_init(void) {
    for (int i = 0; i < WALLPAPER_MAX_WORKSPACES; i++)
        workspaces[i] = &wallpaper_default;
    wp_cache = 0;
    wp_cache_w = wp_cache_h = wp_cache_ws = 0;
    wp_cache_dirty = 1;
}

void wallpaper_invalidate(void) {
    wp_cache_dirty = 1;
}

void wallpaper_set(int workspace, wallpaper_t *wp) {
    if (workspace < 0 || workspace >= WALLPAPER_MAX_WORKSPACES) return;
    workspaces[workspace] = wp ? wp : &wallpaper_default;
    wp_cache_dirty = 1;
}

wallpaper_t *wallpaper_get(int workspace) {
    if (workspace < 0 || workspace >= WALLPAPER_MAX_WORKSPACES)
        return &wallpaper_default;
    return workspaces[workspace];
}

static void ensure_cache(int workspace, int w, int h) {
    if (w <= 0 || h <= 0) return;
    wallpaper_t *wp = wallpaper_get(workspace);
    if (!wp_cache || wp_cache_w != w || wp_cache_h != h) {
        if (wp_cache) free(wp_cache);
        size_t bytes = (size_t)w * (size_t)h * sizeof(uint32_t);
        wp_cache = (uint32_t *)malloc(bytes);
        wp_cache_w = w;
        wp_cache_h = h;
        wp_cache_dirty = 1;
    }
    if (!wp_cache) return;
    if (wp_cache_dirty || wp_cache_ws != workspace) {
        render_simple(wp_cache, w, w, h, wp);
        wp_cache_ws = workspace;
        wp_cache_dirty = 0;
    }
}

void wallpaper_draw(int workspace, uint32_t *buf, int stride, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    if (!buf) {
        buf = fb_get_active_buffer();
        stride = fb_get_pitch() / 4;
    }
    if (!buf) return;

    ensure_cache(workspace, w, h);
    if (wp_cache && wp_cache_w == w && wp_cache_h == h) {
        for (int row = 0; row < h; row++) {
            uint32_t *dst = buf + (y + row) * stride + x;
            uint32_t *src = wp_cache + row * w;
            memcpy(dst, src, (size_t)w * sizeof(uint32_t));
        }
        return;
    }

    /* Fallback: direct render into destination (no cache) */
    wallpaper_t *wp = wallpaper_get(workspace);
    if (wp->type == WALLPAPER_SCENE) {
        /* render into temp rows if needed — direct path for non-scene only */
    }
    /* Simple path: render line-by-line into dest for non-cached */
    if (wp->type == WALLPAPER_SOLID) {
        for (int row = 0; row < h; row++) {
            uint32_t *line = buf + (y + row) * stride + x;
            for (int col = 0; col < w; col++) line[col] = wp->color1;
        }
        return;
    }
    for (int row = 0; row < h; row++) {
        uint32_t c = lerp_color(wp->color1, wp->color2, row, h > 1 ? h - 1 : 1);
        uint32_t *line = buf + (y + row) * stride + x;
        for (int col = 0; col < w; col++) line[col] = c;
    }
}
