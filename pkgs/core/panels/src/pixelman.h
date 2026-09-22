#ifndef PIXELMAN_H
#define PIXELMAN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════════════
 * PixelMan — Pixman-compatible pixel compositing library for CodeOS
 *
 * API mirrors Pixman (libpixman) with naming prefix pixelman_ instead of pixman_.
 * Extensions for CodeOS: double-buffer, damage tracking, glyph cache.
 * ══════════════════════════════════════════════════════════════════════════════ */

/* ─── Fixed-point types (Pixman-compatible) ─── */
typedef int64_t pixelman_fixed_t;
#define PIXELMAN_FIXED_ONE (1LL << 16)

/* ─── Format codes ─── */
typedef enum {
    PIXELMAN_a8r8g8b8    = 0x02000000,
    PIXELMAN_x8r8g8b8    = 0x02000001,
    PIXELMAN_a8b8g8r8    = 0x02000002,
    PIXELMAN_x8b8g8r8    = 0x02000003,
    PIXELMAN_a8          = 0x03000000,
    PIXELMAN_r5g6b5      = 0x01000000,
    PIXELMAN_a1          = 0x04000000,
} pixelman_format_code_t;

/* ─── Compositing operators ─── */
typedef enum {
    PIXELMAN_OP_SRC            = 1,
    PIXELMAN_OP_OVER           = 2,
    PIXELMAN_OP_ADD            = 3,
    PIXELMAN_OP_SUBTRACT       = 4,
    PIXELMAN_OP_REVERSE        = 5,
    PIXELMAN_OP_MULTIPLY       = 6,
    PIXELMAN_OP_SCREEN         = 7,
    PIXELMAN_OP_DIFFERENCE     = 8,
    PIXELMAN_OP_EXCLUSION      = 9,
    PIXELMAN_OP_IN             = 10,
    PIXELMAN_OP_OUT            = 11,
    PIXELMAN_OP_ATOP           = 12,
    PIXELMAN_OP_XOR            = 13,
    PIXELMAN_OP_SATURATE       = 14,
    PIXELMAN_OP_DISJUNCT_OVER  = 15,
    PIXELMAN_OP_CONJUNCT_OVER  = 16,
    PIXELMAN_OP_CLEAR          = 17,
} pixelman_op_t;

/* ─── Repeat modes ─── */
typedef enum {
    PIXELMAN_REPEAT_NONE      = 0,
    PIXELMAN_REPEAT_NORMAL    = 1,
    PIXELMAN_REPEAT_REFLECT   = 2,
    PIXELMAN_REPEAT_PAD       = 3,
} pixelman_repeat_t;

/* ─── Filter types ─── */
typedef enum {
    PIXELMAN_FILTER_FAST     = 0,
    PIXELMAN_FILTER_GOOD     = 1,
    PIXELMAN_FILTER_BEST     = 2,
    PIXELMAN_FILTER_NEAREST  = 3,
    PIXELMAN_FILTER_BILINEAR = 4,
} pixelman_filter_t;

/* ─── Gradient stop ─── */
typedef struct {
    pixelman_fixed_t x;
    uint16_t         index;
    uint16_t         color;
} pixelman_gradient_stop_t;

/* ─── Fixed-point vector ─── */
typedef struct {
    pixelman_fixed_t v[3];
} pixelman_vector_t;

/* ─── Transform (Pixman-compatible) ─── */
typedef struct {
    pixelman_fixed_t matrix[3][3];
} pixelman_transform_t;

static inline void pixelman_transform_init_identity(pixelman_transform_t *t) {
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            t->matrix[i][j] = (i == j) ? PIXELMAN_FIXED_ONE : 0;
}

static inline void pixelman_transform_init_translate(pixelman_transform_t *t,
                                                      pixelman_fixed_t tx,
                                                      pixelman_fixed_t ty) {
    pixelman_transform_init_identity(t);
    t->matrix[0][2] = tx;
    t->matrix[1][2] = ty;
}

static inline void pixelman_transform_init_scale(pixelman_transform_t *t,
                                                  pixelman_fixed_t sx,
                                                  pixelman_fixed_t sy) {
    pixelman_transform_init_identity(t);
    t->matrix[0][0] = sx;
    t->matrix[1][1] = sy;
}

/* ─── Color ─── */
typedef struct {
    uint16_t a, r, g, b;
} pixelman_color_t;

/* ─── Rectangle / Region ─── */
typedef struct {
    int32_t x1, y1, x2, y2;
} pixelman_rect_t;

#define PIXELMAN_RECT_INIT (pixelman_rect_t){0,0,0,0}

static inline int pixelman_rect_empty(pixelman_rect_t r) {
    return r.x1 >= r.x2 || r.y1 >= r.y2;
}
static inline int pixelman_rect_width(pixelman_rect_t r) {
    return r.x2 - r.x1;
}
static inline int pixelman_rect_height(pixelman_rect_t r) {
    return r.y2 - r.y1;
}
static inline uint32_t pixelman_rect_area(pixelman_rect_t r) {
    return (uint32_t)(pixelman_rect_width(r) * pixelman_rect_height(r));
}

static inline pixelman_rect_t pixelman_rect_intersect(pixelman_rect_t a,
                                                       pixelman_rect_t b) {
    pixelman_rect_t r = {
        a.x1 > b.x1 ? a.x1 : b.x1,
        a.y1 > b.y1 ? a.y1 : b.y1,
        a.x2 < b.x2 ? a.x2 : b.x2,
        a.y2 < b.y2 ? a.y2 : b.y2,
    };
    if (r.x1 >= r.x2 || r.y1 >= r.y2) r = (pixelman_rect_t){0,0,0,0};
    return r;
}

static inline int pixelman_rect_contains(pixelman_rect_t r, int x, int y) {
    return x >= r.x1 && x < r.x2 && y >= r.y1 && y < r.y2;
}

/* ─── Region (linked list of rectangles) ─── */
typedef struct pixelman_box32 {
    int32_t x1, y1, x2, y2;
    struct pixelman_box32 *next;
} pixelman_box32_t;

typedef struct {
    pixelman_box32_t *boxes;
    pixelman_rect_t   extents;
    int               count;
} pixelman_region32_t;

/* ─── Image type (opaque) ─── */
typedef struct pixelman_image pixelman_image_t;

/* ─── Double buffer ─── */
typedef struct {
    pixelman_image_t *front;
    pixelman_image_t *back;
    int               width;
    int               height;
    int               stride;
    uint32_t         *front_data;
    uint32_t         *back_data;
    int               needs_swap;
} pixelman_buffer_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * Image creation / destruction
 * ══════════════════════════════════════════════════════════════════════════════ */

pixelman_image_t *pixelman_image_create_bits(pixelman_format_code_t format,
                                              int width, int height,
                                              uint32_t *bits, int stride);

pixelman_image_t *pixelman_image_create_solid_fill(pixelman_color_t *color);

pixelman_image_t *pixelman_image_create_linear_gradient(pixelman_vector_t *p1,
                                                         pixelman_vector_t *p2,
                                                         pixelman_gradient_stop_t *stops,
                                                         int n_stops);

pixelman_image_t *pixelman_image_create_radial_gradient(pixelman_vector_t *inner,
                                                         pixelman_vector_t *outer,
                                                         pixelman_fixed_t inner_r,
                                                         pixelman_fixed_t outer_r,
                                                         pixelman_gradient_stop_t *stops,
                                                         int n_stops);

pixelman_image_t *pixelman_image_ref(pixelman_image_t *img);
void              pixelman_image_unref(pixelman_image_t *img);

void *pixelman_image_get_data(pixelman_image_t *img);
int   pixelman_image_get_stride(pixelman_image_t *img);
int   pixelman_image_get_width(pixelman_image_t *img);
int   pixelman_image_get_height(pixelman_image_t *img);
pixelman_format_code_t pixelman_image_get_format(pixelman_image_t *img);

/* ══════════════════════════════════════════════════════════════════════════════
 * Image properties
 * ══════════════════════════════════════════════════════════════════════════════ */

void pixelman_image_set_clip_region(pixelman_image_t *img,
                                     pixelman_region32_t *region);
void pixelman_image_set_has_clip(pixelman_image_t *img, int has_clip);
void pixelman_image_set_repeat(pixelman_image_t *img, pixelman_repeat_t repeat);
void pixelman_image_set_filter(pixelman_image_t *img, pixelman_filter_t filter);
void pixelman_image_set_transform(pixelman_image_t *img,
                                   pixelman_transform_t *transform);
void pixelman_image_set_component_alpha(pixelman_image_t *img, int enabled);
void pixelman_image_set_destroy_function(pixelman_image_t *img,
                                          void (*fn)(pixelman_image_t *, void *),
                                          void *data);

/* ══════════════════════════════════════════════════════════════════════════════
 * Compositing
 * ══════════════════════════════════════════════════════════════════════════════ */

void pixelman_image_composite(pixelman_op_t      op,
                               pixelman_image_t  *src,
                               pixelman_image_t  *mask,
                               pixelman_image_t  *dst,
                               int16_t            src_x,
                               int16_t            src_y,
                               int16_t            mask_x,
                               int16_t            mask_y,
                               int16_t            dst_x,
                               int16_t            dst_y,
                               uint16_t           width,
                               uint16_t           height);

void pixelman_image_fill_rects(pixelman_op_t       op,
                                pixelman_image_t   *dst,
                                pixelman_color_t   *color,
                                int                 n_rects,
                                pixelman_rect_t    *rects);

void pixelman_fill(pixelman_image_t *dst,
                    uint32_t          color,
                    int               x, int y,
                    int               w, int h);

void pixelman_image_composite32(pixelman_op_t      op,
                                 pixelman_image_t  *src,
                                 pixelman_image_t  *mask,
                                 pixelman_image_t  *dst,
                                 int32_t            src_x,
                                 int32_t            src_y,
                                 int32_t            mask_x,
                                 int32_t            mask_y,
                                 int32_t            dst_x,
                                 int32_t            dst_y,
                                 int32_t            width,
                                 int32_t            height);

/* ══════════════════════════════════════════════════════════════════════════════
 * Legacy pm_* API compatibility wrappers (inline)
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef pixelman_rect_t pm_rect_t;
#define PM_RECT_INIT PIXELMAN_RECT_INIT
#define pm_rect_empty   pixelman_rect_empty
#define pm_rect_width   pixelman_rect_width
#define pm_rect_height  pixelman_rect_height
#define pm_rect_area    pixelman_rect_area
#define pm_rect_intersect pixelman_rect_intersect
#define pm_rect_contains  pixelman_rect_contains

#define PM_MAX_DAMAGE_RECTS 64
typedef struct {
    pm_rect_t rects[PM_MAX_DAMAGE_RECTS];
    int count;
} pm_damage_t;

/* ─── Legacy pm_* drawing wrappers (call pixelman internally) ─── */

void pm_composite_rect(uint32_t *buf, int stride, pm_rect_t clip,
                        int x, int y, int w, int h, uint32_t color);
void pm_composite_rect_alpha(uint32_t *buf, int stride, pm_rect_t clip,
                              int x, int y, int w, int h,
                              uint32_t color, uint8_t alpha);
void pm_composite_glyph(uint32_t *buf, int stride, pm_rect_t clip,
                         int x, int y, const uint8_t *glyph, int gw, int gh,
                         uint32_t fg, uint32_t bg);
void pm_composite_hline(uint32_t *buf, int stride, pm_rect_t clip,
                         int x, int y, int w, uint32_t color);
void pm_composite_vline(uint32_t *buf, int stride, pm_rect_t clip,
                         int x, int y, int h, uint32_t color);
void pm_composite_line(uint32_t *buf, int stride, pm_rect_t clip,
                        int x1, int y1, int x2, int y2, uint32_t color);
void pm_composite_fill_rounded_rect(uint32_t *buf, int stride, pm_rect_t clip,
                                     int x, int y, int w, int h,
                                     int r, uint32_t color);
void pm_composite_fill_rounded_rect_gradient_v(uint32_t *buf, int stride,
                                                pm_rect_t clip,
                                                int x, int y, int w, int h,
                                                int r,
                                                uint32_t color_top,
                                                uint32_t color_bot);
void pm_composite_draw_rounded_rect(uint32_t *buf, int stride, pm_rect_t clip,
                                     int x, int y, int w, int h,
                                     int r, uint32_t color);
void pm_fill_circle(uint32_t *buf, int stride, pm_rect_t clip,
                     int cx, int cy, int r, uint32_t color);
void pm_composite_fill_rect_gradient_v(uint32_t *buf, int stride, pm_rect_t clip,
                                        int x, int y, int w, int h,
                                        uint32_t color_top, uint32_t color_bot);
void pm_composite_fill_rect_gradient_h(uint32_t *buf, int stride, pm_rect_t clip,
                                        int x, int y, int w, int h,
                                        uint32_t color_left,
                                        uint32_t color_right);
void pm_composite_fill_rect_gradient_d(uint32_t *buf, int stride, pm_rect_t clip,
                                        int x, int y, int w, int h,
                                        uint32_t color_tl, uint32_t color_br);
void pm_composite_shadow(uint32_t *buf, int stride, pm_rect_t clip,
                          int x, int y, int w, int h, int radius,
                          uint8_t alpha, int offset, int layers);
void pm_composite_image(uint32_t *buf, int stride, pm_rect_t clip,
                         int dx, int dy,
                         const uint32_t *src, int src_w, int src_h,
                         int src_stride);
void pm_composite_image_reflected(uint32_t *buf, int stride, pm_rect_t clip,
                                   int dx, int dy,
                                   const uint32_t *src, int src_w, int src_h,
                                   int src_stride,
                                   int reflect_height, uint8_t max_alpha);

/* ══════════════════════════════════════════════════════════════════════════════
 * Double-buffer API
 * ══════════════════════════════════════════════════════════════════════════════ */

pixelman_buffer_t *pixelman_buffer_create(int width, int height, int stride);
void               pixelman_buffer_destroy(pixelman_buffer_t *buf);
void               pixelman_buffer_begin(pixelman_buffer_t *buf);
void               pixelman_buffer_end(pixelman_buffer_t *buf);
pixelman_image_t  *pixelman_buffer_get_back(pixelman_buffer_t *buf);
pixelman_image_t  *pixelman_buffer_get_front(pixelman_buffer_t *buf);
uint32_t          *pixelman_buffer_get_data(pixelman_buffer_t *buf);
int                pixelman_buffer_get_stride(pixelman_buffer_t *buf);

/* ══════════════════════════════════════════════════════════════════════════════
 * Damage tracking
 * ══════════════════════════════════════════════════════════════════════════════ */

void pm_damage_init(pm_damage_t *dmg);
void pm_damage_add(pm_damage_t *dmg, pm_rect_t r);
void pm_damage_clear(pm_damage_t *dmg);
int  pm_damage_empty(pm_damage_t *dmg);

/* ─── Utility ─── */

static inline uint32_t pixelman_argb32(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline uint8_t pixelman_alpha32(uint32_t c) { return (c >> 24) & 0xFF; }
static inline uint8_t pixelman_red32(uint32_t c)   { return (c >> 16) & 0xFF; }
static inline uint8_t pixelman_green32(uint32_t c) { return (c >> 8) & 0xFF; }
static inline uint8_t pixelman_blue32(uint32_t c)  { return c & 0xFF; }

static inline uint32_t pixelman_blend_over(uint32_t src, uint32_t dst) {
    uint8_t sa = pixelman_alpha32(src);
    if (sa == 0) return dst;
    if (sa == 255) return src;
    uint8_t sr = pixelman_red32(src), sg = pixelman_green32(src), sb = pixelman_blue32(src);
    uint8_t dr = pixelman_red32(dst), dg = pixelman_green32(dst), db = pixelman_blue32(dst);
    uint8_t inv = 255 - sa;
    return pixelman_argb32(255,
        (sr * sa + dr * inv + 127) / 255,
        (sg * sa + dg * inv + 127) / 255,
        (sb * sa + db * inv + 127) / 255);
}

static inline uint32_t pixelman_lerp_color(uint32_t a, uint32_t b, uint8_t t) {
    if (t == 0 || t == 255) return t == 0 ? a : b;
    int inv = 255 - t;
    return pixelman_argb32(255,
        (pixelman_red32(a) * inv + pixelman_red32(b) * t + 127) / 255,
        (pixelman_green32(a) * inv + pixelman_green32(b) * t + 127) / 255,
        (pixelman_blue32(a) * inv + pixelman_blue32(b) * t + 127) / 255);
}

#ifdef __cplusplus
}
#endif

#endif /* PIXELMAN_H */
