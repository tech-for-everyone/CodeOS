#ifndef XSERVER_H
#define XSERVER_H

#include "types.h"
#include "pixelman.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XS_MAX_WINDOWS    64
#define XS_MAX_GC          8
#define XS_TITLE_MAX      63

#define XS_EVENT_NONE      0
#define XS_EVENT_CLICK     1
#define XS_EVENT_DRAG      2
#define XS_EVENT_RELEASE   3
#define XS_EVENT_KEY       4
#define XS_EVENT_EXPOSE    5
#define XS_EVENT_DESTROY   6

typedef enum {
    XS_EDGE_NONE    = 0,
    XS_EDGE_LEFT    = 1,
    XS_EDGE_RIGHT   = 2,
    XS_EDGE_TOP     = 4,
    XS_EDGE_BOTTOM  = 8,
    XS_EDGE_SNAP    = 20,
} xs_edge_t;

typedef struct xs_event {
    int type;
    int win_idx;
    int mx, my;
    int btn;
    int key;
} xs_event_t;

typedef struct xs_screen xs_screen_t;
typedef struct xs_window xs_window_t;
typedef struct xs_gc xs_gc_t;

struct xs_window {
    int x, y, w, h;
    int orig_x, orig_y, orig_w, orig_h;
    int z_index;
    int visible;
    int minimized;
    int maximized;
    int draggable;
    int resizable;
    int dragging;
    int resizing;
    int drag_off_x, drag_off_y;
    int resize_edge;
    char title[XS_TITLE_MAX + 1];
    uint32_t bg_color;
    pixelman_image_t *buffer;
    pixelman_image_t *shadow_buf;
    int shadow_valid;
    int needs_repaint;
    pixelman_rect_t damage;
    xs_gc_t *gc;
};

struct xs_gc {
    xs_window_t *win;
    uint32_t fg;
    uint32_t bg;
    pixelman_rect_t clip;
    int has_clip;
    const uint8_t *font_bitmap;
    int font_cw, font_ch;
};

struct xs_screen {
    int width;
    int height;
    pixelman_buffer_t *buffer;
    uint32_t *fb_addr;
    int fb_stride;
    pixelman_image_t *fb_img;

    xs_window_t windows[XS_MAX_WINDOWS];
    int win_count;
    int active_win;

    xs_gc_t gcs[XS_MAX_GC];
    int gc_count;

    pixelman_rect_t damage;
    int has_damage;

    xs_event_t event;

    int (*draw_titlebar)(xs_window_t *win, int is_active);
    int window_has_close;
};

xs_screen_t *xs_screen_create(int width, int height, uint32_t *fb_addr, int stride);
xs_screen_t *xs_get_screen(void);
void xs_screen_destroy(xs_screen_t *screen);
void xs_screen_set_titlebar_fn(xs_screen_t *screen, int (*fn)(xs_window_t *, int));

int xs_create_window(xs_screen_t *screen, int x, int y, int w, int h,
                     const char *title, uint32_t bg);
void xs_destroy_window(xs_screen_t *screen, int idx);
void xs_move_window(xs_screen_t *screen, int idx, int x, int y);
void xs_resize_window(xs_screen_t *screen, int idx, int w, int h);
void xs_show_window(xs_screen_t *screen, int idx, int visible);
void xs_raise_window(xs_screen_t *screen, int idx);
void xs_lower_window(xs_screen_t *screen, int idx);
void xs_set_window_title(xs_screen_t *screen, int idx, const char *title);
int  xs_find_window(xs_screen_t *screen, const char *title);
int  xs_raise_or_create(xs_screen_t *screen, const char *title,
                        int x, int y, int w, int h, uint32_t bg);

xs_window_t *xs_get_window(xs_screen_t *screen, int idx);
int  xs_active_window(xs_screen_t *screen);
void xs_set_active_window(xs_screen_t *screen, int idx);
int  xs_window_count(xs_screen_t *screen);
int  xs_next_window(xs_screen_t *screen);
int  xs_prev_window(xs_screen_t *screen);

xs_gc_t *xs_create_gc(xs_screen_t *screen, int win_idx);
void xs_destroy_gc(xs_screen_t *screen, int gc_idx);
void xs_set_fg(xs_gc_t *gc, uint32_t color);
void xs_set_bg(xs_gc_t *gc, uint32_t color);
void xs_set_clip(xs_gc_t *gc, pixelman_rect_t rect);
void xs_set_font(xs_gc_t *gc, const uint8_t *font, int cw, int ch);
void xs_set_gc_font_bitmap(xs_gc_t *gc, const uint8_t *bitmap, int cw, int ch);

void xs_fill_rect(xs_gc_t *gc, int x, int y, int w, int h);
void xs_fill_rounded_rect(xs_gc_t *gc, int x, int y, int w, int h, int r);
void xs_draw_rounded_rect(xs_gc_t *gc, int x, int y, int w, int h, int r);
void xs_fill_circle(xs_gc_t *gc, int cx, int cy, int r);
void xs_draw_line(xs_gc_t *gc, int x1, int y1, int x2, int y2);
void xs_draw_text(xs_gc_t *gc, int x, int y, const char *text);
void xs_draw_glyph(xs_gc_t *gc, int x, int y, const uint8_t *glyph, int gw, int gh);
void xs_draw_image(xs_gc_t *gc, int dx, int dy, const uint32_t *src,
                   int w, int h, int src_stride);
void xs_fill_gradient_v(xs_gc_t *gc, int x, int y, int w, int h,
                        uint32_t top, uint32_t bot);
void xs_fill_gradient_h(xs_gc_t *gc, int x, int y, int w, int h,
                        uint32_t left, uint32_t right);
void xs_fill_gradient_d(xs_gc_t *gc, int x, int y, int w, int h,
                        uint32_t tl, uint32_t br);
void xs_fill_rounded_gradient_v(xs_gc_t *gc, int x, int y, int w, int h, int r,
                                uint32_t top, uint32_t bot);
void xs_draw_shadow(xs_gc_t *gc, int x, int y, int w, int h, int r,
                    uint8_t alpha, int offset, int layers);

void xs_composite(xs_screen_t *screen);
void xs_screen_flush(xs_screen_t *screen);
void xs_damage_add(xs_screen_t *screen, pixelman_rect_t rect);
void xs_expose(xs_screen_t *screen, int x, int y, int w, int h);
uint32_t *xs_get_screen_buffer(xs_screen_t *screen);
void xs_set_window_pixels(xs_screen_t *screen, int idx,
                          const uint32_t *pixels, int src_stride,
                          int dst_x, int dst_y, int w, int h);
int xs_handle_mouse(xs_screen_t *screen, int mx, int my, int btn);
void xs_handle_drag(xs_screen_t *screen, int mx, int my, int mh, int dh);
void xs_handle_release(xs_screen_t *screen);

#ifdef __cplusplus
}
#endif

#endif
