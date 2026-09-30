#include "lgame.h"

/* Kernel driver headers */
#include "fb.h"
#include "audio.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "mm.h"
#include "string.h"
#include "serial.h"
#include "kprintf.h"

lgame_context_t lgame;

/* Internal state */
static uint8_t lgame_keys[LGAME_MAX_KEYS];
static uint8_t lgame_keys_prev[LGAME_MAX_KEYS];
/* Keys read from the serial line, latched for exactly one frame each: a serial
 * byte has no release event to clear it, unlike a PS/2 key. See
 * lgame_input_poll(). */
static uint8_t lgame_serial_keys[LGAME_MAX_KEYS];
static int lgame_mouse_btn_state[3];
static int lgame_mouse_btn_prev[3];
static uint32_t lgame_rand_seed = 12345;
static int lgame_fb_ready = 0;

static void lgame_draw_title_banner_internal(void);

/* Font glyphs (8x16) used by the kernel console — shared for scaled text */
extern const uint8_t font8x16[256][16];

/* ── Core ── */
int lgame_init(int width, int height, const char *title) {
    if (lgame.initialized)
        return LGAME_ERR_ALREADY_INIT;

    extern struct fb_info_t fb;
    if (fb.width == 0 || fb.height == 0)
        return LGAME_ERR_NO_FB;

    if (!lgame_fb_ready) {
        if (!fb_backbuffer_init())
            return LGAME_ERR_NO_FB;
        lgame_fb_ready = 1;
    }

    lgame.width = (width > 0) ? width : (int)fb_getwidth();
    lgame.height = (height > 0) ? height : (int)fb_getheight();
    lgame.bpp = fb_get_bpp();
    lgame.running = 1;
    lgame.initialized = 1;
    lgame.frame_start_ms = timer_get_milliseconds();
    lgame.last_frame_ms = lgame.frame_start_ms;
    lgame.title[0] = 0;
    if (title) {
        strncpy_safe(lgame.title, title, sizeof(lgame.title));
        lgame.title[sizeof(lgame.title) - 1] = 0;
    }

    memset(lgame_keys, 0, sizeof(lgame_keys));
    memset(lgame_keys_prev, 0, sizeof(lgame_keys_prev));
    memset(lgame_mouse_btn_state, 0, sizeof(lgame_mouse_btn_state));
    memset(lgame_mouse_btn_prev, 0, sizeof(lgame_mouse_btn_prev));

    keyboard_init();
    mouse_init();

    fb_backbuffer_begin();
    fb_fill(0xFF000000);
    if (lgame.title[0]) lgame_draw_title_banner_internal();
    fb_backbuffer_end();
    return LGAME_OK;
}

void lgame_quit(void) {
    if (!lgame.initialized)
        return;
    lgame.running = 0;
    lgame.initialized = 0;

    /* Hand the display back to the shell.
     *
     * Without this the last game frame simply stays on screen: the shell
     * resumes drawing its prompt and menu *over* the frozen game, so the user
     * is left looking at a half-covered playfield they cannot get rid of. The
     * pixels are not merely stale, they are wrong -- nothing redraws them.
     *
     * Order matters. fb_backbuffer_end() first, to flush a frame that was
     * begun but never presented, so the front buffer is not left showing a
     * half-drawn scene. Then fb_clear(), which paints the console background
     * and resets the text cursor, so the shell starts from a known position on
     * a known surface -- and resets the cursor as a side effect, so there is no
     * separate fb_move_cursor() call here to forget. */
    fb_backbuffer_end();
    fb_clear();
}

int lgame_running(void) {
    return lgame.running ? 1 : 0;
}

void lgame_title(const char *title) {
    if (!lgame.initialized) return;
    if (title) {
        strncpy_safe(lgame.title, title, sizeof(lgame.title));
        lgame.title[sizeof(lgame.title) - 1] = 0;
    }
    lgame_draw_title_banner_internal();
}

void lgame_clear(lgame_color_t color) {
    if (!lgame.initialized) return;
    fb_fill(lgame_color_pack(color));
}

void lgame_present(void) {
    if (!lgame.initialized) return;
    /* Flips the whole back buffer to the physical framebuffer */
    fb_backbuffer_end();
}

/* ── Timing ── */
uint64_t lgame_time_ms(void) {
    return timer_get_milliseconds();
}

uint64_t lgame_delta_time_ms(void) {
    return lgame.dt_ms;
}

void lgame_frame_begin(void) {
    uint64_t now = timer_get_milliseconds();
    uint64_t delta = now - lgame.last_frame_ms;
    /* Clamp to LGAME_MAX_DT_MS so one stalled frame cannot make the next
     * update integrate the whole stall at once. (Was 1000ms, which let a
     * one-second hitch fling the ball clear off the pitch.) */
    if (delta > LGAME_MAX_DT_MS) delta = LGAME_MAX_DT_MS;
    lgame.dt_ms = delta;
    lgame.last_frame_ms = now;
    lgame.frame_start_ms = now;

    memcpy(lgame_keys_prev, lgame_keys, sizeof(lgame_keys));
    memcpy(lgame_mouse_btn_prev, lgame_mouse_btn_state, sizeof(lgame_mouse_btn_state));

    /* Start drawing into the back buffer for this frame */
    fb_backbuffer_begin();

#if LGAME_TARGET_FPS > 0
    /* Pace the loop. The game loop is
     *     while (lgame_running()) { lgame_frame_begin(); ...update...; draw;
     *                              lgame_present(); }
     * so without this it runs flat out: the ball's speed is dt-based and so
     * stays correct, but the whole thing burns a full core redrawing the
     * same scene thousands of times a second. Sleeping the remainder of the
     * frame yields the CPU (timer_sleep() halts rather than spins) and caps
     * the rate at LGAME_TARGET_FPS.
     *
     * Only sleeps when the frame came in *under* budget -- if drawing already
     * took longer than the target, there is nothing left to wait off and
     * delaying further would only make the game slower than it already is.
     */
    uint32_t budget_ms = 1000u / (uint32_t)LGAME_TARGET_FPS;
    if (delta < budget_ms)
        timer_sleep_ms((uint32_t)(budget_ms - delta));
#endif
}

/* ── 2D Rendering ── */
void lgame_draw_pixel(int x, int y, lgame_color_t color) {
    if (!lgame.initialized) return;
    if (x < 0 || y < 0 || x >= (int)fb_getwidth() || y >= (int)fb_getheight()) return;
    fb_putpixel((uint32_t)x, (uint32_t)y, lgame_color_pack(color));
}

void lgame_draw_line(int x0, int y0, int x1, int y1, lgame_color_t color) {
    if (!lgame.initialized) return;
    fb_drawline(x0, y0, x1, y1, lgame_color_pack(color));
}

void lgame_draw_rect(int x, int y, int w, int h, lgame_color_t color) {
    if (!lgame.initialized) return;
    fb_drawrect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, lgame_color_pack(color));
}

void lgame_fill_rect(int x, int y, int w, int h, lgame_color_t color) {
    if (!lgame.initialized) return;
    fb_fillrect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, lgame_color_pack(color));
}

void lgame_draw_circle(int cx, int cy, int radius, lgame_color_t color) {
    if (!lgame.initialized || radius <= 0) return;
    uint32_t c = lgame_color_pack(color);
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;
    while (y >= x) {
        fb_putpixel((uint32_t)(cx + x), (uint32_t)(cy + y), c);
        fb_putpixel((uint32_t)(cx - x), (uint32_t)(cy + y), c);
        fb_putpixel((uint32_t)(cx + x), (uint32_t)(cy - y), c);
        fb_putpixel((uint32_t)(cx - x), (uint32_t)(cy - y), c);
        fb_putpixel((uint32_t)(cx + y), (uint32_t)(cy + x), c);
        fb_putpixel((uint32_t)(cx - y), (uint32_t)(cy + x), c);
        fb_putpixel((uint32_t)(cx + y), (uint32_t)(cy - x), c);
        fb_putpixel((uint32_t)(cx - y), (uint32_t)(cy - x), c);
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void lgame_fill_circle(int cx, int cy, int radius, lgame_color_t color) {
    if (!lgame.initialized || radius <= 0) return;
    uint32_t c = lgame_color_pack(color);
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;
    while (y >= x) {
        fb_fillrect((uint32_t)(cx - x), (uint32_t)(cy - y), (uint32_t)(2 * x + 1), 1, c);
        fb_fillrect((uint32_t)(cx - y), (uint32_t)(cy - x), (uint32_t)(2 * y + 1), 1, c);
        fb_fillrect((uint32_t)(cx - x), (uint32_t)(cy + y), (uint32_t)(2 * x + 1), 1, c);
        fb_fillrect((uint32_t)(cx - y), (uint32_t)(cy + x), (uint32_t)(2 * y + 1), 1, c);
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void lgame_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, lgame_color_t color) {
    if (!lgame.initialized) return;
    fb_drawline(x0, y0, x1, y1, lgame_color_pack(color));
    fb_drawline(x1, y1, x2, y2, lgame_color_pack(color));
    fb_drawline(x2, y2, x0, y0, lgame_color_pack(color));
}

void lgame_draw_text(int x, int y, const char *text, lgame_color_t color) {
    lgame_draw_text_scaled(x, y, text, color, 1);
}

void lgame_draw_text_scaled(int x, int y, const char *text, lgame_color_t color, int scale) {
    if (!lgame.initialized || !text || scale <= 0) return;
    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return;
    uint32_t fw = fb_getwidth();
    uint32_t fh = fb_getheight();
    uint32_t pitch = fb_get_pitch() / 4;
    uint32_t c = lgame_color_pack(color);
    while (*text) {
        const uint8_t *glyph = font8x16[(unsigned char)*text];
        for (int row = 0; row < 16; row++) {
            /* Clip in signed space. The old guard was
             *   if ((uint32_t)(y + row * scale) >= fh) continue;
             * which only tests the top edge: a negative y makes the cast wrap
             * to a huge value, so *every* row is skipped and text drawn above
             * the framebuffer silently vanishes -- but only because the cast
             * happened to save it, not because the case was handled. The
             * inner loop then recomputed the same row as `uint32_t py` and
             * indexed buf[py * pitch + qx] with it, so a row that was not
             * skipped for a large negative y would have wrapped to a
             * multi-gigabyte offset. Signed compares bound both ends.
             */
            int row_y = y + row * scale;
            if (row_y >= (int)fh) break;          /* past the bottom: done */
            if (row_y + scale <= 0) continue;     /* still above the top */
            uint8_t bits = glyph[row];
            if (!bits) continue;
            for (int col = 0; col < 8; col++) {
                if (!((bits >> (7 - col)) & 1)) continue;
                int base_x = x + col * scale;
                if (base_x >= (int)fw) break;
                if (base_x + scale <= 0) continue;
                for (int sy = 0; sy < scale; sy++) {
                    int py = row_y + sy;
                    if (py < 0 || py >= (int)fh) continue;
                    for (int sx = 0; sx < scale; sx++) {
                        int qx = base_x + sx;
                        if (qx < 0 || qx >= (int)fw) continue;
                        buf[(size_t)py * pitch + qx] = c;
                    }
                }
            }
        }
        x += 8 * scale;
        text++;
    }
}

int lgame_text_width(const char *text) {
    if (!text) return 0;
    int n = 0;
    while (*text++) n++;
    return n * 8;
}

int lgame_text_width_scaled(const char *text, int scale) {
    return lgame_text_width(text) * (scale > 0 ? scale : 1);
}

int lgame_screen_width(void) {
    return (int)fb_getwidth();
}

int lgame_screen_height(void) {
    return (int)fb_getheight();
}

static void lgame_draw_title_banner_internal(void) {
    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return;
    uint32_t fw = fb_getwidth();
    uint32_t pitch = fb_get_pitch() / 4;
    uint32_t bh = 34;
    for (uint32_t y = 0; y < bh; y++)
        for (uint32_t x = 0; x < fw; x++)
            buf[y * pitch + x] = ((uint32_t)(0x18 * (bh - y) / bh) << 24) |
                                 ((uint32_t)(0x10 * (bh - y) / bh) << 16) |
                                 0x00000000;
    for (uint32_t x = 0; x < fw; x++)
        buf[bh * pitch + x] = 0xFF303030;
    int tw = lgame_text_width_scaled(lgame.title, 2);
    int tx = (int)(fw - (uint32_t)tw) / 2;
    int ty = (int)(bh / 2) - 16;
    if (tx < 0) tx = 0;
    lgame_color_t w = lgame_color_hex(0xFFFFFF);
    lgame_draw_text_scaled(tx, ty, lgame.title, w, 2);
}

/* ── Sprites / Textures ── */
lgame_texture_t *lgame_create_texture(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    lgame_texture_t *tex = (lgame_texture_t *)malloc(sizeof(lgame_texture_t));
    if (!tex) return NULL;
    tex->width = w;
    tex->height = h;
    tex->pixels = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!tex->pixels) {
        free(tex);
        return NULL;
    }
    memset(tex->pixels, 0xFF000000, (size_t)w * (size_t)h * sizeof(uint32_t));
    return tex;
}

void lgame_free_texture(lgame_texture_t *tex) {
    if (!tex) return;
    if (tex->pixels) free(tex->pixels);
    free(tex);
}

/* Source-over blend of a texel onto a destination pixel, in place.
 *
 * The destination framebuffer is opaque (alpha 0xFF everywhere -- fb_fill and
 * the games' clear colour both write 0xFF into the top byte, and
 * `lgame_color_hex` promotes a zero alpha byte to 255), so the result is
 * opaque too and the alpha channel is written as 0xFF rather than dropped.
 *
 * The old code wrote `rb | g`, which emitted an alpha byte of 0. Nothing
 * noticed because the framebuffer is never itself a texture source today --
 * but `lgame_create_texture_from_screen` does exactly that, so any code that
 * round-tripped a screenshot through a texture would have read every pixel
 * back as fully transparent.
 *
 * The channel weights are divided by 255 (the true alpha range) rather than
 * shifted right by 8. `>> 8` divides by 256, which is not the same operation:
 * at alpha=255 it darkens every channel by a factor of 255/256, so a fully
 * opaque blend was never quite the source colour.
 *
 * It is also the *wrong shift for these masks*. `src & 0x00FF00` already sits
 * in the green slot, so shifting it right by 8 drops green into the blue slot
 * and leaves the result needing a re-mask; combined with the missing alpha
 * byte, a mid-alpha blend came out as visibly wrong colours, e.g. 50% white
 * over blue producing rgb(190,255,255) instead of rgb(127,127,255).
 * scripts/lgame_check.py asserts every alpha on a ramp, and reverting this
 * helper to `>> 8` makes exactly the three mid-alpha probes fail while the
 * fully-opaque and fully-transparent columns still pass -- the old code
 * short-circuited those two, so that is the sensitivity the fix had to have.
 */
static inline void lgame_blend_pixel(uint32_t *dstp, uint32_t src) {
    uint32_t dst = *dstp;
    uint32_t alpha = src >> 24;
    if (alpha == 255) {
        *dstp = src;
        return;
    }
    if (alpha == 0)
        return;
    uint32_t inv = 255 - alpha;
    uint32_t rb = (((src & 0xFF00FF) * alpha + (dst & 0xFF00FF) * inv) / 255) & 0xFF00FF;
    uint32_t g  = (((src & 0x00FF00) * alpha + (dst & 0x00FF00) * inv) / 255) & 0x00FF00;
    *dstp = 0xFF000000u | rb | g;
}

void lgame_draw_texture(lgame_texture_t *tex, int x, int y) {
    if (!lgame.initialized || !tex || !tex->pixels) return;
    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return;
    uint32_t fw = fb_getwidth();
    uint32_t fh = fb_getheight();
    uint32_t pitch = fb_get_pitch() / 4;
    for (int row = 0; row < tex->height; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fh) continue;
        for (int col = 0; col < tex->width; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fw) continue;
            lgame_blend_pixel(&buf[(size_t)py * pitch + px],
                              tex->pixels[(size_t)row * tex->width + col]);
        }
    }
}

void lgame_draw_texture_tinted(lgame_texture_t *tex, int x, int y, lgame_color_t tint) {
    if (!lgame.initialized || !tex || !tex->pixels) return;
    uint32_t t = lgame_color_pack(tint);
    uint32_t ta = (t >> 24) & 0xFF;
    uint32_t tr = (t >> 16) & 0xFF;
    uint32_t tg = (t >> 8) & 0xFF;
    uint32_t tb = t & 0xFF;

    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return;
    uint32_t fw = fb_getwidth();
    uint32_t fh = fb_getheight();
    uint32_t pitch = fb_get_pitch() / 4;

    /* The tint is applied to a local copy of each texel and blended straight
     * into the back buffer. This used to write the tinted values back through
     * `tex->pixels` and then call lgame_draw_texture(), which made a *draw*
     * call mutate its own source: drawing the same sprite twice with
     * different tints produced two different results, and a second
     * lgame_draw_texture() of the untinted sprite came out tinted. A caller
     * that reused one texture for a normal blit and a highlighted blit had
     * no way back to the original pixels short of recreating it.
     */
    for (int row = 0; row < tex->height; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fh) continue;
        for (int col = 0; col < tex->width; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fw) continue;
            uint32_t s = tex->pixels[(size_t)row * tex->width + col];
            uint32_t sr = ((s >> 16) & 0xFF) * tr / 255;
            uint32_t sg = ((s >> 8) & 0xFF) * tg / 255;
            uint32_t sb = (s & 0xFF) * tb / 255;
            lgame_blend_pixel(&buf[(size_t)py * pitch + px],
                              (ta << 24) | (sr << 16) | (sg << 8) | sb);
        }
    }
}

void lgame_draw_texture_scaled(lgame_texture_t *tex, int x, int y, int scale) {
    lgame_draw_texture_region_scaled(tex, 0, 0, tex ? tex->width : 0,
                                     tex ? tex->height : 0, x, y, scale);
}

void lgame_draw_texture_region(lgame_texture_t *tex, int sx, int sy, int sw, int sh,
                               int x, int y) {
    lgame_draw_texture_region_scaled(tex, sx, sy, sw, sh, x, y, 1);
}

static void blit_tex(uint32_t *buf, uint32_t pitch, uint32_t fw, uint32_t fh,
                     const uint32_t *px, int tex_w, int sx, int sy, int sw, int sh,
                     int x, int y, int scale) {
    for (int row = 0; row < sh; row++) {
        int py = y + row * scale;
        if (py < 0 || (uint32_t)py >= fh) continue;
        int syr = sy + row;
        for (int col = 0; col < sw; col++) {
            int px0 = x + col * scale;
            if (px0 < 0 || (uint32_t)px0 >= fw) continue;
            int sxr = sx + col;
            uint32_t src = px[(size_t)syr * (size_t)tex_w + (size_t)sxr];
            for (int sy01 = 0; sy01 < scale; sy01++) {
                uint32_t qy = (uint32_t)py + (uint32_t)sy01;
                if (qy >= fh) break;
                for (int sx01 = 0; sx01 < scale; sx01++) {
                    uint32_t qx = (uint32_t)px0 + (uint32_t)sx01;
                    if (qx >= fw) break;
                    lgame_blend_pixel(&buf[qy * pitch + qx], src);
                }
            }
        }
    }
}

void lgame_draw_texture_region_scaled(lgame_texture_t *tex, int sx, int sy, int sw, int sh,
                                      int x, int y, int scale) {
    if (!lgame.initialized || !tex || !tex->pixels) return;
    if (scale <= 0) scale = 1;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sw <= 0 || sh <= 0) return;
    if (sx + sw > tex->width) sw = tex->width - sx;
    if (sy + sh > tex->height) sh = tex->height - sy;
    if (sw <= 0 || sh <= 0) return;
    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return;
    blit_tex(buf, fb_get_pitch() / 4, fb_getwidth(), fb_getheight(),
             tex->pixels, tex->width, sx, sy, sw, sh, x, y, scale);
}

void lgame_draw_texture_flipped(lgame_texture_t *tex, int x, int y, int flip_x, int flip_y) {
    if (!lgame.initialized || !tex || !tex->pixels) return;
    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return;
    uint32_t fw = fb_getwidth();
    uint32_t fh = fb_getheight();
    uint32_t pitch = fb_get_pitch() / 4;
    for (int row = 0; row < tex->height; row++) {
        int py = y + (flip_y ? (tex->height - 1 - row) : row);
        if (py < 0 || (uint32_t)py >= fh) continue;
        for (int col = 0; col < tex->width; col++) {
            int px = x + (flip_x ? (tex->width - 1 - col) : col);
            if (px < 0 || (uint32_t)px >= fw) continue;
            lgame_blend_pixel(&buf[(size_t)py * pitch + px],
                              tex->pixels[(size_t)row * tex->width + col]);
        }
    }
}

lgame_texture_t *lgame_create_texture_from_screen(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return NULL;
    uint32_t fw = fb_getwidth();
    uint32_t fh = fb_getheight();
    uint32_t pitch = fb_get_pitch() / 4;
    if ((uint32_t)(x + w) > fw) w = (int)fw - x;
    if ((uint32_t)(y + h) > fh) h = (int)fh - y;
    if (w <= 0 || h <= 0) return NULL;
    lgame_texture_t *tex = lgame_create_texture(w, h);
    if (!tex) return NULL;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            tex->pixels[(size_t)row * w + col] = buf[(size_t)(y + row) * pitch + (x + col)];
    return tex;
}

void lgame_texture_put_pixel(lgame_texture_t *tex, int x, int y, lgame_color_t color) {
    if (!tex || x < 0 || y < 0 || x >= tex->width || y >= tex->height) return;
    tex->pixels[(size_t)y * tex->width + x] = lgame_color_pack(color);
}

lgame_color_t lgame_texture_get_pixel(lgame_texture_t *tex, int x, int y) {
    lgame_color_t c = {0, 0, 0, 255};
    if (!tex || x < 0 || y < 0 || x >= tex->width || y >= tex->height) return c;
    uint32_t p = tex->pixels[(size_t)y * tex->width + x];
    c.r = (p >> 16) & 0xFF;
    c.g = (p >> 8) & 0xFF;
    c.b = p & 0xFF;
    c.a = (p >> 24) & 0xFF;
    if (c.a == 0) c.a = 255;
    return c;
}

/* ── 3D ── */
/* Software triangle rasteriser with a depth buffer, 16.16 fixed point
 * throughout. Depth is stored as 1/z so that a larger value is nearer, which
 * makes the test a plain ">" and puts the precision where it is needed, near
 * the camera. */
#define LGAME_3D_NEAR     LGAME_FP_ONE          /* 1.0 world unit */
/* Smallest view-z the projection will divide by. lgame_3d_tri() clips
 * against LGAME_3D_NEAR first, so this only ever catches a vertex the clipper
 * interpolated to a hair on the wrong side of the near plane -- it exists to
 * keep lgame_fp_div() away from a zero denominator, not to model anything. */
#define LGAME_3D_ZV_MIN   (LGAME_FP_ONE / 64)
/* Reject a degenerate focal length; 0 would divide by zero in the projection. */
#define LGAME_3D_FOCAL_MIN (LGAME_FP_ONE / 8)
#define LGAME_3D_INVZ_MAX  ((uint32_t)0xFFFFFFFFu)

static uint16_t *lgame_3d_depth;
static int lgame_3d_vw, lgame_3d_vh, lgame_3d_ox, lgame_3d_oy;
static int lgame_3d_on;
static int lgame_3d_cull_on;
/* Camera basis, orthonormal, 16.16 */
static lgame_vec3_t lgame_3d_eye, lgame_3d_right, lgame_3d_up, lgame_3d_fwd;
static lgame_fp_t lgame_3d_focal;

static int lgame_3d_n_submitted, lgame_3d_n_drawn;
static int lgame_3d_n_clipped, lgame_3d_n_culled;

/* ── negative-control seam ──
 *
 * Declared here, with the rest of the 3D state, so a reader of the rasteriser
 * finds the seam they can break rather than just the flag.
 *
 * Each 3D pixel assertion in scripts/lgame_check.py has to be shown to fail
 * when the feature it tests is broken, otherwise it is not an assertion. The
 * way to show that is to break the feature and rerun, and the way to break it
 * here is this variable. The values:
 *
 *   1  depth test always passes (occlusion disabled)
 *   2  backface culling ignored
 *   3  near-plane clipping drops the triangle instead of clipping it
 *   4  lgame_3d_clear() does not paint
 *
 * `volatile` is load-bearing and is the whole reason this is a variable and not
 * a macro or a literal: at -O2 the compiler reads it once, sees 0, and deletes
 * the branch, so a control written as `if (0)` -- or with a plain `int` -- is a
 * control that does nothing and the assertion would then pass with the feature
 * still disabled, which is the exact failure this seam exists to prevent. A
 * volatile load is emitted on every evaluation; the disassembly from the
 * control runs is what shows that, rather than assuming it.
 *
 * MUST be 0 outside a deliberate control run. */
volatile int lgame_3d_break = 0;

/* ── fixed-point helpers ── */
static inline lgame_fp_t lgame_fp_mul(lgame_fp_t a, lgame_fp_t b) {
    return (lgame_fp_t)(((int64_t)a * (int64_t)b) >> LGAME_FP_SHIFT);
}
static inline lgame_fp_t lgame_fp_div(lgame_fp_t a, lgame_fp_t b) {
    if (b == 0) return a < 0 ? -LGAME_3D_INVZ_MAX : LGAME_3D_INVZ_MAX;
    return (lgame_fp_t)((((int64_t)a) << LGAME_FP_SHIFT) / (int64_t)b);
}
/* Integer square root, exact, by the digit-by-digit restoring method. Used
 * instead of Newton-Raphson because a Newton seed has to be guessed and an
 * iteration count has to be guessed, and both were wrong here: the seed was
 * 128x too large, mag/x truncated to 0, and eight iterations of `x = (x + 0)/2`
 * merely halved the seed -- which happened to land within 2x of the right answer
 * and so looked fine. A restoring sqrt has no seed and cannot stop early.
 *
 * Returns floor(sqrt(n)) for n up to 2^62. */
static uint64_t lgame_isqrt(uint64_t n) {
    uint64_t res = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* Square root of a 16.16 magnitude, as a 16.16 value.
 *
 * The scaling is the part that is easy to get wrong. `mag` holds a squared
 * length already in 16.16, so the root is sqrt(mag) * 256, NOT sqrt(mag):
 * sqrt(mag << 16). Getting that factor wrong makes every "normalised" vector
 * the wrong length, which does not fail loudly -- it just quietly produces a
 * camera basis that is not orthonormal, and then every projection downstream
 * is wrong. */
static lgame_fp_t lgame_fp_isqrt(uint64_t mag) {
    if (mag == 0) return 0;
    /* mag << 16 must stay inside 64 bits. A magnitude this large means a
     * coordinate far outside any sane world, so saturate rather than wrap. */
    if (mag >= (1ULL << 46)) return LGAME_FP_ONE / 8;
    return (lgame_fp_t)lgame_isqrt(mag << 16);
}
static lgame_vec3_t lgame_vec3_sub(lgame_vec3_t a, lgame_vec3_t b) {
    lgame_vec3_t r; r.x = a.x - b.x; r.y = a.y - b.y; r.z = a.z - b.z; return r;
}
static lgame_vec3_t lgame_vec3_cross(lgame_vec3_t a, lgame_vec3_t b) {
    lgame_vec3_t r;
    r.x = lgame_fp_mul(a.y, b.z) - lgame_fp_mul(a.z, b.y);
    r.y = lgame_fp_mul(a.z, b.x) - lgame_fp_mul(a.x, b.z);
    r.z = lgame_fp_mul(a.x, b.y) - lgame_fp_mul(a.y, b.x);
    return r;
}
static lgame_fp_t lgame_vec3_dot(lgame_vec3_t a, lgame_vec3_t b) {
    return lgame_fp_mul(a.x, b.x) + lgame_fp_mul(a.y, b.y) + lgame_fp_mul(a.z, b.z);
}
/* Scale to unit length. Returns 0 for a zero vector, so callers can detect the
 * degenerate case instead of dividing by a rounded-to-zero length. */
static lgame_vec3_t lgame_vec3_normalize(lgame_vec3_t v) {
    uint64_t mag = (uint64_t)lgame_fp_mul(v.x, v.x)
                 + (uint64_t)lgame_fp_mul(v.y, v.y)
                 + (uint64_t)lgame_fp_mul(v.z, v.z);
    lgame_fp_t len = lgame_fp_isqrt(mag);
    lgame_vec3_t r;
    if (len == 0) { r.x = r.y = r.z = 0; return r; }
    r.x = lgame_fp_div(v.x, len);
    r.y = lgame_fp_div(v.y, len);
    r.z = lgame_fp_div(v.z, len);
    return r;
}

int lgame_3d_active(void) { return lgame_3d_on; }

int lgame_3d_begin(int view_w, int view_h, int ox, int oy) {
    if (!lgame.initialized) return LGAME_ERR_NOT_INIT;
    if (view_w <= 0 || view_h <= 0) return LGAME_ERR_INVALID_ARG;
    /* Refuse a viewport that cannot fit the surface rather than clipping every
     * draw: a silently cropped viewport is much harder to debug than an error. */
    if (ox < 0 || oy < 0 ||
        ox + view_w > (int)fb_getwidth() || oy + view_h > (int)fb_getheight())
        return LGAME_ERR_INVALID_ARG;

    lgame_3d_end();   /* idempotent: re-begin does not leak the old buffer */

    size_t n = (size_t)view_w * (size_t)view_h;
    lgame_3d_depth = (uint16_t *)malloc(n * sizeof(uint16_t));
    if (!lgame_3d_depth) return LGAME_ERR_INVALID_ARG;

    lgame_3d_vw = view_w;
    lgame_3d_vh = view_h;
    lgame_3d_ox = ox;
    lgame_3d_oy = oy;
    lgame_3d_on = 1;
    lgame_3d_cull_on = 0;
    /* Sensible default camera until lgame_3d_camera() is called. focal =
     * view_h is roughly a 53-degree vertical field. */
    lgame_3d_camera(lgame_vec3(LGAME_FP_0, LGAME_FP_0, LGAME_FP_4),
                    lgame_vec3(LGAME_FP_0, LGAME_FP_0, LGAME_FP_0),
                    view_h);
    lgame_3d_reset_counters();
    return LGAME_OK;
}

void lgame_3d_end(void) {
    if (lgame_3d_depth) {
        free(lgame_3d_depth);
        lgame_3d_depth = NULL;
    }
    lgame_3d_vw = lgame_3d_vh = lgame_3d_ox = lgame_3d_oy = 0;
    lgame_3d_on = 0;
}

void lgame_3d_camera(lgame_vec3_t eye, lgame_vec3_t target, lgame_fp_t focal) {
    if (focal < LGAME_3D_FOCAL_MIN) focal = LGAME_3D_FOCAL_MIN;
    lgame_vec3_t fwd = lgame_vec3_normalize(lgame_vec3_sub(target, eye));
    if (fwd.x == 0 && fwd.y == 0 && fwd.z == 0)
        return;   /* eye == target: keep the previous camera, do not zero it */

    /* right = normalise(cross(fwd, world_up)). World up is +Y; a camera looking
     * straight up or down is degenerate against it, so fall back to +Z, which
     * keeps a usable basis instead of a zero-length right vector. */
    lgame_vec3_t world_up = lgame_vec3(LGAME_FP_0, LGAME_FP_1, LGAME_FP_0);
    lgame_vec3_t right = lgame_vec3_cross(fwd, world_up);
    if (right.x == 0 && right.y == 0 && right.z == 0)
        right = lgame_vec3_cross(fwd, lgame_vec3(LGAME_FP_0, LGAME_FP_0, LGAME_FP_1));
    right = lgame_vec3_normalize(right);
    lgame_vec3_t up = lgame_vec3_cross(right, fwd);

    lgame_3d_eye = eye;
    lgame_3d_fwd = fwd;
    lgame_3d_right = right;
    lgame_3d_up = up;
    lgame_3d_focal = focal;
}

void lgame_3d_clear(lgame_color_t sky) {
    if (!lgame_3d_on) return;
    uint32_t c = lgame_color_pack(sky);
    for (int y = 0; y < lgame_3d_vh; y++) {
        for (int x = 0; x < lgame_3d_vw; x++) {
            lgame_3d_depth[(size_t)y * lgame_3d_vw + x] = 0;
            fb_putpixel((uint32_t)(lgame_3d_ox + x),
                        (uint32_t)(lgame_3d_oy + y), c);
        }
    }
}

void lgame_3d_cull(int on) { lgame_3d_cull_on = on ? 1 : 0; }

int  lgame_3d_submitted(void) { return lgame_3d_n_submitted; }
int  lgame_3d_drawn(void)      { return lgame_3d_n_drawn; }
int  lgame_3d_clipped(void)    { return lgame_3d_n_clipped; }
int  lgame_3d_culled(void)     { return lgame_3d_n_culled; }
void lgame_3d_reset_counters(void) {
    lgame_3d_n_submitted = lgame_3d_n_drawn = 0;
    lgame_3d_n_clipped = lgame_3d_n_culled = 0;
}

/* A vertex after the view transform and projection. */
typedef struct {
    int      sx, sy;      /* screen pixels, surface coordinates */
    lgame_fp_t invz;      /* 1/z in 16.16; larger is nearer */
} lgame_3d_vtx;

static void lgame_3d_project(lgame_vec3_t p, lgame_3d_vtx *out) {
    lgame_vec3_t d = lgame_vec3_sub(p, lgame_3d_eye);
    lgame_fp_t zv = lgame_vec3_dot(d, lgame_3d_fwd);
    lgame_fp_t xv = lgame_vec3_dot(d, lgame_3d_right);
    lgame_fp_t yv = lgame_vec3_dot(d, lgame_3d_up);
    if (zv < LGAME_3D_ZV_MIN) zv = LGAME_3D_ZV_MIN;   /* caller clips first */
    out->invz = lgame_fp_div(LGAME_FP_ONE, zv);
    /* lgame_fp_div() returns 16.16, so the shift is what turns the focal-scaled
     * offset back into whole pixels. Leaving it out does not give a slightly
     * wrong picture: every coordinate comes out 65536x too large, so every
     * bounding box lands off the surface and the rasteriser quietly returns
     * without drawing a single pixel. */
    out->sx = lgame_3d_ox + lgame_3d_vw / 2
            + (lgame_fp_div(lgame_fp_mul(xv, lgame_3d_focal), zv) >> LGAME_FP_SHIFT);
    out->sy = lgame_3d_oy + lgame_3d_vh / 2
            - (lgame_fp_div(lgame_fp_mul(yv, lgame_3d_focal), zv) >> LGAME_FP_SHIFT);
}

/* Edge function: positive on one side of the directed edge a->b. */
static inline int64_t lgame_3d_edge(lgame_3d_vtx a, lgame_3d_vtx b, int px, int py) {
    return (int64_t)(px - a.sx) * (int64_t)(b.sy - a.sy)
         - (int64_t)(py - a.sy) * (int64_t)(b.sx - a.sx);
}

static void lgame_3d_raster(lgame_3d_vtx v0, lgame_3d_vtx v1, lgame_3d_vtx v2,
                            uint32_t packed) {
    int64_t area = lgame_3d_edge(v0, v1, v2.sx, v2.sy);
    if (area == 0) return;
    if (lgame_3d_cull_on && area < 0 && lgame_3d_break != 2) {
        lgame_3d_n_culled++;
        return;
    }

    /* Orient so area > 0 and the inside test is a simple sign check. */
    if (area < 0) {
        lgame_3d_vtx t = v1; v1 = v2; v2 = t;
        area = -area;
    }

    int minx = v0.sx < v1.sx ? v0.sx : v1.sx;
    if (v2.sx < minx) minx = v2.sx;
    int maxx = v0.sx > v1.sx ? v0.sx : v1.sx;
    if (v2.sx > maxx) maxx = v2.sx;
    int miny = v0.sy < v1.sy ? v0.sy : v1.sy;
    if (v2.sy < miny) miny = v2.sy;
    int maxy = v0.sy > v1.sy ? v0.sy : v1.sy;
    if (v2.sy > maxy) maxy = v2.sy;

    /* Clip the bounding box to the viewport, then to the surface. */
    if (minx < lgame_3d_ox) minx = lgame_3d_ox;
    if (miny < lgame_3d_oy) miny = lgame_3d_oy;
    if (maxx >= lgame_3d_ox + lgame_3d_vw) maxx = lgame_3d_ox + lgame_3d_vw - 1;
    if (maxy >= lgame_3d_oy + lgame_3d_vh) maxy = lgame_3d_oy + lgame_3d_vh - 1;
    if (minx > maxx || miny > maxy) return;

    int drew = 0;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            int64_t w0 = lgame_3d_edge(v1, v2, x, y);
            int64_t w1 = lgame_3d_edge(v2, v0, x, y);
            int64_t w2 = lgame_3d_edge(v0, v1, x, y);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;

            /* 1/z interpolated linearly in screen space, which is the correct
             * depth for a perspective projection. */
            int64_t invz = (w0 * (int64_t)v0.invz
                          + w1 * (int64_t)v1.invz
                          + w2 * (int64_t)v2.invz) / area;
            if (invz < 0) invz = 0;
            /* Quantise to 16 bits, with the near plane landing on 0xFFFF.
             * invz is at most LGAME_FP_ONE here because lgame_3d_tri() clips
             * against LGAME_3D_NEAR == LGAME_FP_ONE before projecting, so the
             * scale is a constant rather than something derived from the
             * focal length. Deriving it from the focal is what this replaced:
             * focal/NEAR is ~360, which pushed every depth past 0xFFFF, so
             * every pixel compared equal and the depth buffer silently tested
             * nothing at all. */
            uint32_t depth = (uint32_t)(((uint64_t)invz
                                         * (uint64_t)((1u << LGAME_3D_DEPTH_BITS) - 1u))
                                        >> LGAME_FP_SHIFT);
            if (depth > (1u << LGAME_3D_DEPTH_BITS) - 1u)
                depth = (1u << LGAME_3D_DEPTH_BITS) - 1u;

            size_t idx = (size_t)(y - lgame_3d_oy) * lgame_3d_vw
                       + (size_t)(x - lgame_3d_ox);
            if (depth <= lgame_3d_depth[idx] && lgame_3d_break != 1)
                continue;   /* occluded */
            lgame_3d_depth[idx] = (uint16_t)depth;
            fb_putpixel((uint32_t)x, (uint32_t)y, packed);
            drew = 1;
        }
    }
    if (drew) lgame_3d_n_drawn++;
}

void lgame_3d_tri(lgame_vec3_t a, lgame_vec3_t b, lgame_vec3_t c,
                  lgame_color_t col) {
    if (!lgame_3d_on) return;
    lgame_3d_n_submitted++;

    /* View-space z decides the near clip, so transform first. */
    lgame_vec3_t v[3] = { a, b, c };
    lgame_fp_t vz[3];
    for (int i = 0; i < 3; i++)
        vz[i] = lgame_vec3_dot(lgame_vec3_sub(v[i], lgame_3d_eye), lgame_3d_fwd);

    int inside[3];
    int n_in = 0;
    for (int i = 0; i < 3; i++) {
        inside[i] = (vz[i] >= LGAME_3D_NEAR);
        if (inside[i]) n_in++;
    }

    if (n_in == 3) {
        lgame_3d_vtx p[3];
        for (int i = 0; i < 3; i++) lgame_3d_project(v[i], &p[i]);
        lgame_3d_raster(p[0], p[1], p[2], lgame_color_pack(col));
        return;
    }
    if (n_in == 0) { lgame_3d_n_clipped++; return; }

    /* Sutherland-Hodgman against the single near plane. A triangle crossing it
     * yields a 3- or 4-gon, which is then fanned back into triangles. */
    lgame_vec3_t poly[4];
    int np = 0;
    for (int i = 0; i < 3; i++) {
        lgame_vec3_t cur = v[i], nxt = v[(i + 1) % 3];
        lgame_fp_t zc = vz[i], zn = vz[(i + 1) % 3];
        if (inside[i]) poly[np++] = cur;
        if (inside[i] != inside[(i + 1) % 3]) {
            /* Interpolate to z == NEAR. zn - zc is non-zero here because the two
             * vertices are on opposite sides. */
            lgame_fp_t t = lgame_fp_div(LGAME_3D_NEAR - zc, zn - zc);
            poly[np].x = cur.x + lgame_fp_mul(nxt.x - cur.x, t);
            poly[np].y = cur.y + lgame_fp_mul(nxt.y - cur.y, t);
            poly[np].z = cur.z + lgame_fp_mul(nxt.z - cur.z, t);
            np++;
        }
    }
    lgame_3d_n_clipped++;
    if (np < 3) return;
    /* Control 3: skip the fan, so a straddling triangle vanishes instead of
     * being clipped. The probe's clip-band sample then reads sky. */
    if (lgame_3d_break == 3) return;
    for (int i = 1; i + 1 < np; i++) {
        lgame_3d_vtx p[3];
        lgame_3d_project(poly[0], &p[0]);
        lgame_3d_project(poly[i], &p[1]);
        lgame_3d_project(poly[i + 1], &p[2]);
        lgame_3d_raster(p[0], p[1], p[2], lgame_color_pack(col));
    }
}

/* ── 3D probe ──
 *
 * Three sub-probes, each isolating one property of the rasteriser so a failure
 * names the thing that broke rather than "the 3D part is wrong".
 *
 * The geometry is chosen so every expected pixel can be derived on paper from
 * the projection, with >=12px between the sampling point and the nearest
 * boundary, so a couple of units of fixed-point truncation cannot move a
 * sample off a triangle. All the half-extents are exact in 16.16 (multiples of
 * 1/16), which is why the numbers are what they are.
 *
 * The spec is printed to the serial line as well as drawn, so lgame_check.py
 * asserts against what the kernel says it drew rather than against a second,
 * hand-maintained copy of the same numbers. If the rasteriser puts a vertex in
 * the wrong place, the pixels disagree with the printed intent; the harness
 * cannot silently agree with a wrong implementation.
 */
#define SELFTEST_3D_VW    320
#define SELFTEST_3D_VH    480
#define SELFTEST_3D_OX    40
#define SELFTEST_3D_OY    120
#define SELFTEST_3D_CX    (SELFTEST_3D_OX + SELFTEST_3D_VW / 2)  /* 200 */
#define SELFTEST_3D_CY    (SELFTEST_3D_OY + SELFTEST_3D_VH / 2)  /* 360 */
#define SELFTEST_3D_FOCAL 360          /* pixels; independent of the viewport */
#define SELFTEST_3D_EYE_Z 4            /* camera at z=4 looking down -Z */

#define SELFTEST_3D_SKY   0xFF1A1C20u  /* rgb(26,28,32), distinct from every probe */
#define SELFTEST_3D_RED   0xFFFF0000u  /* furthest */
#define SELFTEST_3D_GREEN 0xFF00FF00u
#define SELFTEST_3D_CYAN  0xFF00FFFFu
#define SELFTEST_3D_BLUE  0xFF0000FFu  /* nearest */
#define SELFTEST_3D_CLIP  0xFFFF8000u  /* straddles the near plane */
#define SELFTEST_3D_CULL  0xFFFF00FFu  /* back-facing: must NOT appear */
#define SELFTEST_3D_KEEP  0xFFFFFF00u  /* front-facing: must appear */

#define SELFTEST_3D_FP_1    (LGAME_FP_ONE)
#define SELFTEST_3D_FP_2    (2 * LGAME_FP_ONE)
#define SELFTEST_3D_FP_4    (4 * LGAME_FP_ONE)
#define SELFTEST_3D_FP_1_2  (LGAME_FP_ONE / 2)
#define SELFTEST_3D_FP_7_8  (7 * LGAME_FP_ONE / 8)
/* World half-extents for the staircase, as exact 16.16 (multiples of 1/16, so
 * no truncation creeps into the on-screen extents the assertions rely on):
 * on-screen half = half_w * focal / d, giving 135, 90, 67 and 45 px. */
#define SELFTEST_3D_FP_1_8  (LGAME_FP_ONE / 8)
#define SELFTEST_3D_FP_3_8  (3 * LGAME_FP_ONE / 8)
#define SELFTEST_3D_FP_3_4  (3 * LGAME_FP_ONE / 4)
#define SELFTEST_3D_FP_3_2  (3 * LGAME_FP_ONE / 2)
/* Off-axis centre for the cull probe: world yv = -63000 projects to screen
 * y = 519, which clears the staircase (bottom 495) and the viewport bottom
 * (599) while leaving room for the probe's own half-extent. */
#define SELFTEST_3D_CULL_YV (-63000)

/* An axis-aligned quad at z = eye_z - d, centred on (cx, cy) in the view
 * plane. `flip` reverses the winding of both triangles, which is what the
 * backface culler is supposed to notice. */
static void selftest_3d_quad(int d, lgame_fp_t cx, lgame_fp_t cy,
                             lgame_fp_t half_w, lgame_color_t col, int flip) {
    /* `d` is a distance in whole world units, so it has to be scaled into
     * 16.16 before it can be subtracted from a 16.16 coordinate. Subtracting
     * the raw int here puts every quad at z = 4 - 1 instead of 4 - 1.0, i.e.
     * 262143 instead of 196608, and then every view-space z collapses to a
     * hair above zero and the near clip rejects all of it. */
    lgame_fp_t z  = SELFTEST_3D_FP_4 - (lgame_fp_t)d * LGAME_FP_ONE;
    lgame_fp_t x0 = cx - half_w, x1 = cx + half_w;
    lgame_fp_t y0 = cy - half_w, y1 = cy + half_w;
    if (!flip) {
        lgame_3d_tri(lgame_vec3(x0, y0, z), lgame_vec3(x1, y0, z),
                     lgame_vec3(x0, y1, z), col);
        lgame_3d_tri(lgame_vec3(x1, y0, z), lgame_vec3(x1, y1, z),
                     lgame_vec3(x0, y1, z), col);
    } else {
        lgame_3d_tri(lgame_vec3(x0, y1, z), lgame_vec3(x1, y0, z),
                     lgame_vec3(x0, y0, z), col);
        lgame_3d_tri(lgame_vec3(x0, y1, z), lgame_vec3(x1, y1, z),
                     lgame_vec3(x1, y0, z), col);
    }
}

/* lgame_selftest()'s 3D section. Returns 0 on success, or a negative rc naming
 * which probe could not be set up, so a probe that silently did not run is
 * impossible -- a check that skips its own subject is how a gate starts
 * passing for the wrong reason. */
static int selftest_3d(void) {
    lgame_vec3_t eye  = lgame_vec3(LGAME_FP_0, LGAME_FP_0, SELFTEST_3D_FP_4);
    lgame_vec3_t zero = lgame_vec3(LGAME_FP_0, LGAME_FP_0, LGAME_FP_0);

    if (lgame_3d_begin(SELFTEST_3D_VW, SELFTEST_3D_VH,
                       SELFTEST_3D_OX, SELFTEST_3D_OY) != LGAME_OK)
        return -11;
    lgame_3d_camera(eye, zero, (lgame_fp_t)SELFTEST_3D_FOCAL * LGAME_FP_ONE);
    lgame_3d_cull(0);
    lgame_3d_reset_counters();
    /* Control 4: skip the sky fill. The quads are still drawn over whatever
     * lgame_clear() left behind, so every probe sample still reads its own
     * colour and `drawn` is unchanged -- the sky is the ONLY thing a missing
     * clear takes away, which is why the check asserts on it explicitly rather
     * than inferring it from the samples. */
    if (lgame_3d_break != 4)
        lgame_3d_clear(lgame_color_hex(SELFTEST_3D_SKY));
    /* ── Probe A: the depth staircase ────────────────────────────────────
     * Four nested quads at distances 4, 3, 2, 1, whose on-screen half-extents
     * work out to 135, 90, 67 and 45 px (67.5 and 22.5 truncate). Submitted
     * NEAREST FIRST, so every later quad overlaps every earlier one and the
     * last one drawn -- RED, the largest of the four -- covers the lot. With no
     * depth test all four samples would therefore read RED. So this one set of
     * assertions covers the whole depth buffer: each sample must read the
     * colour of the nearest quad reaching it. */
    selftest_3d_quad(1, 0, 0, SELFTEST_3D_FP_1_8, lgame_color_hex(SELFTEST_3D_BLUE),  0);
    selftest_3d_quad(2, 0, 0, SELFTEST_3D_FP_3_8, lgame_color_hex(SELFTEST_3D_CYAN),  0);
    selftest_3d_quad(3, 0, 0, SELFTEST_3D_FP_3_4, lgame_color_hex(SELFTEST_3D_GREEN), 0);
    selftest_3d_quad(4, 0, 0, SELFTEST_3D_FP_3_2, lgame_color_hex(SELFTEST_3D_RED),   0);

    /* ── Probe B: near-plane clipping ────────────────────────────────────
     * Two vertices at d=2 (in front of the near plane at 1.0) and one at d=0.5
     * (behind it). Clipping must yield a polygon that is still drawn, not drop
     * the triangle. The two in-front vertices land at screen y=203 and the
     * clipped edge runs off the top of the viewport, leaving a band around
     * y=180 where only a correctly clipped triangle could have put a pixel. */
    lgame_3d_tri(lgame_vec3( SELFTEST_3D_FP_1_2, SELFTEST_3D_FP_7_8,
                             SELFTEST_3D_FP_4 - 2 * LGAME_FP_ONE),
                 lgame_vec3(-SELFTEST_3D_FP_1_2, SELFTEST_3D_FP_7_8,
                             SELFTEST_3D_FP_4 - 2 * LGAME_FP_ONE),
                 lgame_vec3( LGAME_FP_0, SELFTEST_3D_FP_7_8,
                             SELFTEST_3D_FP_4 - SELFTEST_3D_FP_1_2),
                 lgame_color_hex(SELFTEST_3D_CLIP));

    /* ── Probe C: backface culling ───────────────────────────────────────
     * Two concentric quads below the staircase, the LARGER one wound the wrong
     * way round. With culling on, the reversed quad must be rejected outright
     * and its colour appear nowhere. The size difference is the point: if the
     * two quads were the same shape, a culler that silently ignored the
     * winding would still hide the wrong one under the right one, and the
     * "must not appear" assertion would pass for the wrong reason. Here the
     * wrong-wound quad would leave a 17px ring of its own colour around the
     * correct one. The small forward-wound quad is also the control that rules
     * out a rasteriser which simply drew nothing here. */
    lgame_3d_cull(1);
    selftest_3d_quad(2, 0, SELFTEST_3D_CULL_YV, 3 * LGAME_FP_ONE / 16,
                     lgame_color_hex(SELFTEST_3D_CULL), 1);   /* must vanish */
    selftest_3d_quad(2, 0, SELFTEST_3D_CULL_YV, 3 * LGAME_FP_ONE / 32,
                     lgame_color_hex(SELFTEST_3D_KEEP), 0);   /* must show */

    /* Sample points and the colour each must read. Offsets from the centre are
     * 112 / 78 / 55 / 30 px against on-screen half-extents of 135 / 90 / 67 /
     * 45, so every offset falls strictly inside one ring and strictly outside
     * the next, by at least 12px -- far more than the couple of units of
     * fixed-point truncation in the projection. */
    struct { int dx, dy; uint32_t hex; const char *name; } probes[] = {
        { -112,    0, SELFTEST_3D_RED,   "stair_red"   },
        {  -78,    0, SELFTEST_3D_GREEN, "stair_green" },
        {  -55,    0, SELFTEST_3D_CYAN,  "stair_cyan"  },
        {  -30,    0, SELFTEST_3D_BLUE,  "stair_blue"  },
        {    0, -180, SELFTEST_3D_CLIP,  "clip_band"   },
        {    0,  159, SELFTEST_3D_KEEP,  "cull_keep"   },
    };
    const int n_probes = (int)(sizeof(probes) / sizeof(probes[0]));

    for (int i = 0; i < n_probes; i++) {
        lgame_color_t want = lgame_color_hex(probes[i].hex);
        kprintf("lgame: 3dprobe %s %d,%d %d,%d,%d\n", probes[i].name,
                SELFTEST_3D_CX + probes[i].dx, SELFTEST_3D_CY + probes[i].dy,
                want.r, want.g, want.b);
    }
    {
        lgame_color_t cull = lgame_color_hex(SELFTEST_3D_CULL);
        lgame_color_t sky  = lgame_color_hex(SELFTEST_3D_SKY);
        kprintf("lgame: 3dprobe absent %d,%d,%d\n", cull.r, cull.g, cull.b);
        kprintf("lgame: 3dprobe sky %d,%d,%d %d,%d,%d,%d\n",
                sky.r, sky.g, sky.b,
                SELFTEST_3D_OX, SELFTEST_3D_OY, SELFTEST_3D_VW, SELFTEST_3D_VH);
    }
    kprintf("lgame: 3dcounters submitted=%d drawn=%d clipped=%d culled=%d\n",
            lgame_3d_submitted(), lgame_3d_drawn(),
            lgame_3d_clipped(), lgame_3d_culled());

    /* Free the depth buffer now rather than holding ~300 KB for the life of
     * the frame. The check asserts the counters, which are plain ints. */
    lgame_3d_end();
    return 0;
}

/* ── Audio ── */
int lgame_play_sound(int freq, int duration_ms) {
    return lgame_play_tone(freq, duration_ms, 100);
}

int lgame_play_tone(int freq, int duration_ms, int volume) {
    if (!lgame.initialized || freq <= 0) return -1;
    audio_play_note_vol(freq, duration_ms, volume);
    return 0;
}

int lgame_play_beep(int freq, int duration_ms) {
    return lgame_play_sound(freq, duration_ms);
}

void lgame_audio_notify(void) {
    if (!lgame.initialized) return;
    audio_notify_beep();
}

void lgame_audio_error(void) {
    if (!lgame.initialized) return;
    audio_error_beep();
}

void lgame_audio_success(void) {
    if (!lgame.initialized) return;
    audio_success_beep();
}

/* ── Input ── */
void lgame_input_poll(void) {
    if (!lgame.initialized) return;

    /* Release the keys that the serial drain latched last frame (see below)
     * BEFORE polling the keyboard, so a key held on the PS/2 port at the same
     * time still reports as down. */
    for (int k = 0; k < LGAME_MAX_KEYS; k++)
        if (lgame_serial_keys[k]) lgame_keys[k] = 0;
    memset(lgame_serial_keys, 0, sizeof(lgame_serial_keys));

    keyboard_poll();

    key_event_t ev;
    while (keyboard_get_event(&ev)) {
        int key = ev.keycode;
        if (key >= 0 && key < LGAME_MAX_KEYS) {
            lgame_keys[key] = ev.pressed ? 1 : 0;
        }
    }

    /* Drain the serial console as well.
     *
     * The graphical titles are reachable *only* as fullscreen shell builtins,
     * because qt_desktop_run() never returns to a shell, so on the `no-desktop`
     * boot entry the serial line is the user's sole input device. Without this,
     * a key typed there never reached the game: ESC could not quit it and the
     * session had to be reset from outside the machine.
     *
     * The byte is latched for exactly one frame. lgame_frame_begin() copies
     * lgame_keys into lgame_keys_prev, and lgame_key_pressed() is
     * `down && !prev`. A PS/2 key is cleared by its own release event, but a
     * serial byte has no release, so without the latch it would read as held
     * for the rest of the session -- lgame_key_pressed() true forever, and a
     * game polling a movement key would walk off the board. */
    while (serial_available()) {
        int key = (unsigned char)serial_readchar();
        if (key > 0 && key < LGAME_MAX_KEYS) {
            lgame_serial_keys[key] = 1;
            lgame_keys[key] = 1;
        }
    }

    int buttons = mouse_get_buttons();
    lgame_mouse_btn_state[0] = buttons & MOUSE_LEFT;
    lgame_mouse_btn_state[1] = buttons & MOUSE_RIGHT;
    lgame_mouse_btn_state[2] = buttons & 0x04;
}

int lgame_key_down(int key) {
    if (key < 0 || key >= LGAME_MAX_KEYS) return 0;
    return lgame_keys[key];
}

int lgame_key_pressed(int key) {
    if (key < 0 || key >= LGAME_MAX_KEYS) return 0;
    return lgame_keys[key] && !lgame_keys_prev[key];
}

int lgame_mouse_x(void) {
    return mouse_get_x();
}

int lgame_mouse_y(void) {
    return mouse_get_y();
}

int lgame_mouse_dx(void) {
    return mouse_get_dx();
}

int lgame_mouse_dy(void) {
    return mouse_get_dy();
}

int lgame_mouse_button(int button) {
    if (button < 0 || button > 2) return 0;
    return lgame_mouse_btn_state[button];
}

/* ── Math helpers ── */
int lgame_rand(int min, int max) {
    if (min >= max) return min;
    lgame_rand_seed = lgame_rand_seed * 1103515245 + 12345;
    uint32_t r = (lgame_rand_seed / 65536) % (uint32_t)(max - min + 1);
    return min + (int)r;
}

void lgame_srand(uint32_t seed) {
    lgame_rand_seed = seed;
}

/* ── Self test ── */
/* Most of this API is otherwise unreachable from the shipped binary. The two
 * games use only the rect, pixel and text primitives, so --gc-sections
 * discards the whole texture path, and a defect in code no binary contains
 * cannot be runtime-verified -- only build-verified. This draws a fixed frame
 * whose every probe pixel has an exactly computable colour, and
 * scripts/lgame_check.py asserts those pixels.
 *
 * Layout, 64x64 blocks left to right from the top-left, over an opaque blue
 * backdrop (0x0000FF):
 *
 *   A (0,0)     left as bare backdrop: the "untouched" control
 *   B (64,0)    the whole texture, an alpha ramp from opaque white at its
 *               left column to fully transparent at its right. A ramp makes
 *               the blend formula checkable across every alpha instead of at
 *               one lucky value.
 *   C (128,0)   the same texture tinted green. This shows the tint is applied
 *               at all; it cannot show whether it was applied *in place*,
 *               because B is already on screen by the time C is drawn. G is
 *               the mutation probe.
 *   D (192,0)   the texture's top-left 32x32 quadrant at 2x (scaled region).
 *   E (256,0)   the whole texture flipped in both axes. There is no
 *               region-flipped entry point -- lgame_draw_texture_flipped()
 *               takes the full texture plus two flags -- so this is the
 *               complete 64x64 ramp mirrored, opaque column now on the right.
 *   F (900,0)   text drawn at y = -8, deliberately off the top edge. Under
 *               the old unsigned guard a row was written at a multi-gigabyte
 *               offset; it must now clip cleanly, with only rows 0..7 of the
 *               16-row glyph landing on screen. Placed clear of the blocks so
 *               it cannot overwrite the A control.
 *   G (384,0)   the same texture drawn UNTINTED, after the tinted draw at C.
 *               Must be identical to B. This is the mutation probe: B, D and
 *               E would also be wrong under the old in-place tint, but only as
 *               a side effect of being drawn later, so G is the only
 *               assertion that tests the claim itself. Reinstating the old
 *               write-back (with the blend left untouched, so C is unaffected)
 *               fails all six G assertions plus D and E, and leaves A, B, C
 *               and F passing -- the check fires on the mutation and only on
 *               the mutation.
 */
#define SELFTEST_BLOCK   64
#define SELFTEST_BG      0xFF0000FFu   /* opaque blue  */
#define SELFTEST_OPAQUE  0xFFFFFFFFu   /* opaque white */
#define SELFTEST_TINT    0xFF00FF00u   /* green tint   */

int lgame_selftest(void) {
    if (lgame_init(0, 0, "LGame selftest") != LGAME_OK)
        return -1;

    lgame_frame_begin();
    lgame_clear(lgame_color_hex(SELFTEST_BG));

    /* 64x64 alpha ramp: opaque white at x=0 fading to fully transparent at
     * x=63, so each column has a known (src, alpha) pair. */
    lgame_texture_t *tex = lgame_create_texture(64, 64);
    if (!tex) {
        lgame_quit();
        return -2;
    }
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint32_t a = (uint32_t)(255 - (x * 255) / 63);
            tex->pixels[(size_t)y * 64 + x] = (a << 24) | 0x00FFFFFFu;
        }
    }

    /* Blend/ramp probe. */
    lgame_draw_texture(tex, SELFTEST_BLOCK * 1, 0);
    /* Tint probe -- drawn after B, so an in-place mutation would show in B. */
    lgame_draw_texture_tinted(tex, SELFTEST_BLOCK * 2, 0,
                              lgame_color_hex(SELFTEST_TINT));
    /* Scaled region probe. */
    lgame_draw_texture_region_scaled(tex, 0, 0, 32, 32,
                                     SELFTEST_BLOCK * 3, 0, 2);
    /* Flip probe. */
    lgame_draw_texture_flipped(tex, SELFTEST_BLOCK * 4, 0, 1, 1);
    /* Off the top edge: must clip, not wrap. Kept well clear of the probe
     * blocks -- the glyph is 16 rows tall starting at y=-8, so rows 0..7 DO
     * land on screen by design, and placing this over block A would destroy
     * the untouched control the check relies on. */
    lgame_draw_text(900, -8, "clip", lgame_color_hex(SELFTEST_OPAQUE));

    /* Mutation probe, and the only one that tests the actual claim: draw the
     * texture UNTINTED again, after the tinted draw above. It must be
     * pixel-identical to block B. When the tint was applied in place, this
     * block came out green -- a *draw* call had permanently altered its
     * source, so a caller that blitted one sprite normally and then
     * highlighted it could never get the original back. */
    lgame_draw_texture(tex, SELFTEST_BLOCK * 6, 0);

    /* The 3D probe, below the texture blocks (viewport top is y=120). It is
     * drawn into the same frame on purpose: the two probes then share one
     * screendump, so a single capture covers both, and neither can pass by
     * drawing over the other. A failure here is fatal to the whole selftest
     * rather than skipped -- if the depth buffer cannot be allocated (it is
     * ~300 KB) the check must say so instead of quietly asserting less. */
    int rc3d = selftest_3d();
    if (rc3d != 0) {
        lgame_free_texture(tex);
        lgame_quit();
        return -20 + rc3d;   /* -31 if the viewport could not be opened */
    }

    lgame_present();

    /* Hold the frame until ESC.
     *
     * Without this the frame is presented and the function returns in the same
     * breath, so the shell repaints over it before scripts/lgame_check.py can
     * screendump. The check would then be judging whatever the shell happened
     * to draw, or a frame already cleared by lgame_quit(). Holding is also
     * what gives the check something real to assert: the surface stays exactly
     * as presented for as long as it takes.
     *
     * The marker is printed here, at present time, rather than by the caller
     * after the return: by the time the caller runs, lgame_quit() has already
     * cleared the screen, so a line printed then would vouch for a frame
     * nobody can see. */
    kprintf("lgame: selftest frame drawn\n");

    while (lgame.running) {
        lgame_input_poll();
        if (lgame_key_pressed(LGAME_KEY_ESC)) lgame.running = 0;
        timer_sleep_ms(16);
    }

    lgame_free_texture(tex);
    lgame_quit();
    return 0;
}
