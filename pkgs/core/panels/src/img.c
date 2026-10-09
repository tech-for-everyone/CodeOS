extern "C" {
#include "img.h"
#include <stdint.h>
#include <string.h>
}

/* Decode a PPM P6 image into a 32-bit RGBA buffer.
   Returns 0 on success, -1 on error.
   The buffer must be at least w * h * 4 bytes. */
int img_decode_ppm(const uint8_t *data, int len, uint32_t *buf, int buf_w, int buf_h) {
    if (!data || !buf || len < 0 || buf_w <= 0 || buf_h <= 0 || len != buf_w * buf_h * 3) {
        return -1;
    }

    for (int y = 0; y < buf_h; y++) {
        for (int x = 0; x < buf_w; x++) {
            int src_idx = (y * buf_w + x) * 3;
            uint8_t r = data[src_idx];
            uint8_t g = data[src_idx + 1];
            uint8_t b = data[src_idx + 2];
            uint8_t a = 255; /* opaque */

            int dst_idx = (y * buf_w + x);
            buf[dst_idx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    return 0;
}

/* Render a procedural sunset mountain scene into an RGBA buffer.
   The scene is drawn to fill the specified dimensions. */
void img_render_sunset(uint32_t *buf, int stride, int buf_w, int buf_h) {
    (void)stride;

    for (int y = 0; y < buf_h; y++) {
        for (int x = 0; x < buf_w; x++) {
            /* Simple gradient sky */
            uint8_t sky_r = (uint8_t)(255 * (buf_h - y) / buf_h);
            uint8_t sky_g = (uint8_t)(180 * (buf_h - y) / buf_h);
            uint8_t sky_b = (uint8_t)(255 * (buf_h - y) / buf_h);

            /* Mountain land */
            uint8_t land_r = 100;
            uint8_t land_g = 180;
            uint8_t land_b = 100;

            /* Horizon line */
            int horizon = buf_h * 3 / 5;

            if (y < horizon) {
                /* Sky */
                buf[y * stride + x] = ((uint32_t)255 << 24) |
                                      ((uint32_t)sky_r << 16) |
                                      ((uint32_t)sky_g << 8) | sky_b;
            } else {
                /* Land */
                buf[y * stride + x] = ((uint32_t)255 << 24) |
                                      ((uint32_t)land_r << 16) |
                                      ((uint32_t)land_g << 8) | land_b;
            }
        }
    }
}

/* Render a night sky scene (stars + moon) */
void img_render_night(uint32_t *buf, int stride, int buf_w, int buf_h) {
    (void)stride;

    for (int y = 0; y < buf_h; y++) {
        for (int x = 0; x < buf_w; x++) {
            /* Deep space background */
            buf[y * stride + x] = ((uint32_t)0x10 << 24) |
                                  ((uint32_t)0x10 << 16) |
                                  ((uint32_t)0x20 << 8) | 0x10;

            /* Random stars */
            if ((x * 7 + y * 11) % 100 < 5) {
                buf[y * stride + x] = ((uint32_t)0xFF << 24) |
                                      ((uint32_t)0xFF << 16) |
                                      ((uint32_t)0xFF << 8) | 0xFF;
            }
        }
    }
}