/* CodeOS Desktop - Renderer
 * High-quality rendering with rounded corners, shadows, and blur */

#ifndef CODEOS_RENDERER_H
#define CODEOS_RENDERER_H

#include <stdint.h>

/* Rendering primitives */
void renderer_init(uint32_t *framebuffer, int width, int height, int stride);

/* Basic drawing */
void renderer_clear(uint32_t color);
void renderer_pixel(int x, int y, uint32_t color);
void renderer_hline(int x, int y, int w, uint32_t color);
void renderer_vline(int x, int y, int h, uint32_t color);
void renderer_rect(int x, int y, int w, int h, uint32_t color);
void renderer_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness);

/* Rounded rectangle */
void renderer_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void renderer_rounded_rect_outline(int x, int y, int w, int h, int radius, uint32_t color, int thickness);

/* Gradient fills */
void renderer_gradient_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void renderer_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2);

/* Circle and ellipse */
void renderer_circle(int cx, int cy, int r, uint32_t color);
void renderer_circle_filled(int cx, int cy, int r, uint32_t color);
void renderer_ellipse(int cx, int cy, int rx, int ry, uint32_t color);

/* Text rendering */
void renderer_text(int x, int y, const char *text, uint32_t color, int scale);
void renderer_text_centered(int x, int y, int w, const char *text, uint32_t color, int scale);
int  renderer_text_width(const char *text, int scale);

/* Shadow effect */
void renderer_shadow(int x, int y, int w, int h, int radius, int blur, uint32_t color);

/* Blur effects */
void renderer_blur_region(int x, int y, int w, int h, int radius);

/* Alpha blending */
void renderer_set_alpha(uint8_t alpha);
uint8_t renderer_get_alpha(void);

/* Clipping */
void renderer_set_clip(int x, int y, int w, int h);
void renderer_clear_clip(void);

/* Double buffering */
void renderer_begin(void);
void renderer_end(void);

/* Icon drawing */
void renderer_icon(int x, int y, int size, const uint32_t *icon_data, int icon_w, int icon_h);

/* Image drawing */
void renderer_image(int x, int y, int w, int h, const uint32_t *data);

/* Animation helpers */
int  renderer_ease_out_cubic(int t, int start, int end, int duration);
int  renderer_ease_in_out_cubic(int t, int start, int end, int duration);
int  renderer_ease_out_back(int t, int start, int end, int duration);

#endif /* CODEOS_RENDERER_H */
