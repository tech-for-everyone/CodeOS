extern "C" {
#include "xserver.h"
#include "pixelman.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "kprintf.h"
#include "fb.h"
}

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

extern uint64_t phys_to_virt_base;

static xs_screen_t *default_screen;

static void xs_window_damage(xs_screen_t *screen, int x, int y, int w, int h) {
    pixelman_rect_t r = {x, y, x + w, y + h};
    if (screen->has_damage) {
        screen->damage.x1 = imin(screen->damage.x1, r.x1);
        screen->damage.y1 = imin(screen->damage.y1, r.y1);
        screen->damage.x2 = imax(screen->damage.x2, r.x2);
        screen->damage.y2 = imax(screen->damage.y2, r.y2);
    } else {
        screen->damage = r;
        screen->has_damage = 1;
    }
}

static void xs_intersect_damage(xs_screen_t *screen, pixelman_rect_t *r) {
    if (screen->has_damage) {
        r->x1 = imax(r->x1, screen->damage.x1);
        r->y1 = imax(r->y1, screen->damage.y1);
        r->x2 = imin(r->x2, screen->damage.x2);
        r->y2 = imin(r->y2, screen->damage.y2);
        if (r->x1 >= r->x2 || r->y1 >= r->y2) {
            r->x1 = r->y1 = r->x2 = r->y2 = 0;
        }
    }
}

static void xs_clear_window_buffer(xs_window_t *win) {
    if (!win || !win->buffer) return;
    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer) / 4;
    int w = pixelman_image_get_width(win->buffer);
    int h = pixelman_image_get_height(win->buffer);
    for (int y = 0; y < h; y++) {
        uint32_t *line = data + y * stride;
        for (int x = 0; x < w; x++)
            line[x] = win->bg_color;
    }
}

xs_screen_t *xs_screen_create(int width, int height, uint32_t *fb_addr, int stride) {
    uint64_t screen_phys = pmm_alloc_page();
    if (!screen_phys) return 0;
    xs_screen_t *screen = (xs_screen_t*)phys_to_virt(screen_phys);
    memset(screen, 0, sizeof(xs_screen_t));

    screen->width = width;
    screen->height = height;
    screen->fb_addr = (uint32_t*)phys_to_virt((uint64_t)fb_addr);
    screen->fb_stride = stride;
    screen->win_count = 0;
    screen->active_win = -1;
    screen->gc_count = 0;
    screen->has_damage = 0;
    screen->window_has_close = 1;

    pixelman_buffer_t *buf = pixelman_buffer_create(width, height, stride);
    if (!buf) {
        pmm_free_page(screen_phys);
        return 0;
    }
    screen->buffer = buf;

    screen->fb_img = pixelman_image_create_bits(
        PIXELMAN_a8r8g8b8, width, height,
        screen->fb_addr, screen->fb_stride);

    default_screen = screen;
    return screen;
}

void xs_screen_destroy(xs_screen_t *screen) {
    if (!screen) return;

    for (int i = 0; i < screen->win_count; i++) {
        xs_window_t *win = &screen->windows[i];
        if (win->buffer) {
            pixelman_image_unref(win->buffer);
            win->buffer = 0;
        }
    }

    if (screen->buffer) pixelman_buffer_destroy(screen->buffer);
    if (screen->fb_img) pixelman_image_unref(screen->fb_img);

    if (default_screen == screen) default_screen = 0;
    pmm_free_page(virt_to_phys((uint64_t)screen));
}

void xs_screen_set_titlebar_fn(xs_screen_t *screen, int (*fn)(xs_window_t *, int)) {
    if (screen) screen->draw_titlebar = fn;
}

xs_screen_t *xs_get_screen(void) {
    return default_screen;
}

int xs_create_window(xs_screen_t *screen, int x, int y, int w, int h,
                     const char *title, uint32_t bg) {
    if (!screen) return -1;
    if (screen->win_count >= XS_MAX_WINDOWS) return -1;

    int idx = screen->win_count;
    xs_window_t *win = &screen->windows[idx];
    memset(win, 0, sizeof(xs_window_t));

    win->x = x; win->y = y;
    win->w = w; win->h = h;
    win->orig_x = x; win->orig_y = y;
    win->orig_w = w; win->orig_h = h;
    win->z_index = idx;
    win->visible = 1;
    win->draggable = 1;
    win->resizable = 1;
    win->bg_color = bg;
    win->needs_repaint = 1;
    win->gc = 0;

    int ti = 0;
    while (*title && ti < XS_TITLE_MAX) win->title[ti++] = *title++;
    win->title[ti] = 0;

    screen->win_count++;
    screen->active_win = idx;

    return idx;
}

void xs_destroy_window(xs_screen_t *screen, int idx) {
    if (!screen || idx < 0 || idx >= screen->win_count) return;
    xs_window_t *win = &screen->windows[idx];
    win->visible = 0;

    if (win->buffer) {
        pixelman_image_unref(win->buffer);
        win->buffer = 0;
    }

    if (screen->active_win == idx) {
        screen->active_win = -1;
        for (int i = screen->win_count - 1; i >= 0; i--)
            if (screen->windows[i].visible) { screen->active_win = i; break; }
    }
}

void xs_move_window(xs_screen_t *screen, int idx, int x, int y) {
    if (!screen || idx < 0 || idx >= screen->win_count) return;
    xs_window_t *win = &screen->windows[idx];
    win->x = x; win->y = y;
    win->maximized = 0;
    xs_window_damage(screen, x, y, win->w, win->h);
}

void xs_resize_window(xs_screen_t *screen, int idx, int w, int h) {
    if (!screen || idx < 0 || idx >= screen->win_count) return;
    xs_window_t *win = &screen->windows[idx];
    win->w = w; win->h = h;

    if (win->buffer) {
        pixelman_image_unref(win->buffer);
        win->buffer = 0;
    }
    win->needs_repaint = 1;
}

void xs_show_window(xs_screen_t *screen, int idx, int visible) {
    if (!screen || idx < 0 || idx >= screen->win_count) return;
    screen->windows[idx].visible = visible;
}

void xs_raise_window(xs_screen_t *screen, int idx) {
    if (!screen || idx < 0 || idx >= screen->win_count) return;
    xs_window_t *win = &screen->windows[idx];
    int target_z = screen->win_count - 1;

    for (int i = 0; i < screen->win_count; i++) {
        if (i != idx && screen->windows[i].z_index > win->z_index)
            screen->windows[i].z_index--;
    }
    win->z_index = target_z;
    if (screen->active_win != idx) {
        screen->active_win = idx;
    }
}

void xs_lower_window(xs_screen_t *screen, int idx) {
    if (!screen || idx < 0 || idx >= screen->win_count) return;
    xs_window_t *win = &screen->windows[idx];

    for (int i = 0; i < screen->win_count; i++) {
        if (i != idx && screen->windows[i].z_index < win->z_index)
            screen->windows[i].z_index++;
    }
    win->z_index = 0;
}

void xs_set_window_title(xs_screen_t *screen, int idx, const char *title) {
    if (!screen || idx < 0 || idx >= screen->win_count || !title) return;
    xs_window_t *win = &screen->windows[idx];
    int ti = 0;
    while (*title && ti < XS_TITLE_MAX) win->title[ti++] = *title++;
    win->title[ti] = 0;
}

int xs_find_window(xs_screen_t *screen, const char *title) {
    if (!screen || !title) return -1;
    for (int i = 0; i < screen->win_count; i++)
        if (screen->windows[i].visible && strcmp(screen->windows[i].title, title) == 0)
            return i;
    return -1;
}

int xs_raise_or_create(xs_screen_t *screen, const char *title,
                       int x, int y, int w, int h, uint32_t bg) {
    if (!screen) return -1;
    int idx = xs_find_window(screen, title);
    if (idx >= 0) {
        xs_window_t *win = &screen->windows[idx];
        if (win->minimized) {
            win->minimized = 0;
            win->visible = 1;
        }
        win->needs_repaint = 1;
        xs_raise_window(screen, idx);
        return idx;
    }
    return xs_create_window(screen, x, y, w, h, title, bg);
}

xs_window_t *xs_get_window(xs_screen_t *screen, int idx) {
    if (!screen || idx < 0 || idx >= screen->win_count) return 0;
    return &screen->windows[idx];
}

int xs_active_window(xs_screen_t *screen) {
    return screen ? screen->active_win : -1;
}

void xs_set_active_window(xs_screen_t *screen, int idx) {
    if (screen) screen->active_win = idx;
}

int xs_window_count(xs_screen_t *screen) {
    return screen ? screen->win_count : 0;
}

int xs_next_window(xs_screen_t *screen) {
    if (!screen || screen->win_count == 0) return -1;
    int start = screen->active_win;
    if (start < 0) start = screen->win_count - 1;
    int i = (start + 1) % screen->win_count;
    while (i != start) {
        if (screen->windows[i].visible && !screen->windows[i].minimized) {
            screen->active_win = i;
            return i;
        }
        i = (i + 1) % screen->win_count;
    }
    return -1;
}

int xs_prev_window(xs_screen_t *screen) {
    if (!screen || screen->win_count == 0) return -1;
    int start = screen->active_win;
    if (start < 0) start = 0;
    int i = (start - 1 + screen->win_count) % screen->win_count;
    while (i != start) {
        if (screen->windows[i].visible && !screen->windows[i].minimized) {
            screen->active_win = i;
            return i;
        }
        i = (i - 1 + screen->win_count) % screen->win_count;
    }
    return -1;
}

xs_gc_t *xs_create_gc(xs_screen_t *screen, int win_idx) {
    if (!screen || win_idx < 0 || win_idx >= screen->win_count) return 0;
    if (screen->gc_count >= XS_MAX_GC) return 0;

    int gidx = screen->gc_count;
    xs_gc_t *gc = &screen->gcs[gidx];
    memset(gc, 0, sizeof(xs_gc_t));

    gc->win = &screen->windows[win_idx];
    gc->fg = 0xFFFFFFFF;
    gc->bg = 0;
    gc->has_clip = 0;
    gc->font_bitmap = 0;
    gc->font_cw = 8;
    gc->font_ch = 16;

    screen->windows[win_idx].gc = gc;
    screen->gc_count++;
    return gc;
}

void xs_destroy_gc(xs_screen_t *screen, int gc_idx) {
    if (!screen || gc_idx < 0 || gc_idx >= screen->gc_count) return;
    xs_gc_t *gc = &screen->gcs[gc_idx];
    if (gc->win) gc->win->gc = 0;
    memset(gc, 0, sizeof(xs_gc_t));

    if (gc_idx < screen->gc_count - 1) {
        screen->gcs[gc_idx] = screen->gcs[screen->gc_count - 1];
        if (screen->gcs[gc_idx].win)
            screen->gcs[gc_idx].win->gc = &screen->gcs[gc_idx];
    }
    screen->gc_count--;
}

void xs_set_fg(xs_gc_t *gc, uint32_t color) {
    if (gc) gc->fg = color;
}

void xs_set_bg(xs_gc_t *gc, uint32_t color) {
    if (gc) gc->bg = color;
}

void xs_set_clip(xs_gc_t *gc, pixelman_rect_t rect) {
    if (!gc) return;
    gc->clip = rect;
    gc->has_clip = 1;
}

void xs_set_font(xs_gc_t *gc, const uint8_t *font, int cw, int ch) {
    if (!gc) return;
    gc->font_bitmap = font;
    gc->font_cw = cw > 0 ? cw : 8;
    gc->font_ch = ch > 0 ? ch : 16;
}

void xs_set_gc_font_bitmap(xs_gc_t *gc, const uint8_t *bitmap, int cw, int ch) {
    xs_set_font(gc, bitmap, cw, ch);
}

static int xs_ensure_window_buffer(xs_window_t *win) {
    if (!win) return 0;
    if (win->buffer) return 1;
    if (win->w <= 0 || win->h <= 0) return 0;

    win->buffer = pixelman_image_create_bits(PIXELMAN_a8r8g8b8,
                                              win->w, win->h, 0, 0);
    if (!win->buffer) return 0;
    xs_clear_window_buffer(win);
    return 1;
}

static void xs_apply_clip(xs_gc_t *gc, int *x, int *y, int *w, int *h) {
    xs_window_t *win = gc->win;
    if (!win) return;

    int win_x1 = 0, win_y1 = 0;
    int win_x2 = win->w, win_y2 = win->h;
    int r_x1 = *x, r_y1 = *y, r_x2 = *x + *w, r_y2 = *y + *h;

    if (gc->has_clip) {
        r_x1 = imax(r_x1, gc->clip.x1);
        r_y1 = imax(r_y1, gc->clip.y1);
        r_x2 = imin(r_x2, gc->clip.x2);
        r_y2 = imin(r_y2, gc->clip.y2);
    }

    r_x1 = imax(r_x1, win_x1);
    r_y1 = imax(r_y1, win_y1);
    r_x2 = imin(r_x2, win_x2);
    r_y2 = imin(r_y2, win_y2);

    *x = r_x1; *y = r_y1;
    *w = r_x2 - r_x1;
    *h = r_y2 - r_y1;
}

void xs_fill_rect(xs_gc_t *gc, int x, int y, int w, int h) {
    if (!gc || !gc->win || w <= 0 || h <= 0) return;
    xs_apply_clip(gc, &x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;

    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    pixelman_fill(win->buffer, gc->fg, x, y, w, h);
}

void xs_fill_rounded_rect(xs_gc_t *gc, int x, int y, int w, int h, int r) {
    if (!gc || !gc->win || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_fill_rounded_rect(data, stride, clip, x, y, w, h, r, gc->fg);
}

void xs_draw_rounded_rect(xs_gc_t *gc, int x, int y, int w, int h, int r) {
    if (!gc || !gc->win || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_draw_rounded_rect(data, stride, clip, x, y, w, h, r, gc->fg);
}

void xs_fill_circle(xs_gc_t *gc, int cx, int cy, int r) {
    if (!gc || !gc->win || r <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_fill_circle(data, stride, clip, cx, cy, r, gc->fg);
}

void xs_draw_line(xs_gc_t *gc, int x1, int y1, int x2, int y2) {
    if (!gc || !gc->win) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_line(data, stride, clip, x1, y1, x2, y2, gc->fg);
}

void xs_draw_text(xs_gc_t *gc, int x, int y, const char *text) {
    if (!gc || !gc->win || !text) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};

    while (*text) {
        unsigned char c = (unsigned char)*text;
        if (gc->font_bitmap) {
            pm_composite_glyph(data, stride, clip, x, y,
                               gc->font_bitmap + c * gc->font_ch,
                               gc->font_cw, gc->font_ch,
                               gc->fg, gc->bg);
        } else {
            extern const uint8_t font8x16[256][16];
            pm_composite_glyph(data, stride, clip, x, y,
                               (const uint8_t*)font8x16[c],
                               8, 16, gc->fg, gc->bg);
        }
        x += gc->font_cw ? gc->font_cw : 8;
        text++;
    }
}

void xs_draw_glyph(xs_gc_t *gc, int x, int y, const uint8_t *glyph, int gw, int gh) {
    if (!gc || !gc->win || !glyph || gw <= 0 || gh <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_glyph(data, stride, clip, x, y, glyph, gw, gh, gc->fg, gc->bg);
}

void xs_draw_image(xs_gc_t *gc, int dx, int dy, const uint32_t *src,
                   int w, int h, int src_stride) {
    if (!gc || !gc->win || !src || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_image(data, stride, clip, dx, dy, src, w, h, src_stride);
}

void xs_fill_gradient_v(xs_gc_t *gc, int x, int y, int w, int h,
                        uint32_t top, uint32_t bot) {
    if (!gc || !gc->win || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_fill_rect_gradient_v(data, stride, clip, x, y, w, h, top, bot);
}

void xs_fill_gradient_h(xs_gc_t *gc, int x, int y, int w, int h,
                        uint32_t left, uint32_t right) {
    if (!gc || !gc->win || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_fill_rect_gradient_h(data, stride, clip, x, y, w, h, left, right);
}

void xs_fill_gradient_d(xs_gc_t *gc, int x, int y, int w, int h,
                        uint32_t tl, uint32_t br) {
    if (!gc || !gc->win || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_fill_rect_gradient_d(data, stride, clip, x, y, w, h, tl, br);
}

void xs_fill_rounded_gradient_v(xs_gc_t *gc, int x, int y, int w, int h, int r,
                                uint32_t top, uint32_t bot) {
    if (!gc || !gc->win || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_fill_rounded_rect_gradient_v(data, stride, clip,
                                               x, y, w, h, r, top, bot);
}

void xs_draw_shadow(xs_gc_t *gc, int x, int y, int w, int h, int r,
                    uint8_t alpha, int offset, int layers) {
    if (!gc || !gc->win || alpha == 0 || layers <= 0 || w <= 0 || h <= 0) return;
    xs_window_t *win = gc->win;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *data = (uint32_t*)pixelman_image_get_data(win->buffer);
    int stride = pixelman_image_get_stride(win->buffer);
    pm_rect_t clip = {0, 0, win->w, win->h};
    pm_composite_shadow(data, stride, clip, x, y, w, h, r, alpha, offset, layers);
}

static void xs_composite_window(xs_screen_t *screen, xs_window_t *win) {
    if (!screen || !win || !win->visible || win->minimized) return;
    if (!win->buffer) {
        if (!xs_ensure_window_buffer(win)) return;
    }

    int sx = win->x, sy = win->y;
    int sw = win->w, sh = win->h;

    pixelman_rect_t sr = {sx, sy, sx + sw, sy + sh};
    xs_intersect_damage(screen, &sr);
    if (sr.x1 >= sr.x2 || sr.y1 >= sr.y2) return;

    pixelman_image_t *back = pixelman_buffer_get_back(screen->buffer);
    if (!back) return;

    int src_x = sr.x1 - sx;
    int src_y = sr.y1 - sy;
    int dst_x = sr.x1;
    int dst_y = sr.y1;
    int cw = sr.x2 - sr.x1;
    int ch = sr.y2 - sr.y1;

    pixelman_image_composite32(PIXELMAN_OP_SRC,
                                win->buffer, 0, back,
                                src_x, src_y,
                                0, 0,
                                dst_x, dst_y,
                                cw, ch);
}

void xs_composite(xs_screen_t *screen) {
    if (!screen || !screen->has_damage) return;

    pixelman_buffer_begin(screen->buffer);
    pixelman_image_t *back = pixelman_buffer_get_back(screen->buffer);
    if (!back) return;

    pixelman_rect_t dmg = screen->damage;
    dmg.x1 = imax(dmg.x1, 0);
    dmg.y1 = imax(dmg.y1, 0);
    dmg.x2 = imin(dmg.x2, screen->width);
    dmg.y2 = imin(dmg.y2, screen->height);

    int max_z = -1;
    for (int pass = 0; pass < screen->win_count; pass++) {
        int next_z = 9999;
        int next_idx = -1;
        for (int i = 0; i < screen->win_count; i++) {
            int z = screen->windows[i].z_index;
            if (z > max_z && z < next_z) {
                next_z = z;
                next_idx = i;
            }
        }
        if (next_idx < 0) break;
        max_z = next_z;

        xs_window_t *win = &screen->windows[next_idx];
        if (win->visible && !win->minimized) {
            xs_composite_window(screen, win);
        }
    }

    screen->has_damage = 0;
    pixelman_buffer_end(screen->buffer);
}

uint32_t *xs_get_screen_buffer(xs_screen_t *screen) {
    if (!screen) return 0;
    pixelman_image_t *img = pixelman_buffer_get_back(screen->buffer);
    return img ? (uint32_t*)pixelman_image_get_data(img) : 0;
}

void xs_screen_flush(xs_screen_t *screen) {
    if (!screen) return;

    /* Composite visible X windows directly onto the real framebuffer.
       The kernel-mode desktop already drew wallpaper/panels there via
       fb_get_active_buffer() → fb_base(), so we only add windows. */

    /* Sort visible windows by z_index (low = bottom, high = top) */
    int order[XS_MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < screen->win_count; i++) {
        if (screen->windows[i].visible && !screen->windows[i].minimized)
            order[n++] = i;
    }
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && screen->windows[order[j]].z_index > screen->windows[key].z_index) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    if (!screen->fb_img) return;

    /* Composite windows in order */
    for (int i = 0; i < n; i++) {
        xs_window_t *win = &screen->windows[order[i]];
        if (!win->buffer) {
            if (win->needs_repaint) xs_ensure_window_buffer(win);
            if (!win->buffer) continue;
        }
        pixelman_image_composite32(PIXELMAN_OP_SRC, win->buffer, 0, screen->fb_img,
                                   0, 0, 0, 0,
                                   win->x, win->y,
                                   win->w, win->h);
        win->needs_repaint = 0;
    }
}

void xs_damage_add(xs_screen_t *screen, pixelman_rect_t rect) {
    if (!screen) return;
    xs_window_damage(screen, rect.x1, rect.y1,
                     pixelman_rect_width(rect),
                     pixelman_rect_height(rect));
}

void xs_expose(xs_screen_t *screen, int x, int y, int w, int h) {
    xs_window_damage(screen, x, y, w, h);
}

int xs_handle_mouse(xs_screen_t *screen, int mx, int my, int btn) {
    if (!screen) return -1;

    for (int pass = screen->win_count - 1; pass >= 0; pass--) {
        int hi_z = -1, hi_idx = -1;
        for (int i = 0; i < screen->win_count; i++) {
            xs_window_t *w = &screen->windows[i];
            if (!w->visible || w->minimized) continue;
            int sx = w->x - 20, sy = w->y - 20;
            int ex = w->x + w->w + 20, ey = w->y + w->h + 20;
            if (mx >= sx && mx < ex && my >= sy && my < ey) {
                if (w->z_index > hi_z) {
                    hi_z = w->z_index;
                    hi_idx = i;
                }
            }
        }
        if (hi_idx >= 0) {
            if (btn) {
                xs_raise_window(screen, hi_idx);
                screen->active_win = hi_idx;
            }
            return hi_idx;
        }
    }
    return -1;
}

void xs_handle_drag(xs_screen_t *screen, int mx, int my, int mh, int dh) {
    if (!screen) return;
    int idx = screen->active_win;
    if (idx < 0) return;
    xs_window_t *w = &screen->windows[idx];
    if (!w->visible || w->minimized) return;

    if (w->resizing) {
        int nx = w->x, ny = w->y, nw = w->w, nh = w->h;
        if (w->resize_edge & XS_EDGE_RIGHT) nw = mx - nx;
        if (w->resize_edge & XS_EDGE_BOTTOM) nh = my - ny;
        if (w->resize_edge & XS_EDGE_LEFT) {
            int old_right = nx + nw;
            nx = mx; nw = old_right - nx;
            if (nw < 80) { nx = old_right - 80; nw = 80; }
        }
        if (w->resize_edge & XS_EDGE_TOP) {
            int old_bottom = ny + nh;
            ny = my; nh = old_bottom - ny;
            if (nh < 60) { ny = old_bottom - 60; nh = 60; }
        }
        if (nw < 80) nw = 80;
        if (nh < 60) nh = 60;
        if (ny < mh) ny = mh;
        w->x = nx; w->y = ny; w->w = nw; w->h = nh;
        w->maximized = 0;
        if (w->buffer) {
            pixelman_image_unref(w->buffer);
            w->buffer = 0;
        }
        w->needs_repaint = 1;
        xs_window_damage(screen, nx, ny, nw, nh);
        return;
    }

    if (!w->dragging) return;
    int nx = mx - w->drag_off_x;
    int ny = my - w->drag_off_y;
    if (nx < XS_EDGE_SNAP) nx = 0;
    if (ny < mh + XS_EDGE_SNAP) ny = mh;
    int max_x = screen->width - w->w;
    if (nx > max_x - XS_EDGE_SNAP) nx = max_x;
    int max_y = (screen->height - dh) - w->h;
    if (ny > max_y - XS_EDGE_SNAP) ny = max_y;
    w->x = nx; w->y = ny;
    w->maximized = 0;
    xs_window_damage(screen, nx, ny, w->w, w->h);
}

void xs_handle_release(xs_screen_t *screen) {
    if (!screen) return;
    for (int i = 0; i < screen->win_count; i++) {
        screen->windows[i].dragging = 0;
        screen->windows[i].resizing = 0;
    }
}

/* ── C API: copy external pixels into a window buffer ─────────────── */
void xs_set_window_pixels(xs_screen_t *screen, int idx,
                          const uint32_t *pixels, int src_stride,
                          int dst_x, int dst_y, int w, int h) {
    if (!screen || idx < 0 || idx >= screen->win_count) return;
    xs_window_t *win = &screen->windows[idx];
    if (!pixels || w <= 0 || h <= 0) return;
    if (!xs_ensure_window_buffer(win)) return;

    uint32_t *dst_data = (uint32_t *)pixelman_image_get_data(win->buffer);
    int dst_stride = pixelman_image_get_stride(win->buffer) / 4;
    int buf_w = pixelman_image_get_width(win->buffer);
    int buf_h = pixelman_image_get_height(win->buffer);

    for (int row = 0; row < h; row++) {
        int sy = dst_y + row;
        int sx = dst_x;
        if (sy < 0 || sy >= buf_h) continue;
        int copy_w = w;
        if (sx < 0) { pixels += (-sx); copy_w += sx; sx = 0; }
        if (sx + copy_w > buf_w) copy_w = buf_w - sx;
        if (copy_w <= 0) continue;
        memcpy(dst_data + sy * dst_stride + sx,
               pixels + row * src_stride,
               copy_w * 4);
    }
    xs_window_damage(screen, win->x, win->y, win->w, win->h);
}

/* ── C API: initialize the compositor screen (called once at boot) ── */
void xs_init(void) {
    extern struct fb_info_t fb;
    extern uint64_t fb_get_addr_phys(void);
    int pitch = fb_get_pitch();
    if (pitch <= 0) return;
    /* pixelman_image strides are in BYTES (composite32 divides by 4 to get
     * words); passing words here compressed every composite into the top
     * quarter of the framebuffer. */
    int stride = pitch;
    uint32_t *phys = (uint32_t *)fb_get_addr_phys();
    if (!phys) return;
    xs_screen_t *s = xs_screen_create(fb.width, fb.height, phys, stride);
    if (s) {
        kprintf("xserver: compositor screen %dx%d stride=%d\n",
                fb.width, fb.height, stride);
    }
}

/* ── C API: composite X windows + flush to active framebuffer ────── */
void xs_present(void) {
    if (!default_screen) return;
    /* Re-wrap the compositor destination around the current active buffer
     * (Qt/HyperDE write there too; double-buffering may be active). */
    uint32_t *active = fb_get_active_buffer();
    if (!active) return;
    if (default_screen->fb_img)
        pixelman_image_unref(default_screen->fb_img);
    default_screen->fb_img = pixelman_image_create_bits(
        PIXELMAN_a8r8g8b8, default_screen->width, default_screen->height,
        active, default_screen->fb_stride);
    xs_composite(default_screen);
    xs_screen_flush(default_screen);
}

/* ── C API: simple pixel primitives on the screen back buffer ─────── */
void xs_fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!default_screen) return;
    pixelman_image_t *back = pixelman_buffer_get_back(default_screen->buffer);
    if (!back) return;
    pixelman_fill(back, color, (int)x, (int)y, (int)w, (int)h);
}

void xs_drawrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 uint32_t color, int thickness) {
    if (!default_screen || thickness <= 0) return;
    xs_fillrect(x, y, w, thickness, color);
    xs_fillrect(x, y + h - thickness, w, thickness, color);
    xs_fillrect(x, y, thickness, h, color);
    xs_fillrect(x + w - thickness, y, thickness, h, color);
}

/* Renders glyphs through the existing kernel font (font8x16) into the
 * active framebuffer; used as a convenience, not the X11 content path. */
void xs_drawtext(int x, int y, const char *text, uint32_t color, int size) {
    (void)size;
    if (!default_screen || !text) return;
    fb_drawstr_px((uint32_t)x, (uint32_t)y, text, color, 0);
}
