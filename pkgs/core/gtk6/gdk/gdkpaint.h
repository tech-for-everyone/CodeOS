/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gdkpaint.h: GDK's drawing API.
 *
 * GTK6 does not use cairo.  This header is the whole of the 2D drawing /
 * imaging surface the GTK4 sources used to get from <cairo.h>, restated as
 * GDK's own types and functions so the port is a mechanical rename rather
 * than a rewrite:
 *
 *     cairo_t              -> GdkPainter
 *     cairo_surface_t      -> GdkPaintSurface
 *     cairo_pattern_t      -> GdkPaintPattern
 *     cairo_matrix_t       -> GdkPaintMatrix
 *     cairo_font_options_t -> GdkPaintFontOptions
 *     cairo_<op>()         -> gdk_paint_<op>()
 *     CAIRO_<CONST>        -> GDK_PAINT_<CONST>
 *
 * The implementations are Qt-backed (gdk/qt/gdkqtpaint.cpp): a GdkPainter is
 * a QPainter over a QImage-backed GdkPaintSurface, so GSK and the widgets
 * draw with the engine that is already freestanding in this tree instead of
 * the Linux one.  The enum values are cairo's, because they are also the
 * on-disk values of the GSK render-node serialisation.
 *
 * This header is pulled in from the end of gdktypes.h and names only itself:
 * it has no glib requirement outside the optional GType block at the bottom,
 * so the C++ backend can include it too.
 */

#ifndef GTK6_GDK_PAINT_H
#define GTK6_GDK_PAINT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GdkRectangle is defined by gdktypes.h, which pulls this header in.  The
 * redundant typedef is legal C11 (redefinition to the same type) and makes
 * gdkpaint.h self-contained for the files that included <cairo.h> and now
 * include this instead. */
typedef struct _GdkRectangle GdkRectangle;

/* ── opaque object types ───────────────────────────────────────────────── */

typedef struct _GdkPainter                GdkPainter;
typedef struct _GdkPaintSurface           GdkPaintSurface;
typedef struct _GdkPaintPattern           GdkPaintPattern;
typedef struct _GdkPaintDevice            GdkPaintDevice;
typedef struct _GdkPaintFontOptions       GdkPaintFontOptions;
typedef struct _GdkPaintScaledFont        GdkPaintScaledFont;
typedef struct _GdkPaintScriptInterpreter GdkPaintScriptInterpreter;

typedef void (*GdkPaintDestroyFunc) (void *data);

typedef struct _GdkPaintUserDataKey
{
  int unused;
} GdkPaintUserDataKey;

/* ── enums (values match cairo's, which are the GSK serialisation values) ─ */

typedef enum
{
  GDK_PAINT_STATUS_SUCCESS = 0,
  GDK_PAINT_STATUS_NO_MEMORY,
  GDK_PAINT_STATUS_INVALID_RESTORE,
  GDK_PAINT_STATUS_INVALID_POP_GROUP,
  GDK_PAINT_STATUS_NO_CURRENT_POINT,
  GDK_PAINT_STATUS_INVALID_MATRIX,
  GDK_PAINT_STATUS_INVALID_STATUS,
  GDK_PAINT_STATUS_NULL_POINTER,
  GDK_PAINT_STATUS_INVALID_STRING,
  GDK_PAINT_STATUS_INVALID_PATH_DATA,
  GDK_PAINT_STATUS_READ_ERROR,
  GDK_PAINT_STATUS_WRITE_ERROR,
  GDK_PAINT_STATUS_SURFACE_FINISHED,
  GDK_PAINT_STATUS_SURFACE_TYPE_MISMATCH,
  GDK_PAINT_STATUS_PATTERN_TYPE_MISMATCH,
  GDK_PAINT_STATUS_INVALID_CONTENT,
  GDK_PAINT_STATUS_INVALID_FORMAT,
  GDK_PAINT_STATUS_INVALID_VISUAL,
  GDK_PAINT_STATUS_FILE_NOT_FOUND,
  GDK_PAINT_STATUS_INVALID_DASH,
  GDK_PAINT_STATUS_INVALID_DSC_COMMENT,
  GDK_PAINT_STATUS_INVALID_INDEX,
  GDK_PAINT_STATUS_CLIP_NOT_REPRESENTABLE,
  GDK_PAINT_STATUS_TEMP_FILE_ERROR,
  GDK_PAINT_STATUS_INVALID_STRIDE,
  GDK_PAINT_STATUS_FONT_TYPE_MISMATCH,
  GDK_PAINT_STATUS_USER_FONT_IMMUTABLE,
  GDK_PAINT_STATUS_USER_FONT_ERROR,
  GDK_PAINT_STATUS_NEGATIVE_COUNT,
  GDK_PAINT_STATUS_INVALID_CLUSTERS,
  GDK_PAINT_STATUS_INVALID_SLANT,
  GDK_PAINT_STATUS_INVALID_WEIGHT,
  GDK_PAINT_STATUS_INVALID_SIZE,
  GDK_PAINT_STATUS_USER_FONT_NOT_IMPLEMENTED,
  GDK_PAINT_STATUS_DEVICE_TYPE_MISMATCH,
  GDK_PAINT_STATUS_DEVICE_ERROR,
  GDK_PAINT_STATUS_INVALID_MESH_CONSTRUCTION,
  GDK_PAINT_STATUS_DEVICE_FINISHED,
  GDK_PAINT_STATUS_JBIG2_GLOBAL_MISSING,
} GdkPaintStatus;

typedef enum
{
  GDK_PAINT_ANTIALIAS_DEFAULT  = 0,
  GDK_PAINT_ANTIALIAS_NONE     = 1,
  GDK_PAINT_ANTIALIAS_GRAY     = 2,
  GDK_PAINT_ANTIALIAS_SUBPIXEL = 3,
  GDK_PAINT_ANTIALIAS_FAST     = 4,
  GDK_PAINT_ANTIALIAS_GOOD     = 5,
  GDK_PAINT_ANTIALIAS_BEST     = 6,
} GdkPaintAntialias;

typedef enum
{
  GDK_PAINT_FORMAT_INVALID   = -1,
  GDK_PAINT_FORMAT_ARGB32    = 0,
  GDK_PAINT_FORMAT_RGB24     = 1,
  GDK_PAINT_FORMAT_A8        = 2,
  GDK_PAINT_FORMAT_A1        = 3,
  GDK_PAINT_FORMAT_RGB16_565 = 4,
  GDK_PAINT_FORMAT_RGB30     = 5,
  GDK_PAINT_FORMAT_RGB96F    = 6,
  GDK_PAINT_FORMAT_RGBA128F  = 7,
} GdkPaintFormat;

typedef enum
{
  GDK_PAINT_CONTENT_COLOR       = 0x1000,
  GDK_PAINT_CONTENT_ALPHA       = 0x2000,
  GDK_PAINT_CONTENT_COLOR_ALPHA = 0x3000,
} GdkPaintContent;

typedef enum
{
  GDK_PAINT_OPERATOR_CLEAR          = 0,
  GDK_PAINT_OPERATOR_SOURCE         = 1,
  GDK_PAINT_OPERATOR_OVER           = 2,
  GDK_PAINT_OPERATOR_IN             = 3,
  GDK_PAINT_OPERATOR_OUT            = 4,
  GDK_PAINT_OPERATOR_ATOP           = 5,
  GDK_PAINT_OPERATOR_DEST           = 6,
  GDK_PAINT_OPERATOR_DEST_OVER      = 7,
  GDK_PAINT_OPERATOR_DEST_IN        = 8,
  GDK_PAINT_OPERATOR_DEST_OUT       = 9,
  GDK_PAINT_OPERATOR_DEST_ATOP      = 10,
  GDK_PAINT_OPERATOR_XOR            = 11,
  GDK_PAINT_OPERATOR_ADD            = 12,
  GDK_PAINT_OPERATOR_SATURATE       = 13,
  GDK_PAINT_OPERATOR_MULTIPLY       = 14,
  GDK_PAINT_OPERATOR_SCREEN         = 15,
  GDK_PAINT_OPERATOR_OVERLAY        = 16,
  GDK_PAINT_OPERATOR_DARKEN         = 17,
  GDK_PAINT_OPERATOR_LIGHTEN        = 18,
  GDK_PAINT_OPERATOR_COLOR_DODGE    = 19,
  GDK_PAINT_OPERATOR_COLOR_BURN     = 20,
  GDK_PAINT_OPERATOR_HARD_LIGHT     = 21,
  GDK_PAINT_OPERATOR_SOFT_LIGHT     = 22,
  GDK_PAINT_OPERATOR_DIFFERENCE     = 23,
  GDK_PAINT_OPERATOR_EXCLUSION      = 24,
  GDK_PAINT_OPERATOR_HSL_HUE        = 25,
  GDK_PAINT_OPERATOR_HSL_SATURATION = 26,
  GDK_PAINT_OPERATOR_HSL_COLOR      = 27,
  GDK_PAINT_OPERATOR_HSL_LUMINOSITY = 28,
} GdkPaintOperator;

typedef enum
{
  GDK_PAINT_FILTER_FAST     = 0,
  GDK_PAINT_FILTER_GOOD     = 1,
  GDK_PAINT_FILTER_BEST     = 2,
  GDK_PAINT_FILTER_NEAREST  = 3,
  GDK_PAINT_FILTER_BILINEAR = 4,
  GDK_PAINT_FILTER_GAUSSIAN = 5,
} GdkPaintFilter;

typedef enum
{
  GDK_PAINT_EXTEND_NONE    = 0,
  GDK_PAINT_EXTEND_REPEAT  = 1,
  GDK_PAINT_EXTEND_REFLECT = 2,
  GDK_PAINT_EXTEND_PAD     = 3,
} GdkPaintExtend;

typedef enum
{
  GDK_PAINT_FILL_RULE_WINDING  = 0,
  GDK_PAINT_FILL_RULE_EVEN_ODD = 1,
} GdkPaintFillRule;

typedef enum
{
  GDK_PAINT_LINE_CAP_BUTT   = 0,
  GDK_PAINT_LINE_CAP_ROUND  = 1,
  GDK_PAINT_LINE_CAP_SQUARE = 2,
} GdkPaintLineCap;

typedef enum
{
  GDK_PAINT_LINE_JOIN_MITER = 0,
  GDK_PAINT_LINE_JOIN_ROUND = 1,
  GDK_PAINT_LINE_JOIN_BEVEL = 2,
} GdkPaintLineJoin;

typedef enum
{
  GDK_PAINT_HINT_STYLE_DEFAULT = 0,
  GDK_PAINT_HINT_STYLE_NONE    = 1,
  GDK_PAINT_HINT_STYLE_SLIGHT  = 2,
  GDK_PAINT_HINT_STYLE_MEDIUM  = 3,
  GDK_PAINT_HINT_STYLE_FULL    = 4,
} GdkPaintHintStyle;

typedef enum
{
  GDK_PAINT_HINT_METRICS_DEFAULT = 0,
  GDK_PAINT_HINT_METRICS_OFF     = 1,
  GDK_PAINT_HINT_METRICS_ON      = 2,
} GdkPaintHintMetrics;

typedef enum
{
  GDK_PAINT_SUBPIXEL_ORDER_DEFAULT = 0,
  GDK_PAINT_SUBPIXEL_ORDER_RGB     = 1,
  GDK_PAINT_SUBPIXEL_ORDER_BGR     = 2,
  GDK_PAINT_SUBPIXEL_ORDER_VRGB    = 3,
  GDK_PAINT_SUBPIXEL_ORDER_VBGR    = 4,
} GdkPaintSubpixelOrder;

typedef enum
{
  GDK_PAINT_PATH_MOVE_TO    = 0,
  GDK_PAINT_PATH_LINE_TO    = 1,
  GDK_PAINT_PATH_CURVE_TO   = 2,
  GDK_PAINT_PATH_CLOSE_PATH = 3,
} GdkPaintPathDataType;

typedef enum
{
  GDK_PAINT_SURFACE_TYPE_IMAGE         = 0,
  GDK_PAINT_SURFACE_TYPE_PDF           = 1,
  GDK_PAINT_SURFACE_TYPE_PS            = 2,
  GDK_PAINT_SURFACE_TYPE_XLIB          = 3,
  GDK_PAINT_SURFACE_TYPE_XCB           = 4,
  GDK_PAINT_SURFACE_TYPE_GLITZ         = 5,
  GDK_PAINT_SURFACE_TYPE_QUARTZ        = 6,
  GDK_PAINT_SURFACE_TYPE_WIN32         = 7,
  GDK_PAINT_SURFACE_TYPE_BEOS          = 8,
  GDK_PAINT_SURFACE_TYPE_DIRECTFB      = 9,
  GDK_PAINT_SURFACE_TYPE_SVG           = 10,
  GDK_PAINT_SURFACE_TYPE_OS2           = 11,
  GDK_PAINT_SURFACE_TYPE_WIN32_PRINTING = 12,
  GDK_PAINT_SURFACE_TYPE_QUARTZ_IMAGE  = 13,
  GDK_PAINT_SURFACE_TYPE_SCRIPT        = 14,
  GDK_PAINT_SURFACE_TYPE_QT            = 15,
  GDK_PAINT_SURFACE_TYPE_RECORDING     = 16,
  GDK_PAINT_SURFACE_TYPE_VG            = 17,
  GDK_PAINT_SURFACE_TYPE_GL            = 18,
  GDK_PAINT_SURFACE_TYPE_DRM           = 19,
  GDK_PAINT_SURFACE_TYPE_TEE           = 20,
  GDK_PAINT_SURFACE_TYPE_XML           = 21,
  GDK_PAINT_SURFACE_TYPE_SKIA          = 22,
  GDK_PAINT_SURFACE_TYPE_SUBSURFACE    = 23,
  GDK_PAINT_SURFACE_TYPE_COGL          = 24,
} GdkPaintSurfaceType;

typedef enum
{
  GDK_PAINT_SVG_UNIT_USER    = 0,
  GDK_PAINT_SVG_UNIT_EM      = 1,
  GDK_PAINT_SVG_UNIT_EX      = 2,
  GDK_PAINT_SVG_UNIT_PX      = 3,
  GDK_PAINT_SVG_UNIT_IN      = 4,
  GDK_PAINT_SVG_UNIT_CM      = 5,
  GDK_PAINT_SVG_UNIT_MM      = 6,
  GDK_PAINT_SVG_UNIT_PT      = 7,
  GDK_PAINT_SVG_UNIT_PC      = 8,
  GDK_PAINT_SVG_UNIT_PERCENT = 9,
} GdkPaintSvgUnit;

/* ── value types ───────────────────────────────────────────────────────── */

typedef struct _GdkPaintMatrix
{
  double xx, yx;
  double xy, yy;
  double x0, y0;
} GdkPaintMatrix;

typedef struct _GdkPaintRectangle
{
  double x, y;
  double width, height;
} GdkPaintRectangle;

typedef union _GdkPaintPathData
{
  struct
  {
    int type;
    int length;
  } header;
  struct
  {
    double x, y;
  } point;
} GdkPaintPathData;

typedef struct _GdkPaintPath
{
  GdkPaintStatus    status;
  GdkPaintPathData *data;
  int               num_data;
} GdkPaintPath;

/* Write callback, mirroring cairo_write_func_t. */
typedef GdkPaintStatus (*GdkPaintWriteFunc) (void                *closure,
                                             const unsigned char *data,
                                             unsigned int         length);

/* ── painter (was cairo_t) ─────────────────────────────────────────────── */

GdkPainter *     gdk_paint_create                 (GdkPaintSurface *target);
GdkPainter *     gdk_paint_reference              (GdkPainter *cr);
void             gdk_paint_destroy                (GdkPainter *cr);
GdkPaintStatus   gdk_paint_status                 (GdkPainter *cr);
const char *     gdk_paint_status_to_string       (GdkPaintStatus status);

void             gdk_paint_save                   (GdkPainter *cr);
void             gdk_paint_restore                (GdkPainter *cr);
void             gdk_paint_push_group             (GdkPainter *cr);
void             gdk_paint_push_group_with_content (GdkPainter *cr, GdkPaintContent content);
GdkPaintPattern *gdk_paint_pop_group              (GdkPainter *cr);
void             gdk_paint_pop_group_to_source    (GdkPainter *cr);
GdkPaintSurface *gdk_paint_get_target             (GdkPainter *cr);
GdkPaintSurface *gdk_paint_get_group_target       (GdkPainter *cr);
GdkPaintPattern *gdk_paint_get_source             (GdkPainter *cr);

void             gdk_paint_set_source             (GdkPainter *cr, GdkPaintPattern *source);
void             gdk_paint_set_source_rgb         (GdkPainter *cr, double red, double green, double blue);
void             gdk_paint_set_source_rgba        (GdkPainter *cr, double red, double green, double blue, double alpha);
void             gdk_paint_set_source_surface     (GdkPainter *cr, GdkPaintSurface *surface, double x, double y);
void             gdk_paint_set_operator           (GdkPainter *cr, GdkPaintOperator op);
void             gdk_paint_set_fill_rule          (GdkPainter *cr, GdkPaintFillRule fill_rule);
void             gdk_paint_set_line_width         (GdkPainter *cr, double width);
void             gdk_paint_set_line_cap           (GdkPainter *cr, GdkPaintLineCap line_cap);
void             gdk_paint_set_line_join          (GdkPainter *cr, GdkPaintLineJoin line_join);
void             gdk_paint_set_miter_limit        (GdkPainter *cr, double limit);
void             gdk_paint_set_dash               (GdkPainter *cr, const double *dashes, int num_dashes, double offset);
double           gdk_paint_get_tolerance          (GdkPainter *cr);

void             gdk_paint_translate              (GdkPainter *cr, double tx, double ty);
void             gdk_paint_scale                  (GdkPainter *cr, double sx, double sy);
void             gdk_paint_transform              (GdkPainter *cr, const GdkPaintMatrix *matrix);
void             gdk_paint_set_matrix             (GdkPainter *cr, const GdkPaintMatrix *matrix);
void             gdk_paint_get_matrix             (GdkPainter *cr, GdkPaintMatrix *matrix);
void             gdk_paint_identity_matrix        (GdkPainter *cr);
void             gdk_paint_user_to_device         (GdkPainter *cr, double *x, double *y);
void             gdk_paint_user_to_device_distance (GdkPainter *cr, double *dx, double *dy);

void             gdk_paint_new_path               (GdkPainter *cr);
void             gdk_paint_new_sub_path           (GdkPainter *cr);
void             gdk_paint_close_path             (GdkPainter *cr);
void             gdk_paint_move_to                (GdkPainter *cr, double x, double y);
void             gdk_paint_line_to                (GdkPainter *cr, double x, double y);
void             gdk_paint_curve_to               (GdkPainter *cr, double x1, double y1, double x2, double y2, double x3, double y3);
void             gdk_paint_arc                    (GdkPainter *cr, double xc, double yc, double radius, double angle1, double angle2);
void             gdk_paint_arc_negative           (GdkPainter *cr, double xc, double yc, double radius, double angle1, double angle2);
void             gdk_paint_rectangle              (GdkPainter *cr, double x, double y, double width, double height);
int              gdk_paint_has_current_point      (GdkPainter *cr);
void             gdk_paint_get_current_point      (GdkPainter *cr, double *x, double *y);

GdkPaintPath *   gdk_paint_copy_path              (GdkPainter *cr);
void             gdk_paint_append_path            (GdkPainter *cr, const GdkPaintPath *path);
void             gdk_paint_path_destroy           (GdkPaintPath *path);

void             gdk_paint_fill                   (GdkPainter *cr);
void             gdk_paint_stroke                 (GdkPainter *cr);
void             gdk_paint_paint                  (GdkPainter *cr);
void             gdk_paint_paint_with_alpha       (GdkPainter *cr, double alpha);
void             gdk_paint_mask                   (GdkPainter *cr, GdkPaintPattern *pattern);
void             gdk_paint_mask_surface           (GdkPainter *cr, GdkPaintSurface *surface, double x, double y);

void             gdk_paint_clip                   (GdkPainter *cr);
void             gdk_paint_clip_extents           (GdkPainter *cr, double *x1, double *y1, double *x2, double *y2);
void             gdk_paint_reset_clip             (GdkPainter *cr);

void *           gdk_paint_get_user_data          (GdkPainter *cr, const GdkPaintUserDataKey *key);
GdkPaintStatus   gdk_paint_set_user_data          (GdkPainter *cr, const GdkPaintUserDataKey *key, void *user_data, GdkPaintDestroyFunc destroy);

/* ── surface (was cairo_surface_t) ─────────────────────────────────────── */

GdkPaintSurface *gdk_paint_image_surface_create        (GdkPaintFormat format, int width, int height);
GdkPaintSurface *gdk_paint_image_surface_create_for_data (unsigned char *data, GdkPaintFormat format, int width, int height, int stride);
unsigned char *  gdk_paint_image_surface_get_data      (GdkPaintSurface *surface);
GdkPaintFormat   gdk_paint_image_surface_get_format    (GdkPaintSurface *surface);
int              gdk_paint_image_surface_get_height    (GdkPaintSurface *surface);
int              gdk_paint_image_surface_get_stride    (GdkPaintSurface *surface);
int              gdk_paint_image_surface_get_width     (GdkPaintSurface *surface);

GdkPaintSurface *gdk_paint_surface_create_similar      (GdkPaintSurface *other, GdkPaintContent content, int width, int height);
GdkPaintSurface *gdk_paint_surface_create_similar_image (GdkPaintSurface *other, GdkPaintFormat format, int width, int height);
GdkPaintSurface *gdk_paint_surface_create_for_rectangle (GdkPaintSurface *target, double x, double y, double width, double height);
GdkPaintSurface *gdk_paint_surface_reference           (GdkPaintSurface *surface);
void             gdk_paint_surface_destroy             (GdkPaintSurface *surface);
void             gdk_paint_surface_finish              (GdkPaintSurface *surface);
void             gdk_paint_surface_flush               (GdkPaintSurface *surface);
void             gdk_paint_surface_mark_dirty          (GdkPaintSurface *surface);
GdkPaintStatus   gdk_paint_surface_status              (GdkPaintSurface *surface);
GdkPaintSurfaceType gdk_paint_surface_get_type         (GdkPaintSurface *surface);
GdkPaintContent  gdk_paint_surface_get_content         (GdkPaintSurface *surface);
void             gdk_paint_surface_set_device_scale    (GdkPaintSurface *surface, double x_scale, double y_scale);
void             gdk_paint_surface_get_device_scale    (GdkPaintSurface *surface, double *x_scale, double *y_scale);
void             gdk_paint_surface_set_device_offset   (GdkPaintSurface *surface, double x_offset, double y_offset);
void             gdk_paint_surface_get_device_offset   (GdkPaintSurface *surface, double *x_offset, double *y_offset);
GdkPaintSurface *gdk_paint_surface_map_to_image        (GdkPaintSurface *surface, const GdkRectangle *extents);
void             gdk_paint_surface_unmap_image         (GdkPaintSurface *surface, GdkPaintSurface *image);
GdkPaintStatus   gdk_paint_surface_write_to_png_stream (GdkPaintSurface *surface, GdkPaintWriteFunc write_func, void *closure);
GdkPaintSurface *gdk_paint_recording_surface_create    (GdkPaintContent content, const GdkPaintRectangle *extents);
GdkPaintSurface *gdk_paint_svg_surface_create_for_stream (GdkPaintWriteFunc write_func, void *closure, double width, double height);
void             gdk_paint_svg_surface_set_document_unit (GdkPaintSurface *surface, GdkPaintSvgUnit unit);

/* ── pattern (was cairo_pattern_t) ─────────────────────────────────────── */

GdkPaintPattern *gdk_paint_pattern_create_for_surface   (GdkPaintSurface *surface);
GdkPaintPattern *gdk_paint_pattern_create_linear        (double x0, double y0, double x1, double y1);
GdkPaintPattern *gdk_paint_pattern_create_radial        (double cx0, double cy0, double radius0, double cx1, double cy1, double radius1);
GdkPaintPattern *gdk_paint_pattern_create_mesh          (void);
GdkPaintPattern *gdk_paint_pattern_reference            (GdkPaintPattern *pattern);
void             gdk_paint_pattern_destroy              (GdkPaintPattern *pattern);
GdkPaintStatus   gdk_paint_pattern_status               (GdkPaintPattern *pattern);
void             gdk_paint_pattern_add_color_stop_rgba  (GdkPaintPattern *pattern, double offset, double red, double green, double blue, double alpha);
void             gdk_paint_pattern_set_extend           (GdkPaintPattern *pattern, GdkPaintExtend extend);
void             gdk_paint_pattern_set_filter           (GdkPaintPattern *pattern, GdkPaintFilter filter);
void             gdk_paint_pattern_set_matrix           (GdkPaintPattern *pattern, const GdkPaintMatrix *matrix);
void             gdk_paint_pattern_get_matrix           (GdkPaintPattern *pattern, GdkPaintMatrix *matrix);
GdkPaintStatus   gdk_paint_pattern_get_surface          (GdkPaintPattern *pattern, GdkPaintSurface **surface);

void             gdk_paint_mesh_pattern_begin_patch     (GdkPaintPattern *pattern);
void             gdk_paint_mesh_pattern_end_patch       (GdkPaintPattern *pattern);
void             gdk_paint_mesh_pattern_move_to         (GdkPaintPattern *pattern, double x, double y);
void             gdk_paint_mesh_pattern_line_to         (GdkPaintPattern *pattern, double x, double y);
void             gdk_paint_mesh_pattern_set_corner_color_rgba (GdkPaintPattern *pattern, unsigned int corner_num, double red, double green, double blue, double alpha);

/* ── matrix ────────────────────────────────────────────────────────────── */

void             gdk_paint_matrix_init                  (GdkPaintMatrix *matrix, double xx, double yx, double xy, double yy, double x0, double y0);
void             gdk_paint_matrix_init_identity         (GdkPaintMatrix *matrix);
void             gdk_paint_matrix_init_translate        (GdkPaintMatrix *matrix, double tx, double ty);
void             gdk_paint_matrix_init_scale            (GdkPaintMatrix *matrix, double sx, double sy);
GdkPaintStatus   gdk_paint_matrix_invert                (GdkPaintMatrix *matrix);
void             gdk_paint_matrix_translate             (GdkPaintMatrix *matrix, double tx, double ty);
void             gdk_paint_matrix_scale                 (GdkPaintMatrix *matrix, double sx, double sy);
void             gdk_paint_matrix_multiply              (GdkPaintMatrix *result, const GdkPaintMatrix *a, const GdkPaintMatrix *b);
void             gdk_paint_matrix_transform_point       (const GdkPaintMatrix *matrix, double *x, double *y);

/* ── font options ──────────────────────────────────────────────────────── */

GdkPaintFontOptions *gdk_paint_font_options_create      (void);
GdkPaintFontOptions *gdk_paint_font_options_copy        (const GdkPaintFontOptions *original);
void             gdk_paint_font_options_destroy         (GdkPaintFontOptions *options);
GdkPaintStatus   gdk_paint_font_options_status          (GdkPaintFontOptions *options);
void             gdk_paint_font_options_merge           (GdkPaintFontOptions *options, const GdkPaintFontOptions *other);
GdkPaintAntialias     gdk_paint_font_options_get_antialias      (const GdkPaintFontOptions *options);
void                  gdk_paint_font_options_set_antialias      (GdkPaintFontOptions *options, GdkPaintAntialias antialias);
GdkPaintHintMetrics   gdk_paint_font_options_get_hint_metrics   (const GdkPaintFontOptions *options);
void                  gdk_paint_font_options_set_hint_metrics   (GdkPaintFontOptions *options, GdkPaintHintMetrics hint_metrics);
GdkPaintHintStyle     gdk_paint_font_options_get_hint_style     (const GdkPaintFontOptions *options);
void                  gdk_paint_font_options_set_hint_style     (GdkPaintFontOptions *options, GdkPaintHintStyle hint_style);
GdkPaintSubpixelOrder gdk_paint_font_options_get_subpixel_order (const GdkPaintFontOptions *options);
void                  gdk_paint_font_options_set_subpixel_order (GdkPaintFontOptions *options, GdkPaintSubpixelOrder subpixel_order);

void             gdk_paint_scaled_font_get_font_options (GdkPaintScaledFont *scaled_font, GdkPaintFontOptions *options);

/* ── device ────────────────────────────────────────────────────────────── */

void             gdk_paint_device_destroy               (GdkPaintDevice *device);
GdkPaintStatus   gdk_paint_device_set_user_data         (GdkPaintDevice *device, const GdkPaintUserDataKey *key, void *user_data, GdkPaintDestroyFunc destroy);

/* ── script / recording (debug serialisation) ──────────────────────────── */

typedef GdkPaintSurface *(*GdkPaintScriptSurfaceCreateFunc) (void *closure, GdkPaintContent content, double width, double height);
typedef GdkPainter      *(*GdkPaintScriptContextCreateFunc) (void *closure);
typedef void             (*GdkPaintScriptSurfaceDestroyFunc) (void *closure, GdkPaintSurface *surface);
typedef void             (*GdkPaintScriptContextDestroyFunc) (void *closure, GdkPainter *context);
typedef void             (*GdkPaintScriptShowPageFunc) (void *closure, GdkPainter *context);
typedef void             (*GdkPaintScriptWriteCommentFunc) (void *closure, GdkPainter *context, const char *comment);

typedef struct _GdkPaintScriptInterpreterHooks
{
  void                            *closure;
  GdkPaintScriptSurfaceCreateFunc  surface_create;
  GdkPaintScriptSurfaceDestroyFunc surface_destroy;
  GdkPaintScriptContextCreateFunc  context_create;
  GdkPaintScriptContextDestroyFunc context_destroy;
  GdkPaintScriptShowPageFunc       show_page;
  GdkPaintScriptWriteCommentFunc   write_comment;
} GdkPaintScriptInterpreterHooks;

GdkPaintScriptInterpreter *gdk_paint_script_interpreter_create (void);
GdkPaintStatus   gdk_paint_script_interpreter_destroy   (GdkPaintScriptInterpreter *interpreter);
void             gdk_paint_script_interpreter_install_hooks (GdkPaintScriptInterpreter *interpreter, const GdkPaintScriptInterpreterHooks *hooks);
GdkPaintStatus   gdk_paint_script_interpreter_feed_string (GdkPaintScriptInterpreter *interpreter, const char *utf8, int len);
GdkPaintDevice * gdk_paint_script_create_for_stream     (GdkPaintWriteFunc write_func, void *closure);
GdkPaintStatus   gdk_paint_script_from_recording_surface (GdkPaintDevice *script, GdkPaintSurface *recording_surface);

/* ── feature macros (the replacements for CAIRO_HAS_*) ─────────────────── */

#define GDK_PAINT_HAS_PNG_FUNCTIONS  1
#define GDK_PAINT_HAS_SVG_SURFACE    1
#define GDK_PAINT_HAS_SCRIPT_SURFACE 1

/* ── GType accessors for the enums (was cairo-gobject) ─────────────────── */

/* The GTypes are wanted by every C consumer (they replace the
 * CAIRO_GOBJECT_TYPE_* macros); only the C++ backend opts out, so that it
 * needs no glib. */
#if !defined(__cplusplus) && !defined(GDK_PAINT_NO_TYPES)
#define GDK_PAINT_WANT_TYPES 1
#endif

#ifdef GDK_PAINT_WANT_TYPES
#include <glib-object.h>

GType gdk_paint_antialias_get_type      (void) G_GNUC_CONST;
GType gdk_paint_hint_style_get_type     (void) G_GNUC_CONST;
GType gdk_paint_hint_metrics_get_type   (void) G_GNUC_CONST;
GType gdk_paint_subpixel_order_get_type (void) G_GNUC_CONST;

#define GDK_PAINT_TYPE_ANTIALIAS      (gdk_paint_antialias_get_type ())
#define GDK_PAINT_TYPE_HINT_STYLE     (gdk_paint_hint_style_get_type ())
#define GDK_PAINT_TYPE_HINT_METRICS   (gdk_paint_hint_metrics_get_type ())
#define GDK_PAINT_TYPE_SUBPIXEL_ORDER (gdk_paint_subpixel_order_get_type ())
#endif

#ifdef __cplusplus
}
#endif

#endif /* GTK6_GDK_PAINT_H */
