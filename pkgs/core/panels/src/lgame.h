#ifndef LGAME_H
#define LGAME_H

#include <stdint.h>

#define LGAME_VERSION  "2.0.0"
#define LGAME_MAX_SPRITES  256
#define LGAME_MAX_SOUNDS    32
#define LGAME_MAX_KEYS      256

typedef enum {
    LGAME_OK = 0,
    LGAME_ERR_ALREADY_INIT,
    LGAME_ERR_NO_FB,
    LGAME_ERR_NOT_INIT,
    LGAME_ERR_INVALID_ARG
} lgame_error_t;

typedef struct {
    int width;
    int height;
    int bpp;
    int running;
    int initialized;
    uint64_t frame_start_ms;
    uint64_t last_frame_ms;
    uint64_t dt_ms;
    char title[64];
} lgame_context_t;

extern lgame_context_t lgame;

typedef struct {
    uint32_t r, g, b, a;
} lgame_color_t;

static inline lgame_color_t lgame_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    lgame_color_t c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}

static inline lgame_color_t lgame_color_hex(uint32_t hex) {
    lgame_color_t c;
    c.r = (hex >> 16) & 0xFF;
    c.g = (hex >> 8) & 0xFF;
    c.b = hex & 0xFF;
    c.a = (hex >> 24) & 0xFF;
    if (c.a == 0) c.a = 255;
    return c;
}

static inline uint32_t lgame_color_pack(lgame_color_t c) {
    return ((c.a << 24) | (c.r << 16) | (c.g << 8) | c.b);
}

/* ── Core ── */
int  lgame_init(int width, int height, const char *title);
void lgame_quit(void);
void lgame_present(void);
void lgame_clear(lgame_color_t color);
int  lgame_running(void);
void lgame_title(const char *title);

/* ── Timing ── */
uint64_t lgame_time_ms(void);
uint64_t lgame_delta_time_ms(void);
void     lgame_frame_begin(void);

/* ── 2D Rendering ── */
void lgame_draw_pixel(int x, int y, lgame_color_t color);
void lgame_draw_line(int x0, int y0, int x1, int y1, lgame_color_t color);
void lgame_draw_rect(int x, int y, int w, int h, lgame_color_t color);
void lgame_fill_rect(int x, int y, int w, int h, lgame_color_t color);
void lgame_draw_circle(int cx, int cy, int radius, lgame_color_t color);
void lgame_fill_circle(int cx, int cy, int radius, lgame_color_t color);
void lgame_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, lgame_color_t color);
void lgame_draw_text(int x, int y, const char *text, lgame_color_t color);
void lgame_draw_text_scaled(int x, int y, const char *text, lgame_color_t color, int scale);
int  lgame_text_width(const char *text);
int  lgame_text_width_scaled(const char *text, int scale);
int  lgame_screen_width(void);
int  lgame_screen_height(void);

/* ── Sprites / Textures ── */
typedef struct {
    int width;
    int height;
    uint32_t *pixels;
} lgame_texture_t;

lgame_texture_t *lgame_create_texture(int w, int h);
lgame_texture_t *lgame_create_texture_from_screen(int x, int y, int w, int h);
void lgame_free_texture(lgame_texture_t *tex);
void lgame_draw_texture(lgame_texture_t *tex, int x, int y);
void lgame_draw_texture_scaled(lgame_texture_t *tex, int x, int y, int scale);
void lgame_draw_texture_region(lgame_texture_t *tex, int sx, int sy, int sw, int sh, int x, int y);
void lgame_draw_texture_region_scaled(lgame_texture_t *tex, int sx, int sy, int sw, int sh,
                                      int x, int y, int scale);
void lgame_draw_texture_flipped(lgame_texture_t *tex, int x, int y, int flip_x, int flip_y);
void lgame_draw_texture_tinted(lgame_texture_t *tex, int x, int y, lgame_color_t tint);
void lgame_texture_put_pixel(lgame_texture_t *tex, int x, int y, lgame_color_t color);
lgame_color_t lgame_texture_get_pixel(lgame_texture_t *tex, int x, int y);

/* ── Audio ── */
int  lgame_play_sound(int freq, int duration_ms);
int  lgame_play_tone(int freq, int duration_ms, int volume);
int  lgame_play_beep(int freq, int duration_ms);
void lgame_audio_notify(void);
void lgame_audio_error(void);
void lgame_audio_success(void);

/* ── Input ── */
void lgame_input_poll(void);
int  lgame_key_down(int key);
int  lgame_key_pressed(int key);
int  lgame_mouse_x(void);
int  lgame_mouse_y(void);
int  lgame_mouse_dx(void);
int  lgame_mouse_dy(void);
int  lgame_mouse_button(int button);  /* 0=left, 1=right */

/* ── Math helpers ── */
int  lgame_rand(int min, int max);
void lgame_srand(uint32_t seed);

#endif
