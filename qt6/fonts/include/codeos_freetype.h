/* FreeType-compatible stubs for Qt6 on CodeOS
 * Minimal API surface to satisfy Qt6's font engine */

#ifndef CODEOS_FREETYPE_H
#define CODEOS_FREETYPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic FreeType types */
typedef void *FT_Library;
typedef void *FT_Face;
typedef long FT_Error;
typedef long FT_Long;
typedef unsigned long FT_ULong;
typedef long FT_Fixed;
typedef long FT_Pos;
typedef int FT_Int;
typedef unsigned int FT_UInt;
typedef short FT_Short;

/* Pixel modes */
#define FT_PIXEL_MODE_NONE 0
#define FT_PIXEL_MODE_MONO 1
#define FT_PIXEL_MODE_GRAY 2

/* Load flags */
#define FT_LOAD_DEFAULT 0
#define FT_LOAD_NO_HINTING 2
#define FT_LOAD_RENDER 4
#define FT_LOAD_NO_BITMAP 8

/* Render modes */
#define FT_RENDER_MODE_NORMAL 0
#define FT_RENDER_MODE_LIGHT 1

/* Face flags */
#define FT_FACE_FLAG_SCALABLE 1
#define FT_FACE_FLAG_FIXED_SIZES 8

/* Glyph format */
#define FT_GLYPH_FORMAT_NONE 0
#define FT_GLYPH_FORMAT_BITMAP 3

/* Bitmap structure */
typedef struct {
    int rows;
    int width;
    int pitch;
    unsigned char *buffer;
    int num_grays;
    int pixel_mode;
} FT_Bitmap;

/* Glyph metrics */
typedef struct {
    FT_Pos width;
    FT_Pos height;
    FT_Pos horiBearingX;
    FT_Pos horiBearingY;
    FT_Pos horiAdvance;
    FT_Pos vertBearingX;
    FT_Pos vertBearingY;
    FT_Pos vertAdvance;
} FT_Glyph_Metrics;

/* Size metrics */
typedef struct {
    FT_Fixed x_ppem;
    FT_Fixed y_ppem;
    FT_Pos x_scale;
    FT_Pos y_scale;
} FT_Size_Metrics;

/* FaceRec structure */
typedef struct {
    long num_faces;
    long face_index;
    int flags;
    int num_glyphs;
    int num_charmaps;
    void *charmaps;
    char *family_name;
    char *style_name;
    FT_Int units_per_EM;
    FT_Short ascender;
    FT_Short descender;
    FT_Short height;
    FT_Short max_advance_width;
    FT_Short max_advance_height;
    FT_Short underline_position;
    FT_Short underline_thickness;
    FT_Size_Metrics size;
} FT_FaceRec;

/* GlyphSlotRec structure */
typedef struct {
    FT_Face face;
    FT_Long glyph_index;
    FT_Int linearHoriAdvance;
    FT_Int linearVertAdvance;
    FT_Glyph_Metrics metrics;
    FT_Bitmap bitmap;
    int bitmap_left;
    int bitmap_top;
    int format;
} FT_GlyphSlotRec;

/* SizeRec structure */
typedef struct {
    FT_Face face;
    FT_Size_Metrics metrics;
} FT_SizeRec;

/* Library init/cleanup */
FT_Error FT_Init_FreeType(FT_Library *library);
FT_Error FT_Done_FreeType(FT_Library library);

/* Face operations */
FT_Error FT_New_Face(FT_Library library, const char *pathname,
                      FT_Long face_index, FT_Face *face);
FT_Error FT_New_Memory_Face(FT_Library library,
                            const unsigned char *data, FT_Long size,
                            FT_Long face_index, FT_Face *face);
FT_Error FT_Done_Face(FT_Face face);
FT_Error FT_Set_Pixel_Sizes(FT_Face face, FT_UInt width, FT_UInt height);
FT_Error FT_Set_Char_Size(FT_Face face, FT_Fixed width, FT_Fixed height,
                          FT_UInt horz_res, FT_UInt vert_res);

/* Glyph operations */
FT_Error FT_Load_Glyph(FT_Face face, FT_UInt glyph_index, FT_Int flags);
FT_Error FT_Load_Char(FT_Face face, FT_ULong char_code, FT_Int flags);
FT_Error FT_Render_Glyph(void *slot, int render_mode);
FT_UInt FT_Get_Char_Index(FT_Face face, FT_ULong charcode);
FT_ULong FT_Get_Next_Char(FT_Face face, FT_ULong charcode, FT_UInt *gindex);

/* Math */
#define FT_MulFix(a, b) (((a) * (b)) >> 16)
#define FT_FloorFix(x)  ((x) & ~0xFFFFL)
#define FT_CeilFix(x)   (((x) + 0xFFFFL) & ~0xFFFFL)

/* Error codes */
#define FT_Err_Ok 0
#define FT_Err_Cannot_Open_Resource 1
#define FT_Err_Unknown_File_Format 2

#ifdef __cplusplus
}
#endif

#endif /* CODEOS_FREETYPE_H */
