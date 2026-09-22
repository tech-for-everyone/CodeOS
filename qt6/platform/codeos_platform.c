#include "codeos_platform.h"
#include <stdint.h>
#include <string.h>

#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "input.h"
#include "lvgl_port.h"

extern void timer_sleep_ms(uint32_t ms);
extern uint64_t timer_get_milliseconds(void);
extern void kprintf(const char *fmt, ...);

static codeos_fb_info_t fb_info;
static int platform_initialized = 0;

int codeos_platform_init(void) {
    if (platform_initialized) return 0;
    fb_info.width  = fb_getwidth();
    fb_info.height = fb_getheight();
    fb_info.pitch  = fb_get_pitch();
    fb_info.addr   = fb_get_addr_phys();
    fb_info.bpp    = fb_get_bpp();
    platform_initialized = 1;
    return 0;
}

void codeos_platform_cleanup(void) {
    platform_initialized = 0;
}

void codeos_platform_dump_theme(const void *theme) {
    if (!theme) {
        kprintf("THM: theme=NULL\n");
        return;
    }
    uint64_t vptr = *(const uint64_t *)theme;
    uint64_t dptr = *(const uint64_t *)((const char *)theme + 8);
    uint64_t syspal = dptr ? *(const uint64_t *)((const char *)dptr + 8) : 0;
    kprintf("THM: theme=%p vptr=%p dptr=%p systemPalette=%p\n",
            theme, (void *)vptr, (void *)dptr, (void *)syspal);
    if (syspal) {
        uint64_t p = *(const uint64_t *)syspal;
        uint64_t data = p ? *(const uint64_t *)((const char *)p + 0x18) : 0;
        uint64_t mask = *(const uint64_t *)((const char *)syspal + 8);
        kprintf("THM: pal.d=%p pal.data=%p mask=%llx\n",
                (void *)p, (void *)data, (unsigned long long)mask);
    }
    /* raw 32 bytes at the theme object */
    kprintf("THM: raw[0]=%p raw[8]=%p raw[16]=%p raw[24]=%p\n",
            (void *)((const uint64_t *)theme)[0],
            (void *)((const uint64_t *)theme)[1],
            (void *)((const uint64_t *)theme)[2],
            (void *)((const uint64_t *)theme)[3]);
    /* QPlatformTheme vtable slots at runtime (vptr known from static analysis) */
    if (vptr == 0xffffffff81cd5460ULL || vptr == 0xffffffff80993220ULL) {
        const uint64_t *vt = (const uint64_t *)0xffffffff81cd5460ULL;
        kprintf("THM: vtbl[0]=%p vtbl[1]=%p vtbl[2]=%p\n",
                (void *)vt[0], (void *)vt[1], (void *)vt[2]);
    }
}

int codeos_platform_get_fb_info(codeos_fb_info_t *info) {
    if (!info) return -1;
    *info = fb_info;
    return 0;
}

extern int hyperde_shell_active(void);

int codeos_platform_poll_input(codeos_input_event_t *ev) {
    if (!ev) return -1;
    /* When HyperDE owns the compositor chrome it routes input to Qt, so the
       LVGL input port (launcher) must not swallow key/mouse events. */
    if (lvgl_port_input_enabled() && !hyperde_shell_active()) {
        ev->type = 0;
        return 0;
    }
    input_poll();
    if (input_available()) {
        input_event_t kiev;
        if (input_get_event(&kiev) && kiev.type != 0) {
            kprintf("QPA poll: type=%d key=%d mouse=(%d,%d) btn=%d\n",
                    kiev.type, kiev.data.key.key,
                    kiev.data.mouse.x, kiev.data.mouse.y, kiev.data.mouse.buttons);
            if (kiev.type == INPUT_EVENT_KEY) {
                ev->type = 1;
                ev->key = kiev.data.key.key;
                ev->mouse_x = 0;
                ev->mouse_y = 0;
                ev->mouse_buttons = 0;
                ev->modifiers = kiev.data.key.modifiers;
            } else if (kiev.type == INPUT_EVENT_MOUSE) {
                ev->type = 2;
                ev->key = 0;
                ev->mouse_x = kiev.data.mouse.x;
                ev->mouse_y = kiev.data.mouse.y;
                ev->mouse_buttons = kiev.data.mouse.buttons;
                ev->modifiers = 0;
            }
            return 1;
        }
    }
    ev->type = 0;
    return 0;
}

/* Always-read mouse position for continuous cursor updates.
 * Returns 1 when the position or buttons changed, 0 otherwise. */
int codeos_platform_poll_mouse_continuous(codeos_input_event_t *ev) {
    if (!ev) return -1;
    static int last_mx = -1, last_my = -1, last_mb = -1;
    static int gated = 0;
    if (lvgl_port_input_enabled()) {
        gated = 1;
        return 0;
    }
    if (gated) { /* launcher just closed: force cursor refresh */
        gated = 0;
        last_mx = -1;
        last_my = -1;
        last_mb = -1;
    }
    ev->mouse_x = mouse_get_x();
    ev->mouse_y = mouse_get_y();
    ev->mouse_buttons = mouse_get_buttons();
    ev->type = 2;
    if (ev->mouse_x != last_mx || ev->mouse_y != last_my || ev->mouse_buttons != last_mb) {
        last_mx = ev->mouse_x;
        last_my = ev->mouse_y;
        last_mb = ev->mouse_buttons;
        return 1;
    }
    return 0;
}

/* Draw a simple arrow cursor at (x,y) on the framebuffer */
void codeos_draw_cursor(int x, int y) {
    extern void fb_cursor_move(int x, int y);
    extern void fb_cursor_render(void);
    fb_cursor_move(x, y);
    fb_cursor_render();
}

void codeos_platform_sleep_ms(int ms) {
    if (ms > 0) timer_sleep_ms((uint32_t)ms);
}

uint64_t codeos_platform_tick_ms(void) {
    return timer_get_milliseconds();
}

int codeos_platform_has_input(void) {
    if (lvgl_port_input_enabled()) return 0;
    return input_available();
}

void codeos_platform_get_mouse(int *x, int *y, int *buttons) {
    if (x) *x = mouse_get_x();
    if (y) *y = mouse_get_y();
    if (buttons) *buttons = mouse_get_buttons();
}

void codeos_platform_get_keyboard(int *key, int *pressed) {
    (void)key;
    (void)pressed;
}

void *codeos_platform_get_framebuffer(void) {
    return fb_get_active_buffer();
}

int codeos_platform_get_fb_width(void) {
    return fb_getwidth();
}

int codeos_platform_get_fb_height(void) {
    return fb_getheight();
}

int codeos_platform_get_fb_pitch(void) {
    return fb_get_pitch();
}

uint64_t codeos_window_create(int x, int y, int width, int height, const char *title) {
    (void)x; (void)y; (void)width; (void)height; (void)title;
    static uint64_t next_id = 100;
    return next_id++;
}

void codeos_window_destroy(uint64_t handle) { (void)handle; }
void codeos_window_set_title(uint64_t handle, const char *title) { (void)handle; (void)title; }
void codeos_window_set_geometry(uint64_t handle, int x, int y, int w, int h) { (void)handle; (void)x; (void)y; (void)w; (void)h; }
void codeos_window_show(uint64_t handle) { (void)handle; }
void codeos_window_hide(uint64_t handle) { (void)handle; }
void codeos_window_raise(uint64_t handle) { (void)handle; }
void codeos_window_lower(uint64_t handle) { (void)handle; }
void codeos_window_request_redraw(uint64_t handle) { (void)handle; }

void codeos_draw_rect(uint64_t window, int x, int y, int w, int h, uint32_t color) {
    uint32_t *fb = fb_get_active_buffer();
    if (!fb) return;
    int pitch = fb_get_pitch() / 4;
    for (int row = 0; row < h && (y + row) < (int)fb_info.height; row++) {
        for (int col = 0; col < w && (x + col) < (int)fb_info.width; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < (int)fb_info.width && py >= 0 && py < (int)fb_info.height)
                fb[py * pitch + px] = color;
        }
    }
    (void)window;
}

void codeos_fill_rect(uint64_t window, int x, int y, int w, int h, uint32_t color) {
    codeos_draw_rect(window, x, y, w, h, color);
}

void codeos_draw_rect_outline(uint64_t window, int x, int y, int w, int h, uint32_t color, int thickness) {
    (void)thickness;
    codeos_draw_rect(window, x, y, w, thickness, color);
    codeos_draw_rect(window, x, y + h - thickness, w, thickness, color);
    codeos_draw_rect(window, x, y, thickness, h, color);
    codeos_draw_rect(window, x + w - thickness, y, thickness, h, color);
}

void codeos_draw_text(uint64_t window, int x, int y, const char *text, uint32_t color, int font_size) {
    (void)window; (void)x; (void)y; (void)text; (void)color; (void)font_size;
}

void codeos_composite(uint64_t window) {
    (void)window;
}

void codeos_clear(uint64_t window, uint32_t color) {
    uint32_t *fb = fb_get_active_buffer();
    if (!fb) return;
    int pitch = fb_get_pitch() / 4;
    for (int y = 0; y < (int)fb_info.height; y++) {
        for (int x = 0; x < (int)fb_info.width; x++) {
            fb[y * pitch + x] = color;
        }
    }
    (void)window;
}

void codeos_draw_rounded_rect(uint64_t window, int x, int y, int w, int h, int radius, uint32_t color) {
    (void)window; (void)x; (void)y; (void)w; (void)h; (void)radius; (void)color;
}

void codeos_draw_line(uint64_t window, int x1, int y1, int x2, int y2, uint32_t color, int thickness) {
    (void)window; (void)x1; (void)y1; (void)x2; (void)y2; (void)color; (void)thickness;
}

void codeos_draw_circle(uint64_t window, int x, int y, int radius, uint32_t color) {
    (void)window; (void)x; (void)y; (void)radius; (void)color;
}

void codeos_fill_rounded_rect(uint64_t window, int x, int y, int w, int h, int radius, uint32_t color) {
    (void)window; (void)x; (void)y; (void)w; (void)h; (void)radius; (void)color;
}

void codeos_draw_gradient_v(uint64_t window, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    uint32_t *fb = fb_get_active_buffer();
    if (!fb) return;
    int pitch = fb_get_pitch() / 4;
    for (int row = 0; row < h && (y + row) < (int)fb_info.height; row++) {
        float t = h > 1 ? (float)row / (h - 1) : 0.5f;
        uint32_t r = ((c1 >> 16) & 0xFF) + (uint32_t)((((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t);
        uint32_t g = ((c1 >> 8) & 0xFF) + (uint32_t)((((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t);
        uint32_t b = (c1 & 0xFF) + (uint32_t)(((c2 & 0xFF) - (c1 & 0xFF)) * t);
        uint32_t color = (r << 16) | (g << 8) | b;
        for (int col = 0; col < w && (x + col) < (int)fb_info.width; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < (int)fb_info.width && py >= 0 && py < (int)fb_info.height)
                fb[py * pitch + px] = color;
        }
    }
    (void)window;
}

void codeos_draw_gradient_h(uint64_t window, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    uint32_t *fb = fb_get_active_buffer();
    if (!fb) return;
    int pitch = fb_get_pitch() / 4;
    for (int row = 0; row < h && (y + row) < (int)fb_info.height; row++) {
        for (int col = 0; col < w && (x + col) < (int)fb_info.width; col++) {
            float t = w > 1 ? (float)col / (w - 1) : 0.5f;
            uint32_t r = ((c1 >> 16) & 0xFF) + (uint32_t)((((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t);
            uint32_t g = ((c1 >> 8) & 0xFF) + (uint32_t)((((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t);
            uint32_t b = (c1 & 0xFF) + (uint32_t)(((c2 & 0xFF) - (c1 & 0xFF)) * t);
            uint32_t color = (r << 16) | (g << 8) | b;
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < (int)fb_info.width && py >= 0 && py < (int)fb_info.height)
                fb[py * pitch + px] = color;
        }
    }
    (void)window;
}

void codeos_draw_shadow(uint64_t window, int x, int y, int w, int h, int radius, uint32_t color, int blur) {
    (void)window; (void)x; (void)y; (void)w; (void)h; (void)radius; (void)color; (void)blur;
}

void codeos_draw_image(uint64_t window, int x, int y, const uint32_t *pixels, int img_w, int img_h) {
    uint32_t *fb = fb_get_active_buffer();
    if (!fb || !pixels) return;
    int pitch = fb_get_pitch() / 4;
    for (int row = 0; row < img_h && (y + row) < (int)fb_info.height; row++) {
        for (int col = 0; col < img_w && (x + col) < (int)fb_info.width; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < (int)fb_info.width && py >= 0 && py < (int)fb_info.height) {
                uint32_t src = pixels[row * img_w + col];
                uint8_t a = (src >> 24) & 0xFF;
                if (a == 0xFF) {
                    fb[py * pitch + px] = src;
                } else if (a > 0) {
                    uint32_t dst = fb[py * pitch + px];
                    uint32_t r = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * (255 - a)) / 255;
                    uint32_t g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * (255 - a)) / 255;
                    uint32_t b = ((src & 0xFF) * a + (dst & 0xFF) * (255 - a)) / 255;
                    fb[py * pitch + px] = (r << 16) | (g << 8) | b;
                }
            }
        }
    }
    (void)window;
}

void codeos_draw_glyph(uint64_t window, int x, int y, const uint8_t *glyph, int gw, int gh, uint32_t color) {
    (void)window; (void)x; (void)y; (void)glyph; (void)gw; (void)gh; (void)color;
}

int codeos_platform_blit(int x, int y, int w, int h, const uint32_t *pixels, int stride_pixels) {
    uint32_t *fb = fb_get_active_buffer();
    if (!fb || !pixels || w <= 0 || h <= 0) return -1;
    int pitch = fb_get_pitch() / 4;
    int fb_w = fb_info.width;
    int fb_h = fb_info.height;
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= fb_h) continue;
        uint32_t *dst = &fb[py * pitch + x];
        const uint32_t *src = &pixels[row * stride_pixels];
        int copy_w = w;
        if (x < 0) { src += (-x); dst += (-x); copy_w += x; }
        if (x + copy_w > fb_w) copy_w = fb_w - x;
        if (copy_w <= 0) continue;

        /* Qt backing stores are Format_ARGB32_Premultiplied (byte0=B,1=G,2=R,
         * 3=A). Opaque runs are a plain copy; translucent pixels must be
         * composited over whatever is already in the framebuffer, otherwise
         * glass panels and opacity animations show up dark and visibly
         * "breathe" against the wallpaper (raw memcpy discards the alpha). */
        int has_alpha = 0;
        for (int i = 0; i < copy_w; i++) {
            if (((src[i] >> 24) & 0xFF) < 250) { has_alpha = 1; break; }
        }
        if (!has_alpha) {
            memcpy(dst, src, (size_t)copy_w * 4);
            continue;
        }
        for (int i = 0; i < copy_w; i++) {
            uint32_t p = src[i];
            uint32_t a = (p >> 24) & 0xFF;
            if (a >= 250) { dst[i] = p; continue; }
            if (a == 0) { continue; }  /* fully transparent: leave fb as-is */
            uint32_t sr = (p >> 16) & 0xFF;
            uint32_t sg = (p >> 8) & 0xFF;
            uint32_t sb = p & 0xFF;
            uint32_t dr = (dst[i] >> 16) & 0xFF;
            uint32_t dg = (dst[i] >> 8) & 0xFF;
            uint32_t db = dst[i] & 0xFF;
            uint32_t ia = 255 - a;
            uint32_t or_ = sr + (dr * ia) / 255;
            uint32_t og = sg + (dg * ia) / 255;
            uint32_t ob = sb + (db * ia) / 255;
            dst[i] = (or_ << 16) | (og << 8) | ob;
        }
    }
    return 0;
}
