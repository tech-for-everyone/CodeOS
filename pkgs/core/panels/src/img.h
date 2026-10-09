#ifndef _IMG_H
#define _IMG_H

#include <stdint.h>

/* Decode a PPM P6 image into a 32-bit RGBA buffer.
   Returns 0 on success, -1 on error.
   The buffer must be at least w * h * 4 bytes. */
int img_decode_ppm(const uint8_t *data, int len, uint32_t *buf, int buf_w, int buf_h);

/* Render a procedural sunset mountain scene into an RGBA buffer.
   The scene is drawn to fill the specified dimensions.
   buf_w, buf_h are the buffer dimensions in pixels. */
void img_render_sunset(uint32_t *buf, int stride, int buf_w, int buf_h);

/* Render a night sky scene (stars + moon) */
void img_render_night(uint32_t *buf, int stride, int buf_w, int buf_h);

#endif
