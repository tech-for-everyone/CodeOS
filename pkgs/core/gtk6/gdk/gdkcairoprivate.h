#pragma once

#include "gdkcolorstateprivate.h"
#include "gdkcolorprivate.h"

#include "gdkmemoryformatprivate.h"
#include "gdkmemorytexture.h"

#include "gdkrectangleprivate.h"

#include <gdk/gdkpaint.h>
#include <graphene.h>
#include <math.h>

static inline GdkPaintFormat
gdk_painter_format_for_depth (GdkMemoryDepth depth)
{
  switch (depth)
    {
      case GDK_MEMORY_NONE:
      case GDK_MEMORY_U8:
        return GDK_PAINT_FORMAT_ARGB32;

      case GDK_MEMORY_FLOAT16:
      case GDK_MEMORY_FLOAT32:
        return GDK_PAINT_FORMAT_RGBA128F;

      case GDK_N_DEPTHS:
      default:
        g_return_val_if_reached (GDK_PAINT_FORMAT_ARGB32);
    }
}

static inline GdkMemoryFormat
gdk_painter_format_to_memory_format (GdkPaintFormat format)
{
  GdkMemoryFormat result;

  if (!gdk_memory_format_find_by_cairo_format (format, &result))
    result = GDK_MEMORY_DEFAULT;

  return result;
}

static inline GdkPaintFormat
gdk_painter_format_for_content (GdkPaintContent content)
{
  switch (content)
    {
      case GDK_PAINT_CONTENT_COLOR:
        return GDK_PAINT_FORMAT_RGB24;
      case GDK_PAINT_CONTENT_ALPHA:
        return GDK_PAINT_FORMAT_A8;
      case GDK_PAINT_CONTENT_COLOR_ALPHA:
        return GDK_PAINT_FORMAT_ARGB32;
      default:
        g_return_val_if_reached (GDK_PAINT_FORMAT_ARGB32);
    }
}

static inline void
gdk_painter_set_source_color (GdkPainter        *cr,
                            GdkColorState  *ccs,
                            const GdkColor *color)
{
  float c[4];
  gdk_color_to_float (color, ccs, c);
  gdk_paint_set_source_rgba (cr, c[0], c[1], c[2], c[3]);
}

static inline void
gdk_painter_set_source_rgba_ccs (GdkPainter       *cr,
                               GdkColorState *ccs,
                               const GdkRGBA *rgba)
{
  GdkColor c;
  gdk_color_init_from_rgba (&c, rgba);
  gdk_painter_set_source_color (cr, ccs, &c);
  gdk_color_finish (&c);
}

static inline void
gdk_painter_pattern_add_color_stop_rgba_ccs (GdkPaintPattern *pattern,
                                           GdkColorState   *ccs,
                                           double           offset,
                                           const GdkRGBA   *rgba)
{
  float color[4];

  gdk_color_state_from_rgba (ccs, rgba, color);
  gdk_paint_pattern_add_color_stop_rgba (pattern, offset, color[0], color[1], color[2], color[3]);
}

static inline void
gdk_painter_pattern_add_color_stop_color (GdkPaintPattern *pattern,
                                        GdkColorState   *ccs,
                                        double           offset,
                                        const GdkColor  *color)
{
  float values[4];

  gdk_color_to_float (color, ccs, values);
  gdk_paint_pattern_add_color_stop_rgba (pattern, offset, values[0], values[1], values[2], values[3]);
}

static inline void
gdk_painter_rect (GdkPainter               *cr,
                const graphene_rect_t *rect)
{
  gdk_paint_rectangle (cr,
                   rect->origin.x, rect->origin.y,
                   rect->size.width, rect->size.height);
}

static inline void
gdk_painter_surface_convert_color_state (GdkPaintSurface *surface,
                                       GdkColorState   *source,
                                       GdkColorState   *target)
{
  GdkPaintSurface *image_surface;
  int status, width, height;

  if (gdk_color_state_equal (source, target))
    return;

  image_surface = gdk_paint_surface_map_to_image (surface, NULL);

  status = gdk_paint_surface_status (image_surface);
  width = gdk_paint_image_surface_get_width (image_surface);
  height = gdk_paint_image_surface_get_height (image_surface);

  if (status == GDK_PAINT_STATUS_SUCCESS && width > 0 && height > 0)
    {
      gdk_memory_convert_color_state (gdk_paint_image_surface_get_data (image_surface),
                                      &GDK_MEMORY_LAYOUT_SIMPLE (
                                          gdk_painter_format_to_memory_format (gdk_paint_image_surface_get_format (image_surface)),
                                          width,
                                          height,
                                          gdk_paint_image_surface_get_stride (image_surface)),
                                      source,
                                      target);
    }

  gdk_paint_surface_mark_dirty (image_surface);
  gdk_paint_surface_unmap_image (surface, image_surface);
  /* https://gitlab.freedesktop.org/cairo/cairo/-/merge_requests/487 */
  gdk_paint_surface_mark_dirty (surface);
}

static inline gboolean
gdk_painter_is_all_clipped (GdkPainter *cr)
{
  double x1, y1, x2, y2;

  gdk_paint_clip_extents (cr, &x1, &y1, &x2, &y2);
  return x1 >= x2 || y1 >= y2;
}

/* apply a rectangle that bounds @rect in
 * pixel-aligned device coordinates.
 *
 * This is useful for clipping to minimize the rectangle
 * in push_group() or when blurring.
 */
static inline void
gdk_painter_rectangle_snap_to_grid (GdkPainter               *cr,
                                  const graphene_rect_t *rect)
{
  double x0, x1, x2, x3;
  double y0, y1, y2, y3;
  double xmin, xmax, ymin, ymax;

  x0 = rect->origin.x;
  y0 = rect->origin.y;
  gdk_paint_user_to_device (cr, &x0, &y0);
  x1 = rect->origin.x + rect->size.width;
  y1 = rect->origin.y;
  gdk_paint_user_to_device (cr, &x1, &y1);
  x2 = rect->origin.x;
  y2 = rect->origin.y + rect->size.height;
  gdk_paint_user_to_device (cr, &x2, &y2);
  x3 = rect->origin.x + rect->size.width;
  y3 = rect->origin.y + rect->size.height;
  gdk_paint_user_to_device (cr, &x3, &y3);

  xmin = MIN (MIN (x0, x1), MIN (x2, x3));
  ymin = MIN (MIN (y0, y1), MIN (y2, y3));
  xmax = MAX (MAX (x0, x1), MAX (x2, x3));
  ymax = MAX (MAX (y0, y1), MAX (y2, y3));

  xmin = floor (xmin);
  ymin = floor (ymin);
  xmax = ceil (xmax);
  ymax = ceil (ymax);

  gdk_paint_save (cr);
  gdk_paint_identity_matrix (cr);
  gdk_paint_rectangle (cr, xmin, ymin, xmax - xmin, ymax - ymin);
  gdk_paint_restore (cr);
}

/**
 * gdk_painter_create_similar_surface:
 * @cr: cairo context
 * @content: the kind of surface to create
 * @bounds: the bounds to create the surface for
 *
 * Creates a surface for offscreen rendering that isn't
 * confined by Cairo's clipping behavior. So this is
 * useful as an alternative to gdk_paint_push_group() if
 * you want a guarantee that the whole area will be
 * created, for example when blurring and needing a larger
 * input area.
 *
 * This function will set the surface's device scale and offset.
 *
 * Once done with rendering to the returned surface, you
 * can render it to the given bounds with this code:
 * 
 * ```c
 * gdk_paint_set_source_surface (cr, surface);
 * gdk_paint_paint (cr);
 * ```
 * 
 * Returns: a new cairo surface 
 **/
static inline GdkPaintSurface *
gdk_painter_create_similar_surface (GdkPainter               *cr,
                                  GdkPaintContent        content,
                                  const graphene_rect_t *bounds)
{
  GdkPaintMatrix matrix;
  double xscale, yscale, det, width, height;
  GdkPaintSurface *surface;

  gdk_paint_get_matrix (cr, &matrix);
  gdk_paint_surface_get_device_scale (gdk_paint_get_target (cr), &xscale, &yscale);
  det = matrix.xx * matrix.yy - matrix.xy * matrix.yx;
  if (matrix.xx != 0 || matrix.yx != 0)
    {
      width = sqrt (matrix.xx * matrix.xx + matrix.yx * matrix.yx);
      height = det / width;
    }
  else if (matrix.yx != 0 || matrix.yy != 0)
    {
      height = sqrt (matrix.yx * matrix.yx + matrix.yy * matrix.yy);
      width = det / height;
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  width = ABS (ceil (width * bounds->size.width * xscale));
  height = ABS (ceil (height * bounds->size.height * yscale));

  surface = gdk_paint_surface_create_similar (gdk_paint_get_group_target (cr), 
                                          content,
                                          width, height);
  xscale = width / bounds->size.width;
  yscale = height / bounds->size.height;
  gdk_paint_surface_set_device_scale (surface, xscale, yscale);
  gdk_paint_surface_set_device_offset (surface,
                                   - bounds->origin.x * xscale,
                                   - bounds->origin.y * yscale);

  return surface;
}

/*
 * gdk_painter_color_component_to_u8:
 * @v: a color component, nominally in [0, 1]
 *
 * Converts a color component to the 8 bit value cairo wants.
 *
 * Shaders divide by zero and input textures contain junk, so infinities and
 * NaN do turn up here. Infinities clamp to the ends of the range, and NaN
 * becomes 0: comparisons against NaN are all false, so it takes the same
 * branch as a negative value. Clamping before the conversion also means the
 * conversion never sees a value that a guint32 cannot represent.
 *
 * Returns: @v as a value in [0, 255]
 */
static inline guint32
gdk_painter_color_component_to_u8 (float v)
{
  float f = roundf (v * 255.f);

  return f > 0.f ? (f < 255.f ? (guint32) f : 255) : 0;
}

/*
 * gdk_painter_pixel_from_float:
 * @rgba: premultiplied color components, in the order red, green, blue, alpha
 *
 * Packs premultiplied float color components into the pixel format used by
 * cairo's ARGB32 surfaces.
 *
 * Returns: the packed pixel
 */
static inline guint32
gdk_painter_pixel_from_float (const float rgba[4])
{
  return gdk_painter_color_component_to_u8 (rgba[3]) << 24 |
         gdk_painter_color_component_to_u8 (rgba[0]) << 16 |
         gdk_painter_color_component_to_u8 (rgba[1]) << 8 |
         gdk_painter_color_component_to_u8 (rgba[2]) << 0;
}

static inline void
gdk_painter_surface_get_device_matrix (GdkPaintSurface *surface,
                                     GdkPaintMatrix  *matrix)
{
  gdk_paint_surface_get_device_scale (surface, &matrix->xx, &matrix->yy);
  gdk_paint_surface_get_device_offset (surface, &matrix->x0, &matrix->y0);
  matrix->yx = matrix->xy = 0;
}

