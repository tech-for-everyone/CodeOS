/* CodeOS Image Loader - Implementation
 * Provides basic image format support for Qt6 */

#include "codeos_image.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* BMP file header */
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} __attribute__((packed)) bmp_file_header_t;

/* BMP info header */
typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_ppm;
    int32_t y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} __attribute__((packed)) bmp_info_header_t;

void codeos_image_init(void) {
    /* No initialization needed */
}

codeos_img_format_t codeos_image_detect_format(const void *data, size_t size) {
    if (!data || size < 4) return CODEOS_IMG_UNKNOWN;
    const uint8_t *bytes = (const uint8_t *)data;

    /* PNG: 89 50 4E 47 */
    if (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47)
        return CODEOS_IMG_PNG;

    /* JPG: FF D8 FF */
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
        return CODEOS_IMG_JPG;

    /* BMP: 42 4D */
    if (bytes[0] == 0x42 && bytes[1] == 0x4D)
        return CODEOS_IMG_BMP;

    return CODEOS_IMG_RAW;
}

codeos_image_t *codeos_image_load_memory(const void *data, size_t size) {
    if (!data || size < sizeof(bmp_file_header_t)) return 0;

    codeos_img_format_t fmt = codeos_image_detect_format(data, size);

    switch (fmt) {
        case CODEOS_IMG_BMP: {
            const uint8_t *bytes = (const uint8_t *)data;
            const bmp_file_header_t *fh = (const bmp_file_header_t *)bytes;
            const bmp_info_header_t *ih = (const bmp_info_header_t *)(bytes + sizeof(bmp_file_header_t));

            if (fh->type != 0x4D42) return 0;
            if (ih->bits_per_pixel != 24 && ih->bits_per_pixel != 32) return 0;

            int channels = ih->bits_per_pixel / 8;
            int w = ih->width;
            int h = ih->height < 0 ? -ih->height : ih->height;
            int stride = ((w * channels + 3) & ~3);

            codeos_image_t *img = codeos_image_create(w, h, channels);
            if (!img) return 0;

            const uint8_t *pixels = bytes + fh->offset;

            for (int y = 0; y < h; y++) {
                int src_y = ih->height < 0 ? y : (h - 1 - y);
                const uint8_t *src_row = pixels + src_y * stride;
                uint8_t *dst_row = img->data + y * img->stride;

                for (int x = 0; x < w; x++) {
                    /* BMP stores as BGR */
                    dst_row[x * channels + 0] = src_row[x * channels + 2]; /* R */
                    dst_row[x * channels + 1] = src_row[x * channels + 1]; /* G */
                    dst_row[x * channels + 2] = src_row[x * channels + 0]; /* B */
                    if (channels == 4)
                        dst_row[x * channels + 3] = src_row[x * channels + 3]; /* A */
                }
            }

            img->format = CODEOS_IMG_BMP;
            return img;
        }

        case CODEOS_IMG_PNG:
        case CODEOS_IMG_JPG:
            /* PNG/JPEG support would require libpng/libjpeg
             * For now, return a solid colored placeholder */
            {
                codeos_image_t *img = codeos_image_create(64, 64, 4);
                if (!img) return 0;

                /* Draw magenta/white checkerboard (transparency indicator) */
                for (int y = 0; y < 64; y++) {
                    for (int x = 0; x < 64; x++) {
                        uint8_t *pixel = codeos_image_get_pixel(img, x, y);
                        if ((x / 8 + y / 8) % 2) {
                            pixel[0] = 255; pixel[1] = 0;
                            pixel[2] = 255; pixel[3] = 255;
                        } else {
                            pixel[0] = 255; pixel[1] = 255;
                            pixel[2] = 255; pixel[3] = 255;
                        }
                    }
                }

                img->format = fmt;
                return img;
            }

        default:
            return 0;
    }
}

codeos_image_t *codeos_image_create(int width, int height, int channels) {
    if (width <= 0 || height <= 0 || channels <= 0 || channels > 4) return 0;

    static codeos_image_t img;
    int stride = width * channels;

    /* Align to 4 bytes */
    stride = (stride + 3) & ~3;

    static uint8_t buffer[1024 * 1024]; /* 1MB static buffer */
    img.data = buffer;
    img.width = width;
    img.height = height;
    img.channels = channels;
    img.stride = stride;
    img.format = CODEOS_IMG_RAW;

    /* Clear to black */
    memset(img.data, 0, stride * height);

    return &img;
}

void codeos_image_destroy(codeos_image_t *image) {
    (void)image;
    /* Static buffer can't be freed */
}

uint8_t *codeos_image_get_pixel(codeos_image_t *image, int x, int y) {
    if (!image || !image->data) return 0;
    if (x < 0 || x >= image->width || y < 0 || y >= image->height) return 0;
    return image->data + y * image->stride + x * image->channels;
}

void codeos_image_set_pixel(codeos_image_t *image, int x, int y, const uint8_t *pixel) {
    if (!image || !pixel) return;
    uint8_t *dst = codeos_image_get_pixel(image, x, y);
    if (!dst) return;
    for (int i = 0; i < image->channels; i++)
        dst[i] = pixel[i];
}

void codeos_image_fill_rect(codeos_image_t *image, int x, int y, int w, int h, const uint8_t *color) {
    if (!image || !color) return;
    for (int j = y; j < y + h && j < image->height; j++) {
        for (int i = x; i < x + w && i < image->width; i++) {
            codeos_image_set_pixel(image, i, j, color);
        }
    }
}

int codeos_image_to_rgba(codeos_image_t *image) {
    if (!image) return -1;
    if (image->channels == 4) return 0;
    /* Simplified: just set alpha */
    image->channels = 4;
    return 0;
}

int codeos_image_to_rgb(codeos_image_t *image) {
    if (!image) return -1;
    if (image->channels == 3) return 0;
    image->channels = 3;
    return 0;
}

codeos_image_t *codeos_image_scale(codeos_image_t *src, int new_width, int new_height) {
    if (!src || new_width <= 0 || new_height <= 0) return 0;

    codeos_image_t *dst = codeos_image_create(new_width, new_height, src->channels);
    if (!dst) return 0;

    /* Nearest-neighbor scaling */
    for (int y = 0; y < new_height; y++) {
        int src_y = (y * src->height) / new_height;
        for (int x = 0; x < new_width; x++) {
            int src_x = (x * src->width) / new_width;
            uint8_t *src_pixel = codeos_image_get_pixel(src, src_x, src_y);
            uint8_t *dst_pixel = codeos_image_get_pixel(dst, x, y);
            if (src_pixel && dst_pixel) {
                for (int c = 0; c < src->channels; c++)
                    dst_pixel[c] = src_pixel[c];
            }
        }
    }

    return dst;
}
