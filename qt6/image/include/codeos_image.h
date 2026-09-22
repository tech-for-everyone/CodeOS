/* CodeOS Image Loader for Qt6
 * Provides minimal image format support */

#ifndef CODEOS_IMAGE_H
#define CODEOS_IMAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Image format */
typedef enum {
    CODEOS_IMG_UNKNOWN = 0,
    CODEOS_IMG_PNG,
    CODEOS_IMG_JPG,
    CODEOS_IMG_BMP,
    CODEOS_IMG_RAW
} codeos_img_format_t;

/* Image info */
typedef struct {
    int width;
    int height;
    int channels;    /* 3=RGB, 4=RGBA */
    int bits_per_channel;
    codeos_img_format_t format;
} codeos_img_info_t;

/* Image data */
typedef struct {
    uint8_t *data;
    int width;
    int height;
    int channels;
    int stride;
    codeos_img_format_t format;
} codeos_image_t;

/* Initialize image system */
void codeos_image_init(void);

/* Detect format from magic bytes */
codeos_img_format_t codeos_image_detect_format(const void *data, size_t size);

/* Load image from memory */
codeos_image_t *codeos_image_load_memory(const void *data, size_t size);

/* Create image with specified dimensions */
codeos_image_t *codeos_image_create(int width, int height, int channels);

/* Destroy image */
void codeos_image_destroy(codeos_image_t *image);

/* Get pixel */
uint8_t *codeos_image_get_pixel(codeos_image_t *image, int x, int y);

/* Set pixel */
void codeos_image_set_pixel(codeos_image_t *image, int x, int y, const uint8_t *pixel);

/* Fill rectangle */
void codeos_image_fill_rect(codeos_image_t *image, int x, int y, int w, int h, const uint8_t *color);

/* Convert to RGBA */
int codeos_image_to_rgba(codeos_image_t *image);

/* Convert to RGB */
int codeos_image_to_rgb(codeos_image_t *image);

/* Scale image */
codeos_image_t *codeos_image_scale(codeos_image_t *src, int new_width, int new_height);

#ifdef __cplusplus
}
#endif

#endif /* CODEOS_IMAGE_H */
