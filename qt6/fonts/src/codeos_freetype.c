/* FreeType stubs for Qt6 on CodeOS
 * Provides minimal FreeType API using CodeOS's bitmap font engine */

#include "codeos_freetype.h"
#include "codeos_font.h"
#include <stdint.h>
#include <stddef.h>

/* Internal structures */
typedef struct {
    int dummy;
} ft_library_rec;

typedef struct {
    FT_FaceRec base;
    codeos_font_face_t *font;
    FT_GlyphSlotRec glyph_slot;
    FT_SizeRec size;
} ft_face_rec;

/* Library init/cleanup */
FT_Error FT_Init_FreeType(FT_Library *library) {
    (void)library;
    return FT_Err_Ok;
}

FT_Error FT_Done_FreeType(FT_Library library) {
    (void)library;
    return FT_Err_Ok;
}

/* Face operations */
FT_Error FT_New_Face(FT_Library library, const char *pathname,
                      FT_Long face_index, FT_Face *face) {
    (void)library;
    (void)pathname;
    (void)face_index;

    /* Use the built-in 8x16 font for all faces */
    static ft_face_rec rec;
    rec.base.num_faces = 1;
    rec.base.face_index = 0;
    rec.base.flags = FT_FACE_FLAG_SCALABLE;
    rec.base.num_glyphs = 256;
    rec.base.family_name = "CodeOS";
    rec.base.style_name = "Regular";
    rec.base.units_per_EM = 16;
    rec.base.ascender = 12;
    rec.base.descender = -4;
    rec.base.height = 16;
    rec.base.max_advance_width = 8;
    rec.base.max_advance_height = 16;
    rec.base.underline_position = -1;
    rec.base.underline_thickness = 1;

    rec.font = codeos_font_default();

    *face = (FT_Face)&rec;
    return FT_Err_Ok;
}

FT_Error FT_New_Memory_Face(FT_Library library,
                            const unsigned char *data, FT_Long size,
                            FT_Long face_index, FT_Face *face) {
    (void)data;
    (void)size;
    return FT_New_Face(library, "", face_index, face);
}

FT_Error FT_Done_Face(FT_Face face) {
    (void)face;
    return FT_Err_Ok;
}

FT_Error FT_Set_Pixel_Sizes(FT_Face face, FT_UInt width, FT_UInt height) {
    (void)face;
    (void)width;
    (void)height;
    return FT_Err_Ok;
}

FT_Error FT_Set_Char_Size(FT_Face face, FT_Fixed width, FT_Fixed height,
                          FT_UInt horz_res, FT_UInt vert_res) {
    (void)face;
    (void)width;
    (void)height;
    (void)horz_res;
    (void)vert_res;
    return FT_Err_Ok;
}

/* Glyph operations */
FT_Error FT_Load_Glyph(FT_Face face, FT_UInt glyph_index, FT_Int flags) {
    if (!face) return FT_Err_Ok;

    ft_face_rec *rec = (ft_face_rec *)face;
    codeos_glyph_metrics_t metrics;

    codeos_font_get_glyph_metrics(rec->font, glyph_index, &metrics);

    rec->glyph_slot.face = face;
    rec->glyph_slot.glyph_index = glyph_index;
    rec->glyph_slot.metrics.width = metrics.width << 6;
    rec->glyph_slot.metrics.height = metrics.height << 6;
    rec->glyph_slot.metrics.horiBearingX = metrics.bearing_x << 6;
    rec->glyph_slot.metrics.horiBearingY = metrics.bearing_y << 6;
    rec->glyph_slot.metrics.horiAdvance = metrics.advance << 6;
    rec->glyph_slot.format = FT_GLYPH_FORMAT_BITMAP;

    /* Get glyph bitmap */
    const uint8_t *glyph = codeos_font_get_glyph(rec->font, glyph_index);
    if (glyph) {
        rec->glyph_slot.bitmap.rows = 16;
        rec->glyph_slot.bitmap.width = 8;
        rec->glyph_slot.bitmap.pitch = 1;
        rec->glyph_slot.bitmap.buffer = (unsigned char *)glyph;
        rec->glyph_slot.bitmap.num_grays = 1;
        rec->glyph_slot.bitmap.pixel_mode = FT_PIXEL_MODE_MONO;
        rec->glyph_slot.bitmap_left = 0;
        rec->glyph_slot.bitmap_top = 12;
    }

    (void)flags;
    return FT_Err_Ok;
}

FT_Error FT_Load_Char(FT_Face face, FT_ULong char_code, FT_Int flags) {
    return FT_Load_Glyph(face, (FT_UInt)char_code, flags);
}

FT_Error FT_Render_Glyph(void *slot, int render_mode) {
    (void)slot;
    (void)render_mode;
    return FT_Err_Ok;
}

FT_UInt FT_Get_Char_Index(FT_Face face, FT_ULong charcode) {
    (void)face;
    return (FT_UInt)charcode;
}

FT_ULong FT_Get_Next_Char(FT_Face face, FT_ULong charcode, FT_UInt *gindex) {
    (void)face;
    if (gindex) *gindex = (FT_UInt)(charcode + 1);
    return charcode + 1;
}
