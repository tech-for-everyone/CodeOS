#include "lgame.h"

/* Kernel driver headers */
#include "fb.h"
#include "audio.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "mm.h"
#include "string.h"

lgame_context_t lgame;

/* Internal state */
static uint8_t lgame_keys[LGAME_MAX_KEYS];
static uint8_t lgame_keys_prev[LGAME_MAX_KEYS];
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
    if (delta > 1000) delta = 16;
    lgame.dt_ms = delta;
    lgame.last_frame_ms = now;
    lgame.frame_start_ms = now;

    memcpy(lgame_keys_prev, lgame_keys, sizeof(lgame_keys));
    memcpy(lgame_mouse_btn_prev, lgame_mouse_btn_state, sizeof(lgame_mouse_btn_state));

    /* Start drawing into the back buffer for this frame */
    fb_backbuffer_begin();
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
    uint32_t gx = (uint32_t)(x < 0 ? 0 : x);
    while (*text) {
        const uint8_t *glyph = font8x16[(unsigned char)*text];
        for (int row = 0; row < 16; row++) {
            if ((uint32_t)(y + row * scale) >= fh) continue;
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (!((bits >> (7 - col)) & 1)) continue;
                uint32_t px = gx + (uint32_t)col * scale;
                if (px >= fw) continue;
                for (int sy = 0; sy < scale; sy++) {
                    uint32_t py = (uint32_t)y + (uint32_t)(row * scale + sy);
                    if (py >= fh) break;
                    for (int sx = 0; sx < scale; sx++) {
                        uint32_t qx = px + (uint32_t)sx;
                        if (qx >= fw) break;
                        buf[py * pitch + qx] = c;
                    }
                }
            }
        }
        gx += (uint32_t)(8 * scale);
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

void lgame_draw_texture(lgame_texture_t *tex, int x, int y) {
    if (!lgame.initialized || !tex || !tex->pixels) return;
    uint32_t *buf = fb_get_active_buffer();
    if (!buf) return;
    uint32_t fw = fb_getwidth();
    uint32_t fh = fb_getheight();
    uint32_t pitch = fw;
    for (int row = 0; row < tex->height; row++) {
        int py = y + row;
        if (py < 0 || (uint32_t)py >= fh) continue;
        for (int col = 0; col < tex->width; col++) {
            int px = x + col;
            if (px < 0 || (uint32_t)px >= fw) continue;
            uint32_t src = tex->pixels[(size_t)row * tex->width + col];
            uint32_t alpha = src >> 24;
            if (alpha == 255) {
                buf[(size_t)py * pitch + px] = src;
            } else if (alpha > 0) {
                uint32_t dst = buf[(size_t)py * pitch + px];
                uint32_t inv = 255 - alpha;
                uint32_t rb = ((src & 0xFF00FF) * alpha + (dst & 0xFF00FF) * inv) >> 8;
                uint32_t g  = ((src & 0x00FF00) * alpha + (dst & 0x00FF00) * inv) >> 8;
                buf[(size_t)py * pitch + px] = rb | g;
            }
        }
    }
}

void lgame_draw_texture_tinted(lgame_texture_t *tex, int x, int y, lgame_color_t tint) {
    if (!lgame.initialized || !tex) return;
    uint32_t t = lgame_color_pack(tint);
    uint32_t ta = (t >> 24) & 0xFF;
    uint32_t tr = (t >> 16) & 0xFF;
    uint32_t tg = (t >> 8) & 0xFF;
    uint32_t tb = t & 0xFF;

    for (int row = 0; row < tex->height; row++) {
        for (int col = 0; col < tex->width; col++) {
            uint32_t *px = &tex->pixels[(size_t)row * tex->width + col];
            uint32_t s = *px;
            uint32_t sr = (s >> 16) & 0xFF;
            uint32_t sg = (s >> 8) & 0xFF;
            uint32_t sb = s & 0xFF;
            sr = (sr * tr) / 255;
            sg = (sg * tg) / 255;
            sb = (sb * tb) / 255;
            *px = (ta << 24) | (sr << 16) | (sg << 8) | sb;
        }
    }
    lgame_draw_texture(tex, x, y);
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
            uint32_t alpha = src >> 24;
            for (int sy01 = 0; sy01 < scale; sy01++) {
                uint32_t qy = (uint32_t)py + (uint32_t)sy01;
                if (qy >= fh) break;
                for (int sx01 = 0; sx01 < scale; sx01++) {
                    uint32_t qx = (uint32_t)px0 + (uint32_t)sx01;
                    if (qx >= fw) break;
                    if (alpha == 255) {
                        buf[qy * pitch + qx] = src;
                    } else if (alpha > 0) {
                        uint32_t dst = buf[qy * pitch + qx];
                        uint32_t inv = 255 - alpha;
                        uint32_t rb = ((src & 0xFF00FF) * alpha + (dst & 0xFF00FF) * inv) >> 8;
                        uint32_t g  = ((src & 0x00FF00) * alpha + (dst & 0x00FF00) * inv) >> 8;
                        buf[qy * pitch + qx] = rb | g;
                    }
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
            uint32_t src = tex->pixels[(size_t)row * tex->width + col];
            uint32_t alpha = src >> 24;
            if (alpha == 255) {
                buf[(size_t)py * pitch + px] = src;
            } else if (alpha > 0) {
                uint32_t dst = buf[(size_t)py * pitch + px];
                uint32_t inv = 255 - alpha;
                uint32_t rb = ((src & 0xFF00FF) * alpha + (dst & 0xFF00FF) * inv) >> 8;
                uint32_t g  = ((src & 0x00FF00) * alpha + (dst & 0x00FF00) * inv) >> 8;
                buf[(size_t)py * pitch + px] = rb | g;
            }
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
    keyboard_poll();

    key_event_t ev;
    while (keyboard_get_event(&ev)) {
        int key = ev.keycode;
        if (key >= 0 && key < LGAME_MAX_KEYS) {
            lgame_keys[key] = ev.pressed ? 1 : 0;
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
