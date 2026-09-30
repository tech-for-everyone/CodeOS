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
/* Target frames per second. lgame_frame_begin() paces the loop to this by
 * sleeping off the remainder of the frame, so a game that redraws as fast as
 * the blit allows does not peg a core. Set to 0 to disable pacing and get the
 * old uncapped behaviour. */
#define LGAME_TARGET_FPS 60
/* Hard ceiling on a single frame's delta, so a frame that was delayed (a
 * debugger, a mode switch, a long preemption) cannot teleport every moving
 * object across the screen on the next update. */
#define LGAME_MAX_DT_MS  100

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

/* ── 3D ── */
/* All 3D maths is 16.16 signed fixed point. Not a stylistic choice: the kernel
 * builds with -mno-sse, so there is no hardware float, and a soft-float
 * pipeline in a per-pixel loop is not affordable. Fixed point is also what
 * makes a frame reproducible, which the pixel assertions depend on -- the same
 * vertex must land on the same pixel every run, or a check would be asserting
 * against rounding noise. */
typedef int32_t lgame_fp_t;
#define LGAME_FP_SHIFT 16
#define LGAME_FP_ONE   (1 << LGAME_FP_SHIFT)
#define LGAME_FP_HALF  (1 << (LGAME_FP_SHIFT - 1))
/* Handy world constants, so callers never write a float literal. */
#define LGAME_FP_0     0
#define LGAME_FP_1     LGAME_FP_ONE
#define LGAME_FP_2     (2 * LGAME_FP_ONE)
#define LGAME_FP_4     (4 * LGAME_FP_ONE)
/* Convert a plain C float to 16.16. Compile-time use only -- a float *value*
 * in a hot loop defeats the whole point. */
#define LGAME_FP(f)    ((lgame_fp_t)((f) * (double)LGAME_FP_ONE))

typedef struct {
    lgame_fp_t x, y, z;
} lgame_vec3_t;

static inline lgame_vec3_t lgame_vec3(lgame_fp_t x, lgame_fp_t y, lgame_fp_t z) {
    lgame_vec3_t v; v.x = x; v.y = y; v.z = z; return v;
}

/* Depth buffer resolution. 16 bits is plenty: it is quantised on 1/z, which
 * spreads precision towards the near plane where it is needed. */
#define LGAME_3D_DEPTH_BITS 16

/* Open a 3D viewport `view_w` x `view_h` with its top-left at (ox, oy) on the
 * surface, and allocate its depth buffer. Returns LGAME_OK, or a negative
 * lgame_error_t. Nothing outside the viewport rect is touched, so the caller
 * still owns the rest of the screen (lgame_clear() for a sky, or draw there).
 *
 * The viewport is deliberately smaller than the surface: a software rasteriser
 * pays per pixel, and a 320x240 viewport is 76k pixels against 1M for the full
 * 1280x800 -- a 13x saving for no visible loss when the result is scaled up. */
int  lgame_3d_begin(int view_w, int view_h, int ox, int oy);
void lgame_3d_end(void);
/* 1 while a viewport is open. */
int  lgame_3d_active(void);

/* Look from `eye` at `target`. `focal` is the focal length in *pixels* rather
 * than a field of view, because that is the form the projection actually wants
 * and it avoids a tan() in a freestanding build. focal = view_h gives roughly a
 * 53-degree vertical field. Degenerate cases (eye == target) leave the previous
 * camera in place rather than producing a zero basis. */
void lgame_3d_camera(lgame_vec3_t eye, lgame_vec3_t target, lgame_fp_t focal);

/* Fill the viewport with `sky` and reset the depth buffer. */
void lgame_3d_clear(lgame_color_t sky);

/* Backface culling, off by default. The depth buffer alone resolves
 * visibility, so culling is purely an optimisation; leaving it off means a
 * caller with the wrong winding still gets a correct image, just slower. */
void lgame_3d_cull(int on);

/* Draw one flat-shaded triangle. Vertices are clipped against the near plane,
 * then rasterised with a depth test, so order of submission does not matter
 * and intersecting geometry resolves correctly. */
void lgame_3d_tri(lgame_vec3_t a, lgame_vec3_t b, lgame_vec3_t c, lgame_color_t col);

/* Counters, so a caller (and the selftest) can tell "nothing was drawn" from
 * "everything was culled or clipped away". */
int  lgame_3d_submitted(void);
int  lgame_3d_drawn(void);
int  lgame_3d_clipped(void);
int  lgame_3d_culled(void);
void lgame_3d_reset_counters(void);

/* ── Audio ── */
int  lgame_play_sound(int freq, int duration_ms);
int  lgame_play_tone(int freq, int duration_ms, int volume);
int  lgame_play_beep(int freq, int duration_ms);
void lgame_audio_notify(void);
void lgame_audio_error(void);
void lgame_audio_success(void);

/* ── Input ── */
/* Keys are raw byte values, not scan codes: the PS/2 layer maps to ASCII
 * before lgame sees it, and the serial drain delivers bytes as they arrive.
 * Both paths therefore agree on 27 for ESC, which is what every title polls. */
#define LGAME_KEY_ESC 27

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

/* ── Self test ── */
/* Renders a fixed, known frame and returns 0 on success, negative on the
 * first failure. Exists because most of this API is otherwise unreachable
 * from the shipped binary: the two games only use the rect, pixel and text
 * primitives, so --gc-sections discards the entire texture path along with
 * everything that calls it. A defect in code that no binary contains cannot
 * be runtime-verified, and a fix to it is only build-verified.
 *
 * scripts/lgame_check.py asserts the resulting framebuffer against the
 * colours named below, so this is a real pixel check, not a self-report.
 * The probe rectangles are laid out in the top-left of a 1280x800 surface and
 * each occupies a distinct 64x64 block; the test draws a half-transparent
 * quad over a solid backdrop so the blend result is a known value. */
int  lgame_selftest(void);

#endif
