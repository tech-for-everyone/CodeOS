/* CodeOS Desktop - Renderer Implementation
 * Software renderer with modern effects */

#include "codeos_renderer.h"
#include <stdint.h>
#include <string.h>

/* Renderer state */
static uint32_t *fb = 0;
static int fb_width = 0;
static int fb_height = 0;
static int fb_stride = 0;
static uint8_t current_alpha = 255;
static int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
static int clip_enabled = 0;

void renderer_init(uint32_t *framebuffer, int width, int height, int stride) {
    fb = framebuffer;
    fb_width = width;
    fb_height = height;
    fb_stride = stride;
    current_alpha = 255;
    clip_enabled = 0;
}

/* ═══════════════════════════════════════════════════════════════════
   Basic Drawing
   ═══════════════════════════════════════════════════════════════════ */

static inline int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint32_t blend_pixel(uint32_t dst, uint32_t src, uint8_t alpha) {
    uint8_t sa = (src >> 24) & 0xFF;
    uint8_t a = (uint16_t)sa * alpha / 255;
    if (a == 0) return dst;
    if (a == 255) return src;

    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;

    uint8_t r = dr + ((sr - dr) * a / 255);
    uint8_t g = dg + ((sg - dg) * a / 255);
    uint8_t b = db + ((sb - db) * a / 255);

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static inline void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) return;
    if (clip_enabled && (x < clip_x || x >= clip_x + clip_w || y < clip_y || y >= clip_y + clip_h)) return;
    uint32_t *dst = fb + y * fb_stride + x;
    *dst = blend_pixel(*dst, color, current_alpha);
}

void renderer_clear(uint32_t color) {
    for (int y = 0; y < fb_height; y++) {
        uint32_t *row = fb + y * fb_stride;
        for (int x = 0; x < fb_width; x++) {
            row[x] = color;
        }
    }
}

void renderer_pixel(int x, int y, uint32_t color) {
    put_pixel(x, y, color);
}

void renderer_hline(int x, int y, int w, uint32_t color) {
    for (int i = 0; i < w; i++) {
        put_pixel(x + i, y, color);
    }
}

void renderer_vline(int x, int y, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        put_pixel(x, y + i, color);
    }
}

void renderer_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            put_pixel(x + i, y + j, color);
        }
    }
}

void renderer_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness) {
    for (int t = 0; t < thickness; t++) {
        renderer_hline(x + t, y + t, w - 2 * t, color);
        renderer_hline(x + t, y + h - 1 - t, w - 2 * t, color);
        renderer_vline(x + t, y + t, h - 2 * t, color);
        renderer_vline(x + w - 1 - t, y + t, h - 2 * t, color);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Rounded Rectangle
   ═══════════════════════════════════════════════════════════════════ */

void renderer_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    /* Fill middle section */
    renderer_rect(x + radius, y, w - 2 * radius, h, color);

    /* Fill sides */
    renderer_rect(x, y + radius, radius, h - 2 * radius, color);
    renderer_rect(x + w - radius, y + radius, radius, h - 2 * radius, color);

    /* Fill corners using circle approximation */
    for (int cy = 0; cy < radius; cy++) {
        for (int cx = 0; cx < radius; cx++) {
            int dx = cx - radius + 1;
            int dy = cy - radius + 1;
            if (dx * dx + dy * dy <= radius * radius) {
                /* Top-left */
                put_pixel(x + cx, y + cy, color);
                /* Top-right */
                put_pixel(x + w - 1 - cx, y + cy, color);
                /* Bottom-left */
                put_pixel(x + cx, y + h - 1 - cy, color);
                /* Bottom-right */
                put_pixel(x + w - 1 - cx, y + h - 1 - cy, color);
            }
        }
    }
}

void renderer_rounded_rect_outline(int x, int y, int w, int h, int radius, uint32_t color, int thickness) {
    /* Draw outline by drawing two rounded rects and XORing */
    renderer_rounded_rect(x, y, w, h, radius, color);
    renderer_rounded_rect(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness, radius - thickness, 0x00000000);
}

/* ═══════════════════════════════════════════════════════════════════
   Gradients
   ═══════════════════════════════════════════════════════════════════ */

void renderer_gradient_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i = 0; i < w; i++) {
        int t = (i * 255) / (w > 0 ? w : 1);
        uint8_t r = ((c1 >> 16) & 0xFF) + (((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t / 255;
        uint8_t g = ((c1 >> 8) & 0xFF) + (((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t / 255;
        uint8_t b = (c1 & 0xFF) + ((c2 & 0xFF) - (c1 & 0xFF)) * t / 255;
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        renderer_vline(x + i, y, h, color);
    }
}

void renderer_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int j = 0; j < h; j++) {
        int t = (j * 255) / (h > 0 ? h : 1);
        uint8_t r = ((c1 >> 16) & 0xFF) + (((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t / 255;
        uint8_t g = ((c1 >> 8) & 0xFF) + (((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t / 255;
        uint8_t b = (c1 & 0xFF) + ((c2 & 0xFF) - (c1 & 0xFF)) * t / 255;
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        renderer_hline(x, y + j, w, color);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Circle and Ellipse
   ═══════════════════════════════════════════════════════════════════ */

void renderer_circle(int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void renderer_circle_filled(int cx, int cy, int r, uint32_t color) {
    renderer_circle(cx, cy, r, color);
}

void renderer_ellipse(int cx, int cy, int rx, int ry, uint32_t color) {
    for (int y = -ry; y <= ry; y++) {
        for (int x = -rx; x <= rx; x++) {
            if ((x * x) / (rx * rx) + (y * y) / (ry * ry) <= 1) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Text Rendering (using built-in 8x16 font)
   ═══════════════════════════════════════════════════════════════════ */

extern const uint8_t codeos_font8x16[256][16];

void renderer_text(int x, int y, const char *text, uint32_t color, int scale) {
    if (!text) return;
    int cursor_x = x;

    while (*text) {
        unsigned char c = (unsigned char)*text;
        const uint8_t *glyph = codeos_font8x16[c];

        for (int gy = 0; gy < 16; gy++) {
            uint8_t row = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                if (row & (0x80 >> gx)) {
                    if (scale == 1) {
                        put_pixel(cursor_x + gx, y + gy, color);
                    } else {
                        for (int sy = 0; sy < scale; sy++) {
                            for (int sx = 0; sx < scale; sx++) {
                                put_pixel(cursor_x + gx * scale + sx, y + gy * scale + sy, color);
                            }
                        }
                    }
                }
            }
        }

        cursor_x += 8 * scale;
        text++;
    }
}

void renderer_text_centered(int x, int y, int w, const char *text, uint32_t color, int scale) {
    int text_w = renderer_text_width(text, scale);
    renderer_text(x + (w - text_w) / 2, y, text, color, scale);
}

int renderer_text_width(const char *text, int scale) {
    if (!text) return 0;
    int len = 0;
    while (*text) { len++; text++; }
    return len * 8 * scale;
}

/* ═══════════════════════════════════════════════════════════════════
   Shadow Effect
   ═══════════════════════════════════════════════════════════════════ */

void renderer_shadow(int x, int y, int w, int h, int radius, int blur, uint32_t color) {
    /* Simple shadow: draw semi-transparent rectangles with decreasing opacity */
    uint8_t base_alpha = (color >> 24) & 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    for (int i = blur; i > 0; i--) {
        int spread = radius + i;
        int a = base_alpha * i / blur / 2;
        uint32_t shadow_color = (a << 24) | (r << 16) | (g << 8) | b;
        renderer_rounded_rect(x - spread, y - spread, w + 2 * spread, h + 2 * spread, radius + i, shadow_color);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Blur Effects (simplified)
   ═══════════════════════════════════════════════════════════════════ */

void renderer_blur_region(int x, int y, int w, int h, int radius) {
    /* Simplified blur: just darken the region */
    (void)radius;
    for (int j = y; j < y + h && j < fb_height; j++) {
        for (int i = x; i < x + w && i < fb_width; i++) {
            if (i < 0 || j < 0) continue;
            uint32_t *pixel = fb + j * fb_stride + i;
            uint8_t r = (((*pixel >> 16) & 0xFF) * 3) / 4;
            uint8_t g = (((*pixel >> 8) & 0xFF) * 3) / 4;
            uint8_t b = ((*pixel & 0xFF) * 3) / 4;
            *pixel = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Alpha and Clipping
   ═══════════════════════════════════════════════════════════════════ */

void renderer_set_alpha(uint8_t alpha) { current_alpha = alpha; }
uint8_t renderer_get_alpha(void) { return current_alpha; }

void renderer_set_clip(int x, int y, int w, int h) {
    clip_x = x; clip_y = y; clip_w = w; clip_h = h;
    clip_enabled = 1;
}

void renderer_clear_clip(void) { clip_enabled = 0; }

void renderer_begin(void) { /* No-op for software renderer */ }
void renderer_end(void) { /* No-op for software renderer */ }

/* ═══════════════════════════════════════════════════════════════════
   Icon and Image Drawing
   ═══════════════════════════════════════════════════════════════════ */

void renderer_icon(int x, int y, int size, const uint32_t *icon_data, int icon_w, int icon_h) {
    if (!icon_data) return;
    for (int j = 0; j < size && j < icon_h; j++) {
        for (int i = 0; i < size && i < icon_w; i++) {
            int src_x = i * icon_w / size;
            int src_y = j * icon_h / size;
            uint32_t color = icon_data[src_y * icon_w + src_x];
            if (color & 0xFF000000) {
                put_pixel(x + i, y + j, color);
            }
        }
    }
}

void renderer_image(int x, int y, int w, int h, const uint32_t *data) {
    if (!data) return;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            put_pixel(x + i, y + j, data[j * w + i]);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Animation Helpers
   ═══════════════════════════════════════════════════════════════════ */

int renderer_ease_out_cubic(int t, int start, int end, int duration) {
    if (duration <= 0) return end;
    int progress = t * 1000 / duration;
    if (progress > 1000) progress = 1000;
    int p = 1000 - progress;
    int result = 1000 - (p * p * p) / (1000 * 1000);
    return start + (end - start) * result / 1000;
}

int renderer_ease_in_out_cubic(int t, int start, int end, int duration) {
    if (duration <= 0) return end;
    int progress = t * 1000 / duration;
    if (progress > 1000) progress = 1000;
    int result;
    if (progress < 500) {
        result = 4 * progress * progress * progress / (1000 * 1000);
    } else {
        int p = progress - 500;
        result = 1 - (-2 * p + 1000) * (-2 * p + 1000) * (-2 * p + 1000) / (2 * 1000 * 1000 * 1000);
    }
    return start + (end - start) * result / 1000;
}

int renderer_ease_out_back(int t, int start, int end, int duration) {
    if (duration <= 0) return end;
    int progress = t * 1000 / duration;
    if (progress > 1000) progress = 1000;
    int p = progress;
    int result = 1 + 2700 * (p * p * p) / (1000 * 1000 * 1000) - 1800 * (p * p) / (1000 * 1000) + 600 * p / 1000;
    return start + (end - start) * result / 1000;
}
