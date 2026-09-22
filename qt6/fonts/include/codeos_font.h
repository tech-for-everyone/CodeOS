/* CodeOS Font Engine for Qt6
 * Wraps CodeOS's bitmap font and provides FreeType-compatible API */

#ifndef CODEOS_FONT_H
#define CODEOS_FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Font metrics */
typedef struct {
    int width;
    int height;
    int ascent;
    int descent;
    int leading;
} codeos_font_metrics_t;

/* Glyph metrics */
typedef struct {
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance;
} codeos_glyph_metrics_t;

/* Font face */
typedef struct {
    const uint8_t *bitmap;
    int glyph_width;
    int glyph_height;
    int num_glyphs;
    codeos_font_metrics_t metrics;
} codeos_font_face_t;

/* Initialize font system */
void codeos_font_init(void);

/* Create font face from bitmap data */
codeos_font_face_t *codeos_font_create_bitmap(
    const uint8_t *bitmap,
    int glyph_width,
    int glyph_height,
    int num_glyphs
);

/* Destroy font face */
void codeos_font_destroy(codeos_font_face_t *face);

/* Get glyph bitmap */
const uint8_t *codeos_font_get_glyph(
    codeos_font_face_t *face,
    int codepoint
);

/* Get glyph metrics */
int codeos_font_get_glyph_metrics(
    codeos_font_face_t *face,
    int codepoint,
    codeos_glyph_metrics_t *metrics
);

/* Get font metrics */
void codeos_font_get_metrics(
    codeos_font_face_t *face,
    codeos_font_metrics_t *metrics
);

/* Measure text width */
int codeos_font_measure_text(
    codeos_font_face_t *face,
    const char *text,
    int length
);

/* Built-in 8x16 font (from kernel) */
extern const uint8_t codeos_font8x16[256][16];

/* Default font face */
codeos_font_face_t *codeos_font_default(void);

#ifdef __cplusplus
}
#endif

#endif /* CODEOS_FONT_H */
