/* CodeOS Qt6 Platform Integration Plugin
 * Bridges Qt6 to CodeOS's framebuffer-based GUI system */

#ifndef QPLATFORMINTEGRATION_CODEOS_H
#define QPLATFORMINTEGRATION_CODEOS_H

#include <stdint.h>

/* Forward declarations */
struct QPlatformIntegration;
struct QPlatformScreen;
struct QPlatformWindow;
struct QPlatformBackingStore;
struct QPlatformClipboard;
struct QPlatformNativeInterface;
struct QPlatformEventLoopIntegration;

/* CodeOS framebuffer info (matches kernel's fb_info_t) */
typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
} codeos_fb_info_t;

/* CodeOS input event (matches kernel's input_event_t) */
typedef struct {
    int type;    /* 0=none, 1=key, 2=mouse */
    int key;
    int mouse_x;
    int mouse_y;
    int mouse_buttons;
    int modifiers;  /* kernel key modifiers: 1=shift 2=ctrl 4=alt 8=super */
} codeos_input_event_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Platform functions - called by Qt6 */
extern int codeos_platform_init(void);
extern void codeos_platform_cleanup(void);
extern int codeos_platform_get_fb_info(codeos_fb_info_t *info);
extern int codeos_platform_poll_input(codeos_input_event_t *ev);
extern int codeos_platform_poll_mouse_continuous(codeos_input_event_t *ev);
extern void codeos_draw_cursor(int x, int y);
extern void codeos_platform_sleep_ms(int ms);
extern uint64_t codeos_platform_tick_ms(void);
extern int codeos_platform_has_input(void);
extern void codeos_platform_get_mouse(int *x, int *y, int *buttons);
extern void codeos_platform_get_keyboard(int *key, int *pressed);
extern void *codeos_platform_get_framebuffer(void);
extern int codeos_platform_get_fb_width(void);
extern int codeos_platform_get_fb_height(void);
extern int codeos_platform_get_fb_pitch(void);

/* Debug: dump raw QPlatformTheme/QPalette fields (for corruption tracing) */
extern void codeos_platform_dump_theme(const void *theme);

/* Window management */
extern uint64_t codeos_window_create(int x, int y, int width, int height, const char *title);
extern void codeos_window_destroy(uint64_t handle);
extern void codeos_window_set_title(uint64_t handle, const char *title);
extern void codeos_window_set_geometry(uint64_t handle, int x, int y, int w, int h);
extern void codeos_window_show(uint64_t handle);
extern void codeos_window_hide(uint64_t handle);
extern void codeos_window_raise(uint64_t handle);
extern void codeos_window_lower(uint64_t handle);
extern void codeos_window_request_redraw(uint64_t handle);

/* Drawing primitives (maps to CodeOS xserver compositor) */
extern void codeos_draw_rect(uint64_t window, int x, int y, int w, int h, uint32_t color);
extern void codeos_draw_rect_outline(uint64_t window, int x, int y, int w, int h, uint32_t color, int thickness);
extern void codeos_draw_rounded_rect(uint64_t window, int x, int y, int w, int h, int radius, uint32_t color);
extern void codeos_draw_line(uint64_t window, int x1, int y1, int x2, int y2, uint32_t color, int thickness);
extern void codeos_draw_text(uint64_t window, int x, int y, const char *text, uint32_t color, int font_size);
extern void codeos_draw_circle(uint64_t window, int x, int y, int radius, uint32_t color);
extern void codeos_fill_rect(uint64_t window, int x, int y, int w, int h, uint32_t color);
extern void codeos_fill_rounded_rect(uint64_t window, int x, int y, int w, int h, int radius, uint32_t color);
extern void codeos_draw_gradient_v(uint64_t window, int x, int y, int w, int h, uint32_t c1, uint32_t c2);
extern void codeos_draw_gradient_h(uint64_t window, int x, int y, int w, int h, uint32_t c1, uint32_t c2);
extern void codeos_draw_shadow(uint64_t window, int x, int y, int w, int h, int radius, uint32_t color, int blur);
extern void codeos_draw_image(uint64_t window, int x, int y, const uint32_t *pixels, int img_w, int img_h);
extern void codeos_draw_glyph(uint64_t window, int x, int y, const uint8_t *glyph, int gw, int gh, uint32_t color);
extern void codeos_clear(uint64_t window, uint32_t color);
extern void codeos_composite(uint64_t window);

/* Framebuffer blit - fast pixel copy for backing store flush */
extern int codeos_platform_blit(int x, int y, int w, int h, const uint32_t *pixels, int stride_pixels);

#ifdef __cplusplus
}
#endif

/* Color helpers */
#define CODEOS_COLOR(r, g, b) ((r) | ((g) << 8) | ((b) << 16))
#define CODEOS_COLOR_RGBA(r, g, b, a) ((r) | ((g) << 8) | ((b) << 16) | ((a) << 24))

#endif /* QPLATFORMINTEGRATION_CODEOS_H */
