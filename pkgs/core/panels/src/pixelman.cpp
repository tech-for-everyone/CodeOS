extern "C" {
#include "pixelman.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "kprintf.h"
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ══════════════════════════════════════════════════════════════════════════════ */

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ─── Image struct (opaque in header) ─── */

struct pixelman_image {
    pixelman_format_code_t format;
    int                    width;
    int                    height;
    int                    stride;       /* in bytes */
    uint32_t              *data;         /* pixel data (may be 0 for solid/gradient) */

    /* Solid fill color */
    pixelman_color_t       solid_color;

    /* Gradient */
    int                    is_linear_gradient;
    int                    is_radial_gradient;
    pixelman_vector_t      grad_p1, grad_p2;
    pixelman_fixed_t       grad_inner_r, grad_outer_r;
    pixelman_gradient_stop_t *grad_stops;
    int                    grad_n_stops;

    /* Transform, repeat, filter */
    pixelman_transform_t   transform;
    int                    has_transform;
    pixelman_repeat_t      repeat;
    pixelman_filter_t      filter;

    /* Clip */
    int                    has_clip;
    pixelman_region32_t    clip_region;

    /* Component alpha */
    int                    component_alpha;

    /* Ref count */
    int                    ref_count;

    /* Destroy callback */
    void (*destroy_fn)(pixelman_image_t *, void *);
    void *destroy_data;

    /* Whether this image owns its data (for freeing) */
    int                    owns_data;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * Blending helpers
 * ══════════════════════════════════════════════════════════════════════════════ */

static inline uint32_t blend_coverage(uint32_t color, uint32_t dst, uint8_t cov) {
    if (cov == 0) return dst;
    uint8_t sa = pixelman_alpha32(color);
    uint16_t eff = (uint16_t)sa * cov / 255;
    if (eff == 0) return dst;
    if (eff >= 254) return color;
    uint8_t sr = pixelman_red32(color), sg = pixelman_green32(color), sb = pixelman_blue32(color);
    uint8_t dr = pixelman_red32(dst), dg = pixelman_green32(dst), db = pixelman_blue32(dst);
    uint8_t inv = 255 - (uint8_t)eff;
    return pixelman_argb32(255,
        (sr * eff + dr * inv + 127) / 255,
        (sg * eff + dg * inv + 127) / 255,
        (sb * eff + db * inv + 127) / 255);
}

static inline uint32_t blend_op(pixelman_op_t op, uint32_t src, uint32_t dst) {
    uint8_t sa = pixelman_alpha32(src), sr = pixelman_red32(src);
    uint8_t sg = pixelman_green32(src), sb = pixelman_blue32(src);
    uint8_t da = pixelman_alpha32(dst), dr = pixelman_red32(dst);
    uint8_t dg = pixelman_green32(dst), db = pixelman_blue32(dst);

    switch (op) {
    case PIXELMAN_OP_SRC:
        return src;
    case PIXELMAN_OP_OVER: {
        if (sa == 0) return dst;
        if (sa == 255) return src;
        uint8_t inv = 255 - sa;
        return pixelman_argb32(255,
            (sr * sa + dr * inv + 127) / 255,
            (sg * sa + dg * inv + 127) / 255,
            (sb * sa + db * inv + 127) / 255);
    }
    case PIXELMAN_OP_ADD: {
        return pixelman_argb32(255,
            imin(sr + dr, 255), imin(sg + dg, 255), imin(sb + db, 255));
    }
    case PIXELMAN_OP_SUBTRACT: {
        int rr = (int)sr - dr, rg = (int)sg - dg, rb = (int)sb - db;
        return pixelman_argb32(255,
            rr < 0 ? 0 : rr, rg < 0 ? 0 : rg, rb < 0 ? 0 : rb);
    }
    case PIXELMAN_OP_MULTIPLY: {
        return pixelman_argb32(255,
            sr * dr / 255, sg * dg / 255, sb * db / 255);
    }
    case PIXELMAN_OP_SCREEN: {
        return pixelman_argb32(255,
            255 - (255 - sr) * (255 - dr) / 255,
            255 - (255 - sg) * (255 - dg) / 255,
            255 - (255 - sb) * (255 - db) / 255);
    }
    case PIXELMAN_OP_DIFFERENCE: {
        return pixelman_argb32(255,
            sr > dr ? sr - dr : dr - sr,
            sg > dg ? sg - dg : dg - sg,
            sb > db ? sb - db : db - sb);
    }
    case PIXELMAN_OP_XOR: {
        return pixelman_argb32(255,
            (sr ^ dr), (sg ^ dg), (sb ^ db));
    }
    case PIXELMAN_OP_CLEAR:
        return 0;
    case PIXELMAN_OP_REVERSE: {
        uint8_t da_inv = 255 - da;
        return pixelman_argb32(255,
            (sr * da_inv + dr * sa + 127) / 255,
            (sg * da_inv + dg * sa + 127) / 255,
            (sb * da_inv + db * sa + 127) / 255);
    }
    default:
        return src;
    }
}

static inline uint8_t corner_coverage(int dist_sq, int r) {
    int inner = r * r - r;
    int outer = r * r + r;
    if (dist_sq <= inner) return 255;
    if (dist_sq >= outer) return 0;
    int range = outer - inner;
    int offset = outer - dist_sq;
    return (uint8_t)((offset * 255 + range / 2) / range);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Image creation / destruction
 * ══════════════════════════════════════════════════════════════════════════════ */

pixelman_image_t *pixelman_image_create_bits(pixelman_format_code_t format,
                                              int width, int height,
                                              uint32_t *bits, int stride) {
    if (width <= 0 || height <= 0) return 0;
    uint64_t img_phys = pmm_alloc_page();
    if (!img_phys) return 0;
    pixelman_image_t *img = (pixelman_image_t*)phys_to_virt(img_phys);
    memset(img, 0, sizeof(pixelman_image_t));

    img->format = format;
    img->width  = width;
    img->height = height;
    img->ref_count = 1;

    if (bits) {
        img->data   = bits;
        img->stride = stride;
        img->owns_data = 0;
    } else {
        int bpp = 32;
        int row_bytes = width * bpp / 8;
        if (stride > 0 && stride >= row_bytes) stride = row_bytes;
        row_bytes = (row_bytes + 15) & ~15;
        int size = row_bytes * height;
        int pages = (size + 0xFFF) / 0x1000;
        if (pages < 1) pages = 1;
        uint64_t phys = pmm_alloc_pages(pages);
        if (!phys) {
            pmm_free_page(img_phys);
            return 0;
        }
        img->data = (uint32_t*)phys_to_virt(phys);
        memset(img->data, 0, size);
        img->stride   = row_bytes;
        img->owns_data = 1;
    }

    pixelman_transform_init_identity(&img->transform);
    img->repeat  = PIXELMAN_REPEAT_NONE;
    img->filter  = PIXELMAN_FILTER_NEAREST;
    return img;
}

pixelman_image_t *pixelman_image_create_solid_fill(pixelman_color_t *color) {
    uint64_t img_phys = pmm_alloc_page();
    if (!img_phys) return 0;
    pixelman_image_t *img = (pixelman_image_t*)phys_to_virt(img_phys);
    memset(img, 0, sizeof(pixelman_image_t));

    img->format       = PIXELMAN_a8r8g8b8;
    img->width        = 1;
    img->height       = 1;
    img->data         = 0;
    img->solid_color  = *color;
    img->ref_count    = 1;
    img->repeat       = PIXELMAN_REPEAT_NORMAL;
    pixelman_transform_init_identity(&img->transform);
    return img;
}

pixelman_image_t *pixelman_image_create_linear_gradient(pixelman_vector_t *p1,
                                                         pixelman_vector_t *p2,
                                                         pixelman_gradient_stop_t *stops,
                                                         int n_stops) {
    uint64_t img_phys = pmm_alloc_page();
    if (!img_phys) return 0;
    pixelman_image_t *img = (pixelman_image_t*)phys_to_virt(img_phys);
    memset(img, 0, sizeof(pixelman_image_t));

    img->format          = PIXELMAN_a8r8g8b8;
    img->width           = 1;
    img->height          = 1;
    img->data            = 0;
    img->ref_count       = 1;
    img->is_linear_gradient = 1;
    img->grad_p1         = *p1;
    img->grad_p2         = *p2;
    img->grad_n_stops    = n_stops;
    img->repeat          = PIXELMAN_REPEAT_NORMAL;
    pixelman_transform_init_identity(&img->transform);

    if (n_stops > 0) {
        uint64_t stops_phys = pmm_alloc_page();
        if (stops_phys) {
            img->grad_stops = (pixelman_gradient_stop_t*)phys_to_virt(stops_phys);
            int copy = n_stops * sizeof(pixelman_gradient_stop_t);
            if (copy > 4096) copy = 4096;
            memcpy(img->grad_stops, stops, copy);
        }
    }
    return img;
}

pixelman_image_t *pixelman_image_create_radial_gradient(pixelman_vector_t *inner,
                                                         pixelman_vector_t *outer,
                                                         pixelman_fixed_t inner_r,
                                                         pixelman_fixed_t outer_r,
                                                         pixelman_gradient_stop_t *stops,
                                                         int n_stops) {
    uint64_t img_phys = pmm_alloc_page();
    if (!img_phys) return 0;
    pixelman_image_t *img = (pixelman_image_t*)phys_to_virt(img_phys);
    memset(img, 0, sizeof(pixelman_image_t));

    img->format             = PIXELMAN_a8r8g8b8;
    img->width              = 1;
    img->height             = 1;
    img->data               = 0;
    img->ref_count          = 1;
    img->is_radial_gradient = 1;
    img->grad_p1            = *inner;
    img->grad_p2            = *outer;
    img->grad_inner_r       = inner_r;
    img->grad_outer_r       = outer_r;
    img->grad_n_stops       = n_stops;
    img->repeat             = PIXELMAN_REPEAT_NORMAL;
    pixelman_transform_init_identity(&img->transform);

    if (n_stops > 0) {
        uint64_t stops_phys = pmm_alloc_page();
        if (stops_phys) {
            img->grad_stops = (pixelman_gradient_stop_t*)phys_to_virt(stops_phys);
            int copy = n_stops * sizeof(pixelman_gradient_stop_t);
            if (copy > 4096) copy = 4096;
            memcpy(img->grad_stops, stops, copy);
        }
    }
    return img;
}

pixelman_image_t *pixelman_image_ref(pixelman_image_t *img) {
    if (!img) return 0;
    img->ref_count++;
    return img;
}

void pixelman_image_unref(pixelman_image_t *img) {
    if (!img) return;
    if (img->ref_count <= 1) {
        if (img->destroy_fn)
            img->destroy_fn(img, img->destroy_data);
        if (img->owns_data && img->data) {
            pmm_free_pages(virt_to_phys((uint64_t)img->data),
                           (img->height * img->stride + 0xFFF) / 0x1000);
        }
        if (img->grad_stops)
            pmm_free_page(virt_to_phys((uint64_t)img->grad_stops));
        pmm_free_page(virt_to_phys((uint64_t)img));
    } else {
        img->ref_count--;
    }
}

void *pixelman_image_get_data(pixelman_image_t *img) {
    return img ? img->data : 0;
}
int pixelman_image_get_stride(pixelman_image_t *img) {
    return img ? img->stride : 0;
}
int pixelman_image_get_width(pixelman_image_t *img) {
    return img ? img->width : 0;
}
int pixelman_image_get_height(pixelman_image_t *img) {
    return img ? img->height : 0;
}
pixelman_format_code_t pixelman_image_get_format(pixelman_image_t *img) {
    return img ? img->format : (pixelman_format_code_t)0;
}

/* ─── Property setters ─── */

void pixelman_image_set_clip_region(pixelman_image_t *img,
                                     pixelman_region32_t *region) {
    if (!img) return;
    if (region) {
        img->has_clip = 1;
        img->clip_region = *region;
    } else {
        img->has_clip = 0;
    }
}

void pixelman_image_set_has_clip(pixelman_image_t *img, int has_clip) {
    if (img) img->has_clip = has_clip;
}

void pixelman_image_set_repeat(pixelman_image_t *img, pixelman_repeat_t repeat) {
    if (img) img->repeat = repeat;
}

void pixelman_image_set_filter(pixelman_image_t *img, pixelman_filter_t filter) {
    if (img) img->filter = filter;
}

void pixelman_image_set_transform(pixelman_image_t *img,
                                   pixelman_transform_t *transform) {
    if (!img) return;
    if (transform) {
        img->transform = *transform;
        img->has_transform = 1;
    } else {
        img->has_transform = 0;
    }
}

void pixelman_image_set_component_alpha(pixelman_image_t *img, int enabled) {
    if (img) img->component_alpha = enabled;
}

void pixelman_image_set_destroy_function(pixelman_image_t *img,
                                          void (*fn)(pixelman_image_t *, void *),
                                          void *data) {
    if (!img) return;
    img->destroy_fn  = fn;
    img->destroy_data = data;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Sampler: get pixel from any image type at logical (x, y)
 * ══════════════════════════════════════════════════════════════════════════════ */

static uint32_t sample_image(pixelman_image_t *img, int x, int y) {
    if (!img) return 0;

    /* Solid fill */
    if (!img->data && !img->is_linear_gradient && !img->is_radial_gradient) {
        pixelman_color_t c = img->solid_color;
        return pixelman_argb32(c.a >> 8, c.r >> 8, c.g >> 8, c.b >> 8);
    }

    /* Gradient sampling is done in composite — for per-pixel use, just use a simple gradient */
    if (img->is_linear_gradient || img->is_radial_gradient) {
        return 0xFF888888; /* fallback gray */
    }

    /* Bitmap image */
    if (!img->data) return 0;

    int w = img->width, h = img->height;
    /* Apply repeat */
    switch (img->repeat) {
    case PIXELMAN_REPEAT_NORMAL:
        x %= w; if (x < 0) x += w;
        y %= h; if (y < 0) y += h;
        break;
    case PIXELMAN_REPEAT_REFLECT: {
        int d;
        d = x / w; x %= w; if (x < 0) x += w; if (d & 1) x = w - 1 - x;
        d = y / h; y %= h; if (y < 0) y += h; if (d & 1) y = h - 1 - y;
        break;
    }
    case PIXELMAN_REPEAT_PAD:
        if (x < 0) x = 0;
        if (x >= w) x = w - 1;
        if (y < 0) y = 0;
        if (y >= h) y = h - 1;
        break;
    case PIXELMAN_REPEAT_NONE:
    default:
        if (x < 0 || x >= w || y < 0 || y >= h) return 0;
        break;
    }

    int stride_words = img->stride / 4;
    return img->data[y * stride_words + x];
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Gradient evaluation
 * ══════════════════════════════════════════════════════════════════════════════ */

static uint32_t eval_linear_gradient(pixelman_image_t *img,
                                      pixelman_fixed_t fx, pixelman_fixed_t fy) {
    if (!img->grad_n_stops) return 0;
    pixelman_fixed_t dx = img->grad_p2.v[0] - img->grad_p1.v[0];
    pixelman_fixed_t dy = img->grad_p2.v[1] - img->grad_p1.v[1];
    pixelman_fixed_t len_sq = dx * dx / PIXELMAN_FIXED_ONE * dx +
                              dy * dy / PIXELMAN_FIXED_ONE * dy;
    if (len_sq == 0)
    {
        pixelman_color_t c;
        c.a = (img->grad_stops[0].color >> 8) & 0xFF;
        c.a |= c.a << 8;
        c.r = (img->grad_stops[0].color >> 0) & 0xFF;
        c.r |= c.r << 8;
        c.g = (img->grad_stops[0].color >> 8) & 0xFF;
        c.g |= c.g << 8;
        c.b = (img->grad_stops[0].color >> 16) & 0xFF;
        c.b |= c.b << 8;
        return pixelman_argb32(c.a >> 8, c.r >> 8, c.g >> 8, c.b >> 8);
    }
    pixelman_fixed_t t = ((fx - img->grad_p1.v[0]) * dx / PIXELMAN_FIXED_ONE +
                          (fy - img->grad_p1.v[1]) * dy / PIXELMAN_FIXED_ONE) / len_sq;

    if (t < 0) t = 0;
    if (t > PIXELMAN_FIXED_ONE) t = PIXELMAN_FIXED_ONE;

    /* Find stops */
    int i;
    for (i = 0; i < img->grad_n_stops - 1; i++) {
        if (img->grad_stops[i + 1].x >= t) break;
    }
    if (i >= img->grad_n_stops - 1) {
        uint16_t c = img->grad_stops[img->grad_n_stops - 1].color;
        return pixelman_argb32((c >> 8) & 0xFF, c & 0xFF,
                               (c >> 8) & 0xFF, (c >> 16) & 0xFF);
    }

    pixelman_fixed_t t0 = img->grad_stops[i].x;
    pixelman_fixed_t t1 = img->grad_stops[i + 1].x;
    int64_t frac = (t - t0) * 255 / (t1 - t0);
    if (frac < 0) frac = 0;
    if (frac > 255) frac = 255;
    uint8_t f = (uint8_t)frac;

    uint32_t ca = img->grad_stops[i].color;
    uint32_t cb = img->grad_stops[i + 1].color;
    uint8_t ar = (ca >> 0) & 0xFF, ag = (ca >> 8) & 0xFF, ab = (ca >> 16) & 0xFF, aa = (ca >> 24) & 0xFF;
    uint8_t br = (cb >> 0) & 0xFF, bg = (cb >> 8) & 0xFF, bb = (cb >> 16) & 0xFF, ba = (cb >> 24) & 0xFF;
    uint8_t inv = 255 - f;
    return pixelman_argb32(
        (aa * inv + ba * f + 127) / 255,
        (ar * inv + br * f + 127) / 255,
        (ag * inv + bg * f + 127) / 255,
        (ab * inv + bb * f + 127) / 255);
}

static uint32_t eval_radial_gradient(pixelman_image_t *img,
                                      pixelman_fixed_t fx, pixelman_fixed_t fy) {
    (void)fx; (void)fy;
    if (!img->grad_n_stops) return 0;
    uint16_t c = img->grad_stops[0].color;
    return pixelman_argb32((c >> 8) & 0xFF, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Core compositing
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
                               uint16_t           height) {
    pixelman_image_composite32(op, src, mask, dst,
                                src_x, src_y, mask_x, mask_y,
                                dst_x, dst_y, width, height);
}

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
                                 int32_t            height) {
    if (!src || !dst || width <= 0 || height <= 0) return;
    if (!dst->data) return;

    int dst_stride = dst->stride / 4;
    int dst_w = dst->width, dst_h = dst->height;

    for (int32_t row = 0; row < height; row++) {
        int dy = dst_y + row;
        if (dy < 0 || dy >= dst_h) continue;
        uint32_t *dst_line = dst->data + dy * dst_stride;

        for (int32_t col = 0; col < width; col++) {
            int dx = dst_x + col;
            if (dx < 0 || dx >= dst_w) continue;

            int sx = src_x + col;
            int sy = src_y + row;
            uint32_t sp;

            if (src->is_linear_gradient) {
                sp = eval_linear_gradient(src,
                    (pixelman_fixed_t)sx * PIXELMAN_FIXED_ONE,
                    (pixelman_fixed_t)sy * PIXELMAN_FIXED_ONE);
            } else if (src->is_radial_gradient) {
                sp = eval_radial_gradient(src,
                    (pixelman_fixed_t)sx * PIXELMAN_FIXED_ONE,
                    (pixelman_fixed_t)sy * PIXELMAN_FIXED_ONE);
            } else {
                sp = sample_image(src, sx, sy);
            }

            /* Apply mask */
            if (mask) {
                int mx = mask_x + col;
                int my = mask_y + row;
                uint32_t mp = sample_image(mask, mx, my);
                uint8_t ma = pixelman_alpha32(mp);
                if (ma == 0 && !mask->component_alpha) continue;
                if (ma < 255 && !mask->component_alpha) {
                    uint8_t sa = pixelman_alpha32(sp);
                    uint8_t eff = (uint16_t)sa * ma / 255;
                    sp = (sp & 0x00FFFFFF) | ((uint32_t)eff << 24);
                }
            }

            dst_line[dx] = blend_op(op, sp, dst_line[dx]);
        }
    }
}

void pixelman_image_fill_rects(pixelman_op_t       op,
                                pixelman_image_t   *dst,
                                pixelman_color_t   *color,
                                int                 n_rects,
                                pixelman_rect_t    *rects) {
    if (!dst || !dst->data || !color || n_rects <= 0) return;
    uint32_t c = pixelman_argb32(color->a >> 8, color->r >> 8,
                                  color->g >> 8, color->b >> 8);
    int stride = dst->stride / 4;

    for (int i = 0; i < n_rects; i++) {
        pixelman_rect_t r = rects[i];
        if (pixelman_rect_empty(r)) continue;
        for (int y = r.y1; y < r.y2 && y < dst->height; y++) {
            if (y < 0) continue;
            uint32_t *line = dst->data + y * stride;
            for (int x = r.x1; x < r.x2 && x < dst->width; x++) {
                if (x < 0) continue;
                line[x] = blend_op(op, c, line[x]);
            }
        }
    }
}

void pixelman_fill(pixelman_image_t *dst,
                    uint32_t          color,
                    int               x, int y,
                    int               w, int h) {
    if (!dst || !dst->data || w <= 0 || h <= 0) return;
    int stride = dst->stride / 4;
    for (int row = imax(y, 0); row < imin(y + h, dst->height); row++) {
        uint32_t *line = dst->data + row * stride;
        for (int col = imax(x, 0); col < imin(x + w, dst->width); col++) {
            line[col] = color;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Double-buffer
 * ══════════════════════════════════════════════════════════════════════════════ */

pixelman_buffer_t *pixelman_buffer_create(int width, int height, int stride) {
    uint64_t buf_phys = pmm_alloc_page();
    if (!buf_phys) return 0;
    pixelman_buffer_t *buf = (pixelman_buffer_t*)phys_to_virt(buf_phys);
    memset(buf, 0, sizeof(pixelman_buffer_t));

    buf->width  = width;
    buf->height = height;

    if (stride <= 0) stride = width * 4;
    stride = (stride + 15) & ~15;
    buf->stride = stride;

    int size = stride * height;
    int pages = (size + 0xFFF) / 0x1000;
    if (pages < 1) pages = 1;

    uint64_t fphys = pmm_alloc_pages(pages);
    if (!fphys) {
        pmm_free_page(buf_phys);
        return 0;
    }
    buf->front_data = (uint32_t*)phys_to_virt(fphys);
    memset(buf->front_data, 0, size);

    uint64_t bphys = pmm_alloc_pages(pages);
    if (!bphys) {
        pmm_free_pages(fphys, pages);
        pmm_free_page(buf_phys);
        return 0;
    }
    buf->back_data = (uint32_t*)phys_to_virt(bphys);
    memset(buf->back_data, 0, size);

    /* Wrap in pixelman_image_t for compositing */
    buf->front = pixelman_image_create_bits(PIXELMAN_a8r8g8b8,
                                             width, height,
                                             buf->front_data, stride);
    buf->back  = pixelman_image_create_bits(PIXELMAN_a8r8g8b8,
                                             width, height,
                                             buf->back_data, stride);
    return buf;
}

void pixelman_buffer_destroy(pixelman_buffer_t *buf) {
    if (!buf) return;
    if (buf->front) {
        buf->front->owns_data = 0; /* don't double-free data */
        pixelman_image_unref(buf->front);
    }
    if (buf->back) {
        buf->back->owns_data = 0;
        pixelman_image_unref(buf->back);
    }
    int size = buf->stride * buf->height;
    int pages = (size + 0xFFF) / 0x1000;
    if (pages < 1) pages = 1;
    if (buf->front_data) pmm_free_pages(virt_to_phys((uint64_t)buf->front_data), pages);
    if (buf->back_data)  pmm_free_pages(virt_to_phys((uint64_t)buf->back_data), pages);
    pmm_free_page(virt_to_phys((uint64_t)buf));
}

void pixelman_buffer_begin(pixelman_buffer_t *buf) {
    if (!buf) return;
    buf->needs_swap = 1;
}

void pixelman_buffer_end(pixelman_buffer_t *buf) {
    if (!buf || !buf->needs_swap) return;
    /* Swap front and back */
    uint32_t *tmp_data = buf->front_data;
    pixelman_image_t *tmp_img = buf->front;

    buf->front_data = buf->back_data;
    buf->front      = buf->back;

    buf->back_data  = tmp_data;
    buf->back       = tmp_img;

    /* Re-target the image wrappers */
    if (buf->front) buf->front->data = buf->front_data;
    if (buf->back)  buf->back->data  = buf->back_data;

    buf->needs_swap = 0;
}

pixelman_image_t *pixelman_buffer_get_back(pixelman_buffer_t *buf) {
    return buf ? buf->back : 0;
}

pixelman_image_t *pixelman_buffer_get_front(pixelman_buffer_t *buf) {
    return buf ? buf->front : 0;
}

uint32_t *pixelman_buffer_get_data(pixelman_buffer_t *buf) {
    return buf ? buf->front_data : 0;
}

int pixelman_buffer_get_stride(pixelman_buffer_t *buf) {
    return buf ? buf->stride : 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Damage tracking
 * ══════════════════════════════════════════════════════════════════════════════ */

void pm_damage_init(pm_damage_t *dmg) {
    dmg->count = 0;
}

void pm_damage_add(pm_damage_t *dmg, pm_rect_t r) {
    if (dmg->count >= PM_MAX_DAMAGE_RECTS) return;
    dmg->rects[dmg->count++] = r;
}

void pm_damage_clear(pm_damage_t *dmg) {
    dmg->count = 0;
}

int pm_damage_empty(pm_damage_t *dmg) {
    return dmg->count == 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Legacy pm_* API (implemented via pixelman for backward compat)
 * ══════════════════════════════════════════════════════════════════════════════ */

void pm_composite_rect(uint32_t *buf, int stride, pm_rect_t clip,
                        int x, int y, int w, int h, uint32_t color) {
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + w, y + h});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    for (int row = r.y1; row < r.y2; row++) {
        uint32_t *line = buf + row * stride_words;
        for (int col = r.x1; col < r.x2; col++)
            line[col] = color;
    }
}

void pm_composite_rect_alpha(uint32_t *buf, int stride, pm_rect_t clip,
                              int x, int y, int w, int h,
                              uint32_t color, uint8_t alpha) {
    if (alpha == 0) return;
    if (alpha == 255) { pm_composite_rect(buf, stride, clip, x, y, w, h, color); return; }
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + w, y + h});
    if (pixelman_rect_empty(r)) return;
    uint8_t sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
    int stride_words = stride / 4;
    for (int row = r.y1; row < r.y2; row++) {
        uint32_t *line = buf + row * stride_words;
        for (int col = r.x1; col < r.x2; col++) {
            uint32_t dst = line[col];
            uint8_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
            uint8_t rr = (sr * alpha + dr * (255 - alpha) + 127) / 255;
            uint8_t rg = (sg * alpha + dg * (255 - alpha) + 127) / 255;
            uint8_t rb = (sb * alpha + db * (255 - alpha) + 127) / 255;
            line[col] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
        }
    }
}

void pm_composite_glyph(uint32_t *buf, int stride, pm_rect_t clip,
                         int x, int y, const uint8_t *glyph, int gw, int gh,
                         uint32_t fg, uint32_t bg) {
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + gw, y + gh});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    for (int row = r.y1; row < r.y2; row++) {
        int gy = row - y;
        if (gy < 0 || gy >= gh) continue;
        uint32_t *line = buf + row * stride_words;
        for (int col = r.x1; col < r.x2; col++) {
            int gx = col - x;
            if (gx < 0 || gx >= gw) continue;
            int bit = (gx < 8) ? ((glyph[gy] >> (7 - gx)) & 1) : 0;
            line[col] = bit ? fg : bg;
        }
    }
}

void pm_composite_hline(uint32_t *buf, int stride, pm_rect_t clip,
                         int x, int y, int w, uint32_t color) {
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + w, y + 1});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    uint32_t *line = buf + r.y1 * stride_words;
    for (int col = r.x1; col < r.x2; col++) line[col] = color;
}

void pm_composite_vline(uint32_t *buf, int stride, pm_rect_t clip,
                         int x, int y, int h, uint32_t color) {
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + 1, y + h});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    for (int row = r.y1; row < r.y2; row++) {
        uint32_t *line = buf + row * stride_words;
        line[r.x1] = color;
    }
}

void pm_composite_line(uint32_t *buf, int stride, pm_rect_t clip,
                        int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = x2 - x1, dy = y2 - y1;
    int stepx, stepy, fraction;
    if (dy < 0) { dy = -dy; stepy = -1; } else { stepy = 1; }
    if (dx < 0) { dx = -dx; stepx = -1; } else { stepx = 1; }
    dx <<= 1; dy <<= 1;
    int stride_words = stride / 4;

    if (dx > dy) {
        fraction = dy - (dx >> 1);
        while (x1 != x2) {
            if (pixelman_rect_contains(clip, x1, y1))
                *(buf + y1 * stride_words + x1) = color;
            if (fraction >= 0) { y1 += stepy; fraction -= dx; }
            x1 += stepx; fraction += dy;
        }
    } else {
        fraction = dx - (dy >> 1);
        while (y1 != y2) {
            if (pixelman_rect_contains(clip, x1, y1))
                *(buf + y1 * stride_words + x1) = color;
            if (fraction >= 0) { x1 += stepx; fraction -= dy; }
            y1 += stepy; fraction += dx;
        }
    }
    if (pixelman_rect_contains(clip, x2, y2))
        *(buf + y2 * stride_words + x2) = color;
}

void pm_composite_fill_rounded_rect(uint32_t *buf, int stride, pm_rect_t clip,
                                     int x, int y, int w, int h,
                                     int r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r == 0) { pm_composite_rect(buf, stride, clip, x, y, w, h, color); return; }

    int stride_words = stride / 4;
    int r2 = r * r;
    uint8_t src_alpha = pixelman_alpha32(color);
    int do_blend = (src_alpha > 0 && src_alpha < 255);

    int cx1 = x + r, cy1 = y + r;
    int cx2 = x + w - r - 1, cy2 = y + r;
    int cx3 = x + r, cy3 = y + h - r - 1;
    int cx4 = x + w - r - 1, cy4 = y + h - r - 1;

    for (int row = y; row < y + h; row++) {
        if (row < clip.y1 || row >= clip.y2) continue;
        uint32_t *line = buf + row * stride_words;
        for (int col = x; col < x + w; col++) {
            if (col < clip.x1 || col >= clip.x2) continue;
            int in = 1;
            uint8_t cov = 255;

            if      (col < x + r && row < y + r)         { int dsq = (col-cx1)*(col-cx1)+(row-cy1)*(row-cy1); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }
            else if (col >= x + w - r && row < y + r)    { int dsq = (col-cx2)*(col-cx2)+(row-cy2)*(row-cy2); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }
            else if (col < x + r && row >= y + h - r)    { int dsq = (col-cx3)*(col-cx3)+(row-cy3)*(row-cy3); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }
            else if (col >= x + w - r && row >= y + h - r) { int dsq = (col-cx4)*(col-cx4)+(row-cy4)*(row-cy4); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }

            if (in) {
                if (do_blend || cov < 255)
                    line[col] = blend_coverage(color, line[col], cov);
                else
                    line[col] = color;
            }
        }
    }
}

void pm_composite_fill_rounded_rect_gradient_v(uint32_t *buf, int stride,
                                                pm_rect_t clip,
                                                int x, int y, int w, int h,
                                                int r,
                                                uint32_t color_top,
                                                uint32_t color_bot) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r == 0) {
        pm_composite_fill_rect_gradient_v(buf, stride, clip, x, y, w, h,
                                           color_top, color_bot);
        return;
    }

    int stride_words = stride / 4;
    int r2 = r * r;
    int max_t = imax(h - 1, 1);
    int cx1 = x + r, cy1 = y + r;
    int cx2 = x + w - r - 1, cy2 = y + r;
    int cx3 = x + r, cy3 = y + h - r - 1;
    int cx4 = x + w - r - 1, cy4 = y + h - r - 1;

    for (int row = y; row < y + h; row++) {
        if (row < clip.y1 || row >= clip.y2) continue;
        uint32_t *line = buf + row * stride_words;
        int t = (row - y) * 255 / max_t;
        uint32_t color = pixelman_lerp_color(color_top, color_bot, (uint8_t)t);
        uint8_t src_alpha = pixelman_alpha32(color);
        int do_blend = (src_alpha > 0 && src_alpha < 255);

        for (int col = x; col < x + w; col++) {
            if (col < clip.x1 || col >= clip.x2) continue;
            int in = 1;
            uint8_t cov = 255;

            if      (col < x + r && row < y + r)         { int dsq = (col-cx1)*(col-cx1)+(row-cy1)*(row-cy1); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }
            else if (col >= x + w - r && row < y + r)    { int dsq = (col-cx2)*(col-cx2)+(row-cy2)*(row-cy2); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }
            else if (col < x + r && row >= y + h - r)    { int dsq = (col-cx3)*(col-cx3)+(row-cy3)*(row-cy3); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }
            else if (col >= x + w - r && row >= y + h - r) { int dsq = (col-cx4)*(col-cx4)+(row-cy4)*(row-cy4); if (dsq > r2) { cov = corner_coverage(dsq, r); in = (cov > 0); } }

            if (in) {
                if (do_blend || cov < 255)
                    line[col] = blend_coverage(color, line[col], cov);
                else
                    line[col] = color;
            }
        }
    }
}

void pm_composite_draw_rounded_rect(uint32_t *buf, int stride, pm_rect_t clip,
                                     int x, int y, int w, int h,
                                     int r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int stride_words = stride / 4;
    int r_inner = imax(r - 1, 0);
    int r_inner2 = r_inner * r_inner;
    int r_outer2 = (r + 1) * (r + 1);
    int cx1 = x + r, cy1 = y + r, cx2 = x + w - r - 1, cy2 = y + r;
    int cx3 = x + r, cy3 = y + h - r - 1, cx4 = x + w - r - 1, cy4 = y + h - r - 1;

    for (int row = y; row < y + h; row++) {
        if (row < clip.y1 || row >= clip.y2) continue;
        uint32_t *line = buf + row * stride_words;
        for (int col = x; col < x + w; col++) {
            if (col < clip.x1 || col >= clip.x2) continue;
            int on_edge = 0;
            if (row == y || row == y + h - 1) {
                if (col >= x + r && col < x + w - r) on_edge = 1;
            } else if (col == x || col == x + w - 1) {
                if (row >= y + r && row < y + h - r) on_edge = 1;
            } else {
                int dx, dy, dsq, in_corner = 0;
                if      (col < x + r && row < y + r)         { dx = col - cx1; dy = row - cy1; in_corner = 1; }
                else if (col >= x + w - r && row < y + r)    { dx = col - cx2; dy = row - cy2; in_corner = 1; }
                else if (col < x + r && row >= y + h - r)    { dx = col - cx3; dy = row - cy3; in_corner = 1; }
                else if (col >= x + w - r && row >= y + h - r) { dx = col - cx4; dy = row - cy4; in_corner = 1; }
                if (in_corner) {
                    dsq = dx * dx + dy * dy;
                    if (dsq >= r_inner2 && dsq <= r_outer2) on_edge = 1;
                }
            }
            if (on_edge) line[col] = color;
        }
    }
}

void pm_fill_circle(uint32_t *buf, int stride, pm_rect_t clip,
                     int cx, int cy, int r, uint32_t color) {
    if (r <= 0) return;
    int x1 = cx - r, y1 = cy - r, x2 = cx + r, y2 = cy + r;
    if (x1 >= clip.x2 || y1 >= clip.y2 || x2 < clip.x1 || y2 < clip.y1) return;
    int stride_words = stride / 4;
    int r2 = r * r;
    uint8_t src_alpha = pixelman_alpha32(color);
    int do_blend = (src_alpha > 0 && src_alpha < 255);

    for (int row = y1; row <= y2; row++) {
        if (row < clip.y1 || row >= clip.y2) continue;
        uint32_t *line = buf + row * stride_words;
        for (int col = x1; col <= x2; col++) {
            if (col < clip.x1 || col >= clip.x2) continue;
            int dx = col - cx, dy = row - cy, dsq = dx * dx + dy * dy;
            if (dsq <= r2) {
                uint8_t cov = (dsq > r2 - r && dsq <= r2) ? corner_coverage(dsq, r) : 255;
                if (do_blend || cov < 255)
                    line[col] = blend_coverage(color, line[col], cov);
                else
                    line[col] = color;
            }
        }
    }
}

void pm_composite_fill_rect_gradient_v(uint32_t *buf, int stride, pm_rect_t clip,
                                        int x, int y, int w, int h,
                                        uint32_t color_top, uint32_t color_bot) {
    if (w <= 0 || h <= 0) return;
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + w, y + h});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    int max_t = imax(h - 1, 1);
    for (int row = r.y1; row < r.y2; row++) {
        int t = (row - y) * 255 / max_t;
        t = iclamp(t, 0, 255);
        uint32_t c = pixelman_lerp_color(color_top, color_bot, (uint8_t)t);
        uint32_t *line = buf + row * stride_words;
        for (int col = r.x1; col < r.x2; col++) line[col] = c;
    }
}

void pm_composite_fill_rect_gradient_h(uint32_t *buf, int stride, pm_rect_t clip,
                                        int x, int y, int w, int h,
                                        uint32_t color_left,
                                        uint32_t color_right) {
    if (w <= 0 || h <= 0) return;
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + w, y + h});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    int max_t = imax(w - 1, 1);
    for (int row = r.y1; row < r.y2; row++) {
        uint32_t *line = buf + row * stride_words;
        for (int col = r.x1; col < r.x2; col++) {
            int t = (col - x) * 255 / max_t;
            t = iclamp(t, 0, 255);
            line[col] = pixelman_lerp_color(color_left, color_right, (uint8_t)t);
        }
    }
}

void pm_composite_fill_rect_gradient_d(uint32_t *buf, int stride, pm_rect_t clip,
                                        int x, int y, int w, int h,
                                        uint32_t color_tl, uint32_t color_br) {
    if (w <= 0 || h <= 0) return;
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){x, y, x + w, y + h});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    int max_dist = imax(w + h - 2, 1);
    for (int row = r.y1; row < r.y2; row++) {
        uint32_t *line = buf + row * stride_words;
        for (int col = r.x1; col < r.x2; col++) {
            int d = (col - x) + (row - y);
            int t = d * 255 / max_dist;
            t = iclamp(t, 0, 255);
            line[col] = pixelman_lerp_color(color_tl, color_br, (uint8_t)t);
        }
    }
}

void pm_composite_shadow(uint32_t *buf, int stride, pm_rect_t clip,
                          int x, int y, int w, int h, int radius,
                          uint8_t alpha, int offset, int layers) {
    if (alpha == 0 || layers <= 0) return;
    if (layers > 16) layers = 16;
    for (int i = 0; i < layers; i++) {
        int off = offset * (layers - i);
        uint8_t layer_alpha = (uint8_t)((uint16_t)alpha * (i + 1) / (layers + 1));
        if (layer_alpha == 0) continue;
        uint32_t shadow_color = ((uint32_t)layer_alpha << 24);
        pm_composite_fill_rounded_rect(buf, stride, clip,
                                        x + off, y + off, w, h, radius, shadow_color);
    }
}

void pm_composite_image(uint32_t *buf, int stride, pm_rect_t clip,
                         int dx, int dy,
                         const uint32_t *src, int src_w, int src_h,
                         int src_stride) {
    pm_rect_t r = pixelman_rect_intersect(clip, (pm_rect_t){dx, dy, dx + src_w, dy + src_h});
    if (pixelman_rect_empty(r)) return;
    int stride_words = stride / 4;
    int src_stride_words = src_stride / 4;

    for (int row = r.y1; row < r.y2; row++) {
        int sy = row - dy;
        if (sy < 0 || sy >= src_h) continue;
        uint32_t *dst_line = buf + row * stride_words;
        const uint32_t *src_line = src + sy * src_stride_words;
        for (int col = r.x1; col < r.x2; col++) {
            int sx = col - dx;
            if (sx < 0 || sx >= src_w) continue;
            dst_line[col] = pixelman_blend_over(src_line[sx], dst_line[col]);
        }
    }
}

void pm_composite_image_reflected(uint32_t *buf, int stride, pm_rect_t clip,
                                   int dx, int dy,
                                   const uint32_t *src, int src_w, int src_h,
                                   int src_stride,
                                   int reflect_height, uint8_t max_alpha) {
    if (reflect_height <= 0 || max_alpha == 0) return;
    int stride_words = stride / 4;
    int src_stride_words = src_stride / 4;
    int rh = imin(reflect_height, src_h);

    for (int row = 0; row < rh; row++) {
        int dst_y = dy + src_h + row;
        if (dst_y < clip.y1 || dst_y >= clip.y2) continue;
        int src_row = src_h - 1 - row;
        if (src_row < 0 || src_row >= src_h) continue;
        uint8_t ref_a = (uint8_t)((uint16_t)max_alpha * (rh - row) / rh);
        if (ref_a == 0) continue;

        uint32_t *dst_line = buf + dst_y * stride_words;
        const uint32_t *src_line = src + src_row * src_stride_words;

        for (int col = imax(clip.x1, dx); col < imin(clip.x2, dx + src_w); col++) {
            int sx = col - dx;
            if (sx < 0 || sx >= src_w) continue;
            uint32_t sp = src_line[sx];
            uint8_t sa = pixelman_alpha32(sp);
            uint8_t eff_a = (uint8_t)((uint16_t)sa * ref_a / 255);
            if (eff_a == 0) continue;
            uint32_t dp = dst_line[col];
            uint8_t dr = (dp >> 16) & 0xFF, dg = (dp >> 8) & 0xFF, db = dp & 0xFF;
            uint8_t sr = (sp >> 16) & 0xFF, sg = (sp >> 8) & 0xFF, sb = sp & 0xFF;
            uint8_t inv = 255 - eff_a;
            dst_line[col] = pixelman_argb32(255,
                (sr * eff_a + dr * inv + 127) / 255,
                (sg * eff_a + dg * inv + 127) / 255,
                (sb * eff_a + db * inv + 127) / 255);
        }
    }
}
