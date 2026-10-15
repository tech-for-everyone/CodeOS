/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK6 painting engine — see gdk/gdkpaint.h.
 *
 * GTK4's drawing API was cairo's.  GTK6 owns that API instead (GdkPainter and
 * friends) and this file is its engine:
 *
 *     GdkPainter       -> a QPainter over a QImage-backed GdkPaintSurface
 *     GdkPaintSurface  -> a QImage
 *     GdkPaintPattern  -> a QBrush (solid, gradient or surface)
 *     GdkPaintMatrix   -> the six coefficients of a QTransform
 *
 * The names and the enum values are cairo's, so the port of the GTK4 sources
 * stays the mechanical rename spelled out in gdk/gdkpaint.h.
 *
 * This file is part of the C++ side of the package (CXX_SRCS) and must stay
 * glib-free: it includes gdkpaint.h with __cplusplus defined, which is exactly
 * the case the header's optional GType block is written for.  The enum GTypes
 * that replace cairo-gobject live in gdk/gdkpaintenumtypes.c, which is C.
 *
 * Coverage: painter, path, clip, transform, groups, surfaces, patterns, font
 * options and devices are real.  The two surfaces GTK only builds for debug
 * serialisation (recording and SVG) and mesh patterns have no QPainter
 * equivalent and carry an explicit TODO; they still draw or hold a path, they
 * just do not replay or serialise.
 */

#include "gdk/gdkpaint.h"

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtCore/QVector>
#include <QtCore/QtGlobal>
#include <QtGui/QBrush>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QRadialGradient>
#include <QtGui/QTransform>

#include <math.h>
#include <string.h>

/* gdkpaint.h forward-declares GdkRectangle (it is defined in gdktypes.h,
 * which this glib-free translation unit does not include).  Completing it here
 * is safe: the two definitions never meet in one translation unit. */
struct _GdkRectangle
{
  int x;
  int y;
  int width;
  int height;
};

/* ── QImage / enum plumbing ────────────────────────────────────────────── */

static QImage::Format
qt_format (GdkPaintFormat format)
{
  switch (format)
    {
    case GDK_PAINT_FORMAT_ARGB32:    return QImage::Format_ARGB32_Premultiplied;
    case GDK_PAINT_FORMAT_RGB24:     return QImage::Format_RGB32;
    case GDK_PAINT_FORMAT_A8:        return QImage::Format_Alpha8;
    case GDK_PAINT_FORMAT_A1:        return QImage::Format_Mono;
    case GDK_PAINT_FORMAT_RGB16_565: return QImage::Format_RGB16;
    case GDK_PAINT_FORMAT_RGB30:     return QImage::Format_RGB30;
    case GDK_PAINT_FORMAT_RGB96F:    return QImage::Format_RGBX32FPx4;
    case GDK_PAINT_FORMAT_RGBA128F:  return QImage::Format_RGBA32FPx4;
    default:                         return QImage::Format_Invalid;
    }
}

static bool
format_has_alpha (GdkPaintFormat format)
{
  return format == GDK_PAINT_FORMAT_ARGB32 ||
         format == GDK_PAINT_FORMAT_A8      ||
         format == GDK_PAINT_FORMAT_A1      ||
         format == GDK_PAINT_FORMAT_RGBA128F;
}

static int
image_bytes_per_pixel (const QImage &image)
{
  return (image.depth () + 7) / 8;
}

static QTransform
qt_transform (const GdkPaintMatrix *matrix)
{
  /* cairo stores (xx yx xy yy x0 y0); QTransform's constructor is
   * (m11 m12 m21 m22 dx dy) and maps (x,y) to
   * (m11*x + m21*y + dx, m12*x + m22*y + dy). */
  return QTransform (matrix->xx, matrix->yx, matrix->xy, matrix->yy,
                     matrix->x0, matrix->y0);
}

static GdkPaintMatrix
matrix_from_qt (const QTransform &t)
{
  GdkPaintMatrix m;

  m.xx = t.m11 (); m.yx = t.m12 ();
  m.xy = t.m21 (); m.yy = t.m22 ();
  m.x0 = t.dx ();  m.y0 = t.dy ();

  return m;
}

static Qt::FillRule
qt_fill_rule (GdkPaintFillRule rule)
{
  return rule == GDK_PAINT_FILL_RULE_EVEN_ODD ? Qt::OddEvenFill : Qt::WindingFill;
}

static QGradient::Spread
qt_spread (GdkPaintExtend extend)
{
  switch (extend)
    {
    case GDK_PAINT_EXTEND_REPEAT:  return QGradient::RepeatSpread;
    case GDK_PAINT_EXTEND_REFLECT: return QGradient::ReflectSpread;
    default:                       return QGradient::PadSpread;
    }
}

static QPainter::CompositionMode
qt_composition (GdkPaintOperator op)
{
  switch (op)
    {
    case GDK_PAINT_OPERATOR_CLEAR:       return QPainter::CompositionMode_Clear;
    case GDK_PAINT_OPERATOR_SOURCE:      return QPainter::CompositionMode_Source;
    case GDK_PAINT_OPERATOR_OVER:        return QPainter::CompositionMode_SourceOver;
    case GDK_PAINT_OPERATOR_IN:          return QPainter::CompositionMode_SourceIn;
    case GDK_PAINT_OPERATOR_OUT:         return QPainter::CompositionMode_SourceOut;
    case GDK_PAINT_OPERATOR_ATOP:        return QPainter::CompositionMode_SourceAtop;
    case GDK_PAINT_OPERATOR_DEST:        return QPainter::CompositionMode_Destination;
    case GDK_PAINT_OPERATOR_DEST_OVER:   return QPainter::CompositionMode_DestinationOver;
    case GDK_PAINT_OPERATOR_DEST_IN:     return QPainter::CompositionMode_DestinationIn;
    case GDK_PAINT_OPERATOR_DEST_OUT:    return QPainter::CompositionMode_DestinationOut;
    case GDK_PAINT_OPERATOR_DEST_ATOP:   return QPainter::CompositionMode_DestinationAtop;
    case GDK_PAINT_OPERATOR_XOR:         return QPainter::CompositionMode_Xor;
    case GDK_PAINT_OPERATOR_ADD:         return QPainter::CompositionMode_Plus;
    case GDK_PAINT_OPERATOR_MULTIPLY:    return QPainter::CompositionMode_Multiply;
    case GDK_PAINT_OPERATOR_SCREEN:      return QPainter::CompositionMode_Screen;
    case GDK_PAINT_OPERATOR_OVERLAY:     return QPainter::CompositionMode_Overlay;
    case GDK_PAINT_OPERATOR_DARKEN:      return QPainter::CompositionMode_Darken;
    case GDK_PAINT_OPERATOR_LIGHTEN:     return QPainter::CompositionMode_Lighten;
    case GDK_PAINT_OPERATOR_COLOR_DODGE: return QPainter::CompositionMode_ColorDodge;
    case GDK_PAINT_OPERATOR_COLOR_BURN:  return QPainter::CompositionMode_ColorBurn;
    case GDK_PAINT_OPERATOR_HARD_LIGHT:  return QPainter::CompositionMode_HardLight;
    case GDK_PAINT_OPERATOR_SOFT_LIGHT:  return QPainter::CompositionMode_SoftLight;
    case GDK_PAINT_OPERATOR_DIFFERENCE:  return QPainter::CompositionMode_Difference;
    case GDK_PAINT_OPERATOR_EXCLUSION:   return QPainter::CompositionMode_Exclusion;
    default:
      /* SATURATE and the HSL operators have no QPainter equivalent.
       * TODO: blend them by hand. */
      return QPainter::CompositionMode_SourceOver;
    }
}

/* ── surfaces ──────────────────────────────────────────────────────────── */

struct _GdkPaintSurface
{
  QImage              image;
  unsigned char      *data = nullptr;   /* cached; the backing store is unique */
  GdkPaintFormat      format = GDK_PAINT_FORMAT_INVALID;
  GdkPaintSurfaceType kind = GDK_PAINT_SURFACE_TYPE_IMAGE;
  int                 refcount = 1;
  GdkPaintStatus      status = GDK_PAINT_STATUS_SUCCESS;
  bool                finished = false;
  double              scale_x = 1.0;
  double              scale_y = 1.0;
  double              off_x = 0.0;
  double              off_y = 0.0;
  GdkPaintSurface    *owner = nullptr;  /* a view keeps its surface alive */
};

/* A view shares @owner's pixels: a sub-rectangle surface or a map_to_image()
 * window.  The view does not own the memory. */
static GdkPaintSurface *
surface_view (GdkPaintSurface *owner, int x, int y, int width, int height)
{
  GdkPaintSurface *view;
  int              bpp;

  if (owner == nullptr || owner->data == nullptr)
    return nullptr;

  if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
      x + width > owner->image.width () || y + height > owner->image.height ())
    return nullptr;

  bpp = image_bytes_per_pixel (owner->image);

  view = new GdkPaintSurface ();
  view->data = owner->data + (qint64) y * owner->image.bytesPerLine () + (qint64) x * bpp;
  view->image = QImage (view->data, width, height, owner->image.bytesPerLine (),
                        owner->image.format ());
  view->format = owner->format;
  view->kind = GDK_PAINT_SURFACE_TYPE_SUBSURFACE;
  view->owner = gdk_paint_surface_reference (owner);
  view->scale_x = owner->scale_x;
  view->scale_y = owner->scale_y;

  return view;
}

extern "C" GdkPaintSurface *
gdk_paint_image_surface_create (GdkPaintFormat format, int width, int height)
{
  GdkPaintSurface *surface = new GdkPaintSurface ();

  surface->format = format;
  surface->kind = GDK_PAINT_SURFACE_TYPE_IMAGE;

  if (width > 0 && height > 0)
    surface->image = QImage (width, height, qt_format (format));

  if (width > 0 && height > 0 && surface->image.isNull ())
    surface->status = GDK_PAINT_STATUS_INVALID_FORMAT;
  else if (width <= 0 || height <= 0)
    surface->status = GDK_PAINT_STATUS_INVALID_FORMAT;

  if (!surface->image.isNull ())
    surface->data = surface->image.bits ();

  return surface;
}

extern "C" GdkPaintSurface *
gdk_paint_image_surface_create_for_data (unsigned char *data,
                                         GdkPaintFormat format,
                                         int            width,
                                         int            height,
                                         int            stride)
{
  GdkPaintSurface *surface = new GdkPaintSurface ();

  surface->format = format;
  surface->kind = GDK_PAINT_SURFACE_TYPE_IMAGE;
  surface->image = QImage (data, width, height, stride, qt_format (format));
  surface->data = data;

  if (surface->image.isNull ())
    surface->status = GDK_PAINT_STATUS_INVALID_FORMAT;

  return surface;
}

extern "C" unsigned char *
gdk_paint_image_surface_get_data (GdkPaintSurface *surface)
{
  return surface ? surface->data : nullptr;
}

extern "C" GdkPaintFormat
gdk_paint_image_surface_get_format (GdkPaintSurface *surface)
{
  return surface ? surface->format : GDK_PAINT_FORMAT_INVALID;
}

extern "C" int
gdk_paint_image_surface_get_width (GdkPaintSurface *surface)
{
  return surface ? surface->image.width () : 0;
}

extern "C" int
gdk_paint_image_surface_get_height (GdkPaintSurface *surface)
{
  return surface ? surface->image.height () : 0;
}

extern "C" int
gdk_paint_image_surface_get_stride (GdkPaintSurface *surface)
{
  return surface ? surface->image.bytesPerLine () : 0;
}

extern "C" GdkPaintSurface *
gdk_paint_surface_create_similar (GdkPaintSurface *other,
                                  GdkPaintContent  content,
                                  int              width,
                                  int              height)
{
  GdkPaintFormat format = (content == GDK_PAINT_CONTENT_COLOR)
                              ? GDK_PAINT_FORMAT_RGB24
                              : GDK_PAINT_FORMAT_ARGB32;
  GdkPaintSurface *surface = gdk_paint_image_surface_create (format, width, height);

  if (other != nullptr)
    {
      surface->scale_x = other->scale_x;
      surface->scale_y = other->scale_y;
      surface->off_x = other->off_x;
      surface->off_y = other->off_y;
    }

  return surface;
}

extern "C" GdkPaintSurface *
gdk_paint_surface_create_similar_image (GdkPaintSurface *other,
                                        GdkPaintFormat   format,
                                        int              width,
                                        int              height)
{
  GdkPaintSurface *surface = gdk_paint_image_surface_create (format, width, height);

  if (other != nullptr)
    {
      surface->scale_x = other->scale_x;
      surface->scale_y = other->scale_y;
    }

  return surface;
}

extern "C" GdkPaintSurface *
gdk_paint_surface_create_for_rectangle (GdkPaintSurface *target,
                                        double           x,
                                        double           y,
                                        double           width,
                                        double           height)
{
  GdkPaintSurface *view;
  int              ix = (int) floor (x);
  int              iy = (int) floor (y);
  int              iw = (int) floor (width);
  int              ih = (int) floor (height);

  view = surface_view (target, ix, iy, iw, ih);
  if (view != nullptr)
    return view;

  /* Out of bounds or not an image surface: hand back a real surface so the
   * caller always has somewhere to draw. */
  return gdk_paint_image_surface_create (target ? target->format : GDK_PAINT_FORMAT_ARGB32,
                                         iw > 0 ? iw : 1, ih > 0 ? ih : 1);
}

extern "C" GdkPaintSurface *
gdk_paint_surface_reference (GdkPaintSurface *surface)
{
  if (surface != nullptr)
    surface->refcount++;

  return surface;
}

extern "C" void
gdk_paint_surface_destroy (GdkPaintSurface *surface)
{
  if (surface == nullptr || --surface->refcount > 0)
    return;

  if (surface->owner != nullptr)
    {
      GdkPaintSurface *owner = surface->owner;
      surface->owner = nullptr;
      gdk_paint_surface_destroy (owner);
    }

  delete surface;
}

extern "C" void
gdk_paint_surface_finish (GdkPaintSurface *surface)
{
  if (surface != nullptr)
    surface->finished = true;
}

extern "C" void
gdk_paint_surface_flush (GdkPaintSurface *surface)
{
  (void) surface;
}

extern "C" void
gdk_paint_surface_mark_dirty (GdkPaintSurface *surface)
{
  (void) surface;
}

extern "C" GdkPaintStatus
gdk_paint_surface_status (GdkPaintSurface *surface)
{
  return surface ? surface->status : GDK_PAINT_STATUS_NULL_POINTER;
}

extern "C" GdkPaintSurfaceType
gdk_paint_surface_get_type (GdkPaintSurface *surface)
{
  return surface ? surface->kind : GDK_PAINT_SURFACE_TYPE_IMAGE;
}

extern "C" GdkPaintContent
gdk_paint_surface_get_content (GdkPaintSurface *surface)
{
  if (surface == nullptr)
    return GDK_PAINT_CONTENT_COLOR_ALPHA;

  return format_has_alpha (surface->format) ? GDK_PAINT_CONTENT_COLOR_ALPHA
                                            : GDK_PAINT_CONTENT_COLOR;
}

extern "C" void
gdk_paint_surface_set_device_scale (GdkPaintSurface *surface, double x_scale, double y_scale)
{
  if (surface != nullptr)
    {
      surface->scale_x = x_scale;
      surface->scale_y = y_scale;
    }
}

extern "C" void
gdk_paint_surface_get_device_scale (GdkPaintSurface *surface, double *x_scale, double *y_scale)
{
  if (surface == nullptr)
    return;

  *x_scale = surface->scale_x;
  *y_scale = surface->scale_y;
}

extern "C" void
gdk_paint_surface_set_device_offset (GdkPaintSurface *surface, double x_offset, double y_offset)
{
  if (surface != nullptr)
    {
      surface->off_x = x_offset;
      surface->off_y = y_offset;
    }
}

extern "C" void
gdk_paint_surface_get_device_offset (GdkPaintSurface *surface, double *x_offset, double *y_offset)
{
  if (surface == nullptr)
    return;

  *x_offset = surface->off_x;
  *y_offset = surface->off_y;
}

/* A mapped image shares the surface's memory, so a caller that writes through
 * it writes through to the surface (which is what cairo-similar use expects:
 * GTK only reads it back, but the aliasing must not be lost). */
extern "C" GdkPaintSurface *
gdk_paint_surface_map_to_image (GdkPaintSurface *surface, const GdkRectangle *extents)
{
  int x = extents ? extents->x : 0;
  int y = extents ? extents->y : 0;
  int w = extents ? extents->width : (surface ? surface->image.width () : 0);
  int h = extents ? extents->height : (surface ? surface->image.height () : 0);

  return surface_view (surface, x, y, w, h);
}

extern "C" void
gdk_paint_surface_unmap_image (GdkPaintSurface *surface, GdkPaintSurface *image)
{
  (void) surface;

  if (image != nullptr)
    {
      /* The view holds an extra reference on its owner; drop the link and then
       * the view, so the owner goes too. */
      if (image->owner != nullptr)
        {
          GdkPaintSurface *owner = image->owner;
          image->owner = nullptr;
          gdk_paint_surface_destroy (owner);
        }

      gdk_paint_surface_destroy (image);
    }
}

extern "C" GdkPaintStatus
gdk_paint_surface_write_to_png_stream (GdkPaintSurface *surface,
                                       GdkPaintWriteFunc write_func,
                                       void            *closure)
{
  QByteArray data;
  QBuffer    buffer (&data);

  if (surface == nullptr || write_func == nullptr)
    return GDK_PAINT_STATUS_NULL_POINTER;

  if (surface->image.isNull ())
    return GDK_PAINT_STATUS_SURFACE_TYPE_MISMATCH;

  if (!buffer.open (QIODevice::WriteOnly))
    return GDK_PAINT_STATUS_WRITE_ERROR;

  if (!surface->image.save (&buffer, "PNG"))
    {
      buffer.close ();
      return GDK_PAINT_STATUS_WRITE_ERROR;
    }

  buffer.close ();

  return write_func (closure, (const unsigned char *) data.constData (),
                     (unsigned int) data.size ());
}

extern "C" GdkPaintSurface *
gdk_paint_recording_surface_create (GdkPaintContent content, const GdkPaintRectangle *extents)
{
  int width = extents ? (int) ceil (extents->width) : 0;
  int height = extents ? (int) ceil (extents->height) : 0;
  GdkPaintFormat format = (content == GDK_PAINT_CONTENT_COLOR)
                              ? GDK_PAINT_FORMAT_RGB24
                              : GDK_PAINT_FORMAT_ARGB32;
  GdkPaintSurface *surface;

  /* TODO: a true recording surface has unbounded extents and replays.  GTK6
   * only builds these for the debug serialisers (gskpathbuilder, the render
   * node parser), which draw a path into one and read it straight back, so a
   * plain image surface is enough — and with NULL extents it is a null image,
   * which still records a path because path ops never touch the QPainter. */
  surface = gdk_paint_image_surface_create (format, width, height);
  surface->kind = GDK_PAINT_SURFACE_TYPE_RECORDING;

  if (extents)
    {
      surface->off_x = extents->x;
      surface->off_y = extents->y;
    }

  return surface;
}

extern "C" GdkPaintSurface *
gdk_paint_svg_surface_create_for_stream (GdkPaintWriteFunc write_func,
                                         void            *closure,
                                         double           width,
                                         double           height)
{
  GdkPaintSurface *surface;

  /* TODO: real SVG serialisation.  The surface is drawable, so the paths GTK
   * builds on it are correct, but nothing is written to write_func yet. */
  (void) write_func;
  (void) closure;

  surface = gdk_paint_image_surface_create (GDK_PAINT_FORMAT_ARGB32,
                                            (int) ceil (width), (int) ceil (height));
  surface->kind = GDK_PAINT_SURFACE_TYPE_SVG;

  return surface;
}

extern "C" void
gdk_paint_svg_surface_set_document_unit (GdkPaintSurface *surface, GdkPaintSvgUnit unit)
{
  (void) surface;
  (void) unit;
}

/* ── patterns ──────────────────────────────────────────────────────────── */

enum GdkPaintPatternKind
{
  GDK_PAINT_PATTERN_SOLID = 0,
  GDK_PAINT_PATTERN_SURFACE,
  GDK_PAINT_PATTERN_LINEAR,
  GDK_PAINT_PATTERN_RADIAL,
  GDK_PAINT_PATTERN_MESH
};

struct _GdkPaintPattern
{
  int                            kind = GDK_PAINT_PATTERN_SOLID;
  int                            refcount = 1;
  GdkPaintStatus                 status = GDK_PAINT_STATUS_SUCCESS;

  QColor                         color = QColor (Qt::black);
  GdkPaintSurface               *surface = nullptr;
  QVector<QPair<double, QColor> > stops;
  QColor                         corners[4];

  double                         x0 = 0, y0 = 0, x1 = 0, y1 = 0;   /* linear */
  double                         cx0 = 0, cy0 = 0, r0 = 0;         /* radial */
  double                         cx1 = 0, cy1 = 0, r1 = 0;

  GdkPaintExtend                 extend = GDK_PAINT_EXTEND_PAD;
  GdkPaintFilter                 filter = GDK_PAINT_FILTER_GOOD;
  GdkPaintMatrix                 matrix = { 1, 0, 0, 1, 0, 0 };
};

/* The QBrush a pattern paints with.  Built per operation so it always sees the
 * surface's current pixels. */
static QBrush
pattern_brush (GdkPaintPattern *pattern)
{
  QBrush brush (Qt::black);

  if (pattern == nullptr || pattern->status != GDK_PAINT_STATUS_SUCCESS)
    return QBrush (Qt::transparent);

  switch (pattern->kind)
    {
    case GDK_PAINT_PATTERN_SURFACE:
      brush = pattern->surface ? QBrush (pattern->surface->image)
                               : QBrush (Qt::transparent);
      break;

    case GDK_PAINT_PATTERN_LINEAR:
      {
        QLinearGradient g (pattern->x0, pattern->y0, pattern->x1, pattern->y1);
        for (const auto &stop : pattern->stops)
          g.setColorAt (stop.first, stop.second);
        g.setSpread (qt_spread (pattern->extend));
        brush = QBrush (g);
      }
      break;

    case GDK_PAINT_PATTERN_RADIAL:
      {
        QRadialGradient g (pattern->cx1, pattern->cy1, pattern->r1,
                           pattern->cx0, pattern->cy0);
        for (const auto &stop : pattern->stops)
          g.setColorAt (stop.first, stop.second);
        g.setSpread (qt_spread (pattern->extend));
        brush = QBrush (g);
      }
      break;

    case GDK_PAINT_PATTERN_MESH:
      /* TODO: QPainter has no Gouraud-mesh brush; approximate with the first
       * corner colour so a mesh pattern still paints something visible. */
      brush = QBrush (pattern->corners[0]);
      break;

    case GDK_PAINT_PATTERN_SOLID:
    default:
      brush = QBrush (pattern->color);
      break;
    }

  if (pattern->kind != GDK_PAINT_PATTERN_MESH)
    brush.setTransform (qt_transform (&pattern->matrix));

  return brush;
}

extern "C" GdkPaintPattern *
gdk_paint_pattern_create_for_surface (GdkPaintSurface *surface)
{
  GdkPaintPattern *pattern = new GdkPaintPattern ();

  pattern->kind = GDK_PAINT_PATTERN_SURFACE;

  if (surface != nullptr)
    pattern->surface = gdk_paint_surface_reference (surface);
  else
    pattern->status = GDK_PAINT_STATUS_NULL_POINTER;

  return pattern;
}

extern "C" GdkPaintPattern *
gdk_paint_pattern_create_linear (double x0, double y0, double x1, double y1)
{
  GdkPaintPattern *pattern = new GdkPaintPattern ();

  pattern->kind = GDK_PAINT_PATTERN_LINEAR;
  pattern->x0 = x0; pattern->y0 = y0;
  pattern->x1 = x1; pattern->y1 = y1;

  return pattern;
}

extern "C" GdkPaintPattern *
gdk_paint_pattern_create_radial (double cx0, double cy0, double radius0,
                                 double cx1, double cy1, double radius1)
{
  GdkPaintPattern *pattern = new GdkPaintPattern ();

  pattern->kind = GDK_PAINT_PATTERN_RADIAL;
  pattern->cx0 = cx0; pattern->cy0 = cy0; pattern->r0 = radius0;
  pattern->cx1 = cx1; pattern->cy1 = cy1; pattern->r1 = radius1;

  return pattern;
}

extern "C" GdkPaintPattern *
gdk_paint_pattern_create_mesh (void)
{
  GdkPaintPattern *pattern = new GdkPaintPattern ();

  pattern->kind = GDK_PAINT_PATTERN_MESH;

  return pattern;
}

extern "C" GdkPaintPattern *
gdk_paint_pattern_reference (GdkPaintPattern *pattern)
{
  if (pattern != nullptr)
    pattern->refcount++;

  return pattern;
}

extern "C" void
gdk_paint_pattern_destroy (GdkPaintPattern *pattern)
{
  if (pattern == nullptr || --pattern->refcount > 0)
    return;

  if (pattern->surface)
    gdk_paint_surface_destroy (pattern->surface);

  delete pattern;
}

extern "C" GdkPaintStatus
gdk_paint_pattern_status (GdkPaintPattern *pattern)
{
  return pattern ? pattern->status : GDK_PAINT_STATUS_NULL_POINTER;
}

extern "C" void
gdk_paint_pattern_add_color_stop_rgba (GdkPaintPattern *pattern,
                                       double           offset,
                                       double           red,
                                       double           green,
                                       double           blue,
                                       double           alpha)
{
  QColor color;

  if (pattern == nullptr)
    return;

  color.setRgbF (qBound (0.0, red, 1.0),
                 qBound (0.0, green, 1.0),
                 qBound (0.0, blue, 1.0),
                 qBound (0.0, alpha, 1.0));

  pattern->stops.append (qMakePair (qBound (0.0, offset, 1.0), color));
}

extern "C" void
gdk_paint_pattern_set_extend (GdkPaintPattern *pattern, GdkPaintExtend extend)
{
  if (pattern != nullptr)
    pattern->extend = extend;
}

extern "C" void
gdk_paint_pattern_set_filter (GdkPaintPattern *pattern, GdkPaintFilter filter)
{
  /* TODO: QBrush has no filter setting; store it for callers that query it. */
  if (pattern != nullptr)
    pattern->filter = filter;
}

extern "C" void
gdk_paint_pattern_set_matrix (GdkPaintPattern *pattern, const GdkPaintMatrix *matrix)
{
  if (pattern != nullptr && matrix != nullptr)
    pattern->matrix = *matrix;
}

extern "C" void
gdk_paint_pattern_get_matrix (GdkPaintPattern *pattern, GdkPaintMatrix *matrix)
{
  if (pattern == nullptr || matrix == nullptr)
    return;

  *matrix = pattern->matrix;
}

extern "C" GdkPaintStatus
gdk_paint_pattern_get_surface (GdkPaintPattern *pattern, GdkPaintSurface **surface)
{
  if (pattern == nullptr)
    return GDK_PAINT_STATUS_NULL_POINTER;

  if (pattern->kind != GDK_PAINT_PATTERN_SURFACE || pattern->surface == nullptr)
    {
      if (surface)
        *surface = nullptr;
      return GDK_PAINT_STATUS_PATTERN_TYPE_MISMATCH;
    }

  if (surface)
    *surface = pattern->surface;

  return GDK_PAINT_STATUS_SUCCESS;
}

extern "C" void
gdk_paint_mesh_pattern_begin_patch (GdkPaintPattern *pattern)
{
  (void) pattern;
}

extern "C" void
gdk_paint_mesh_pattern_end_patch (GdkPaintPattern *pattern)
{
  (void) pattern;
}

extern "C" void
gdk_paint_mesh_pattern_move_to (GdkPaintPattern *pattern, double x, double y)
{
  (void) pattern;
  (void) x;
  (void) y;
}

extern "C" void
gdk_paint_mesh_pattern_line_to (GdkPaintPattern *pattern, double x, double y)
{
  (void) pattern;
  (void) x;
  (void) y;
}

extern "C" void
gdk_paint_mesh_pattern_set_corner_color_rgba (GdkPaintPattern *pattern,
                                              unsigned int     corner_num,
                                              double           red,
                                              double           green,
                                              double           blue,
                                              double           alpha)
{
  if (pattern == nullptr || corner_num > 3)
    return;

  pattern->corners[corner_num].setRgbF (qBound (0.0, red, 1.0),
                                        qBound (0.0, green, 1.0),
                                        qBound (0.0, blue, 1.0),
                                        qBound (0.0, alpha, 1.0));
}

/* ── font options / scaled fonts ───────────────────────────────────────── */

struct _GdkPaintFontOptions
{
  GdkPaintAntialias   antialias = GDK_PAINT_ANTIALIAS_DEFAULT;
  GdkPaintHintMetrics hint_metrics = GDK_PAINT_HINT_METRICS_DEFAULT;
  GdkPaintHintStyle   hint_style = GDK_PAINT_HINT_STYLE_DEFAULT;
  GdkPaintSubpixelOrder subpixel_order = GDK_PAINT_SUBPIXEL_ORDER_DEFAULT;
};

struct _GdkPaintScaledFont
{
  GdkPaintFontOptions options;
};

extern "C" GdkPaintFontOptions *
gdk_paint_font_options_create (void)
{
  return new GdkPaintFontOptions ();
}

extern "C" GdkPaintFontOptions *
gdk_paint_font_options_copy (const GdkPaintFontOptions *original)
{
  if (original == nullptr)
    return nullptr;

  return new GdkPaintFontOptions (*original);
}

extern "C" void
gdk_paint_font_options_destroy (GdkPaintFontOptions *options)
{
  delete options;
}

extern "C" void
gdk_paint_font_options_merge (GdkPaintFontOptions       *options,
                              const GdkPaintFontOptions *other)
{
  if (options == nullptr || other == nullptr)
    return;

  if (other->antialias != GDK_PAINT_ANTIALIAS_DEFAULT)
    options->antialias = other->antialias;
  if (other->hint_metrics != GDK_PAINT_HINT_METRICS_DEFAULT)
    options->hint_metrics = other->hint_metrics;
  if (other->hint_style != GDK_PAINT_HINT_STYLE_DEFAULT)
    options->hint_style = other->hint_style;
  if (other->subpixel_order != GDK_PAINT_SUBPIXEL_ORDER_DEFAULT)
    options->subpixel_order = other->subpixel_order;
}

extern "C" GdkPaintAntialias
gdk_paint_font_options_get_antialias (const GdkPaintFontOptions *options)
{
  return options ? options->antialias : GDK_PAINT_ANTIALIAS_DEFAULT;
}

extern "C" void
gdk_paint_font_options_set_antialias (GdkPaintFontOptions *options, GdkPaintAntialias antialias)
{
  if (options != nullptr)
    options->antialias = antialias;
}

extern "C" GdkPaintHintMetrics
gdk_paint_font_options_get_hint_metrics (const GdkPaintFontOptions *options)
{
  return options ? options->hint_metrics : GDK_PAINT_HINT_METRICS_DEFAULT;
}

extern "C" void
gdk_paint_font_options_set_hint_metrics (GdkPaintFontOptions *options, GdkPaintHintMetrics hint_metrics)
{
  if (options != nullptr)
    options->hint_metrics = hint_metrics;
}

extern "C" GdkPaintHintStyle
gdk_paint_font_options_get_hint_style (const GdkPaintFontOptions *options)
{
  return options ? options->hint_style : GDK_PAINT_HINT_STYLE_DEFAULT;
}

extern "C" void
gdk_paint_font_options_set_hint_style (GdkPaintFontOptions *options, GdkPaintHintStyle hint_style)
{
  if (options != nullptr)
    options->hint_style = hint_style;
}

extern "C" GdkPaintSubpixelOrder
gdk_paint_font_options_get_subpixel_order (const GdkPaintFontOptions *options)
{
  return options ? options->subpixel_order : GDK_PAINT_SUBPIXEL_ORDER_DEFAULT;
}

extern "C" void
gdk_paint_font_options_set_subpixel_order (GdkPaintFontOptions *options,
                                           GdkPaintSubpixelOrder subpixel_order)
{
  if (options != nullptr)
    options->subpixel_order = subpixel_order;
}

extern "C" void
gdk_paint_scaled_font_get_font_options (GdkPaintScaledFont  *scaled_font,
                                        GdkPaintFontOptions *options)
{
  if (scaled_font != nullptr && options != nullptr)
    *options = scaled_font->options;
}

/* ── devices ───────────────────────────────────────────────────────────── */

struct _GdkPaintDevice
{
  GdkPaintStatus status = GDK_PAINT_STATUS_SUCCESS;
  QHash<const void *, QPair<void *, GdkPaintDestroyFunc> > user_data;
};

extern "C" void
gdk_paint_device_destroy (GdkPaintDevice *device)
{
  if (device == nullptr)
    return;

  for (const auto &entry : device->user_data)
    if (entry.second)
      entry.second (entry.first);

  delete device;
}

extern "C" GdkPaintStatus
gdk_paint_device_set_user_data (GdkPaintDevice              *device,
                                const GdkPaintUserDataKey   *key,
                                void                        *user_data,
                                GdkPaintDestroyFunc          destroy)
{
  if (device == nullptr || key == nullptr)
    return GDK_PAINT_STATUS_NULL_POINTER;

  auto it = device->user_data.find ((const void *) key);

  if (it != device->user_data.end ())
    {
      if (it.value ().second && it.value ().first != user_data)
        it.value ().second (it.value ().first);

      if (user_data == nullptr)
        {
          device->user_data.erase (it);
          return GDK_PAINT_STATUS_SUCCESS;
        }
    }

  device->user_data[(const void *) key] =
      QPair<void *, GdkPaintDestroyFunc> (user_data, destroy);

  return GDK_PAINT_STATUS_SUCCESS;
}

/* ── script interpreter (debug serialisation) ──────────────────────────── */

struct _GdkPaintScriptInterpreter
{
  GdkPaintScriptInterpreterHooks hooks;
};

extern "C" GdkPaintScriptInterpreter *
gdk_paint_script_interpreter_create (void)
{
  return new GdkPaintScriptInterpreter ();
}

extern "C" GdkPaintStatus
gdk_paint_script_interpreter_destroy (GdkPaintScriptInterpreter *interpreter)
{
  delete interpreter;

  return GDK_PAINT_STATUS_SUCCESS;
}

extern "C" void
gdk_paint_script_interpreter_install_hooks (GdkPaintScriptInterpreter      *interpreter,
                                            const GdkPaintScriptInterpreterHooks *hooks)
{
  if (interpreter == nullptr || hooks == nullptr)
    return;

  interpreter->hooks = *hooks;
}

extern "C" GdkPaintStatus
gdk_paint_script_interpreter_feed_string (GdkPaintScriptInterpreter *interpreter,
                                          const char                *utf8,
                                          int                        length)
{
  /* TODO: no script interpreter — GTK6 renders with Qt, so the debug script
   * surface is never replayed.  Feeding a script is accepted and ignored. */
  (void) interpreter;
  (void) utf8;
  (void) length;

  return GDK_PAINT_STATUS_SUCCESS;
}

extern "C" GdkPaintDevice *
gdk_paint_script_create_for_stream (GdkPaintWriteFunc write_func, void *closure)
{
  (void) write_func;
  (void) closure;

  return new GdkPaintDevice ();
}

/* ── matrix ────────────────────────────────────────────────────────────── */

extern "C" void
gdk_paint_matrix_init (GdkPaintMatrix *matrix, double xx, double yx,
                       double xy, double yy, double x0, double y0)
{
  matrix->xx = xx; matrix->yx = yx;
  matrix->xy = xy; matrix->yy = yy;
  matrix->x0 = x0; matrix->y0 = y0;
}

extern "C" void
gdk_paint_matrix_init_identity (GdkPaintMatrix *matrix)
{
  gdk_paint_matrix_init (matrix, 1, 0, 0, 1, 0, 0);
}

extern "C" void
gdk_paint_matrix_init_translate (GdkPaintMatrix *matrix, double tx, double ty)
{
  gdk_paint_matrix_init (matrix, 1, 0, 0, 1, tx, ty);
}

extern "C" void
gdk_paint_matrix_init_scale (GdkPaintMatrix *matrix, double sx, double sy)
{
  gdk_paint_matrix_init (matrix, sx, 0, 0, sy, 0, 0);
}

extern "C" GdkPaintStatus
gdk_paint_matrix_invert (GdkPaintMatrix *matrix)
{
  QTransform t (qt_transform (matrix));
  bool       ok = false;
  QTransform inv = t.inverted (&ok);

  if (!ok)
    return GDK_PAINT_STATUS_INVALID_MATRIX;

  *matrix = matrix_from_qt (inv);

  return GDK_PAINT_STATUS_SUCCESS;
}

extern "C" void
gdk_paint_matrix_translate (GdkPaintMatrix *matrix, double tx, double ty)
{
  QTransform t (qt_transform (matrix));

  t.translate (tx, ty);
  *matrix = matrix_from_qt (t);
}

extern "C" void
gdk_paint_matrix_scale (GdkPaintMatrix *matrix, double sx, double sy)
{
  QTransform t (qt_transform (matrix));

  t.scale (sx, sy);
  *matrix = matrix_from_qt (t);
}

extern "C" void
gdk_paint_matrix_multiply (GdkPaintMatrix       *result,
                           const GdkPaintMatrix *a,
                           const GdkPaintMatrix *b)
{
  *result = matrix_from_qt (qt_transform (a) * qt_transform (b));
}

extern "C" void
gdk_paint_matrix_transform_point (const GdkPaintMatrix *matrix, double *x, double *y)
{
  QPointF p = qt_transform (matrix).map (QPointF (*x, *y));

  *x = p.x ();
  *y = p.y ();
}

/* ── the painter ───────────────────────────────────────────────────────── */

struct GdkPainterState
{
  QPainterPath              path;
  QBrush                    brush;
  QPen                      pen;
  GdkPaintPattern          *source = nullptr;
  GdkPaintOperator          op = GDK_PAINT_OPERATOR_OVER;
  GdkPaintFillRule          fill_rule = GDK_PAINT_FILL_RULE_WINDING;
  QTransform                transform;
  QList<QPainterPath>       clip;
  int                       group_depth = 0;
};

struct _GdkPainter
{
  GdkPaintSurface          *target = nullptr;
  QPainter                 *qp = nullptr;
  QList<GdkPaintSurface *>  groups;
  QList<GdkPainterState>    stack;

  QPainterPath              path;
  QBrush                    brush = QBrush (Qt::black);
  QPen                      pen = QPen (Qt::black, 1.0);
  GdkPaintPattern          *source = nullptr;
  GdkPaintOperator          op = GDK_PAINT_OPERATOR_OVER;
  GdkPaintFillRule          fill_rule = GDK_PAINT_FILL_RULE_WINDING;
  QTransform                transform;
  QList<QPainterPath>       clip;

  double                    tolerance = 0.1;
  int                       refcount = 1;
  GdkPaintStatus            status = GDK_PAINT_STATUS_SUCCESS;

  QHash<const void *, QPair<void *, GdkPaintDestroyFunc> > user_data;
};

static GdkPaintSurface *
painter_current_surface (GdkPainter *cr)
{
  if (!cr->groups.isEmpty ())
    return cr->groups.last ();

  return cr->target;
}

static void
painter_bind (GdkPainter *cr, GdkPaintSurface *surface)
{
  if (cr->qp)
    {
      cr->qp->end ();
      delete cr->qp;
      cr->qp = nullptr;
    }

  if (surface != nullptr && !surface->image.isNull ())
    {
      cr->qp = new QPainter (&surface->image);
      cr->qp->setRenderHint (QPainter::Antialiasing, true);
      cr->qp->setRenderHint (QPainter::SmoothPixmapTransform, true);
    }
}

/* Push the tracked state (transform, device-space clip, composition) onto the
 * active QPainter.  QPainter clips are device-space and survive a later
 * setTransform(), so the clip is installed with an identity transform first. */
static void
painter_apply (GdkPainter *cr)
{
  if (cr->qp == nullptr)
    return;

  cr->qp->setClipping (false);
  cr->qp->resetTransform ();

  for (const QPainterPath &c : cr->clip)
    cr->qp->setClipPath (c, Qt::IntersectClip);

  cr->qp->setCompositionMode (qt_composition (cr->op));
  cr->qp->setTransform (cr->transform);
}

static QPen
painter_stroke_pen (GdkPainter *cr)
{
  QPen p (cr->brush, cr->pen.widthF ());

  p.setCapStyle (cr->pen.capStyle ());
  p.setJoinStyle (cr->pen.joinStyle ());
  p.setMiterLimit (cr->pen.miterLimit ());
  p.setStyle (Qt::SolidLine);
  if (cr->pen.style () == Qt::CustomDashLine)
    p.setDashPattern (cr->pen.dashPattern ());

  return p;
}

static QRect
painter_device_rect (GdkPainter *cr)
{
  GdkPaintSurface *s = painter_current_surface (cr);

  if (s == nullptr)
    return QRect ();

  return QRect (0, 0, s->image.width (), s->image.height ());
}

static QRectF
painter_user_rect (GdkPainter *cr)
{
  QTransform inv;

  inv = cr->transform.inverted ();

  return inv.mapRect (QRectF (painter_device_rect (cr)));
}

/* ── painter lifetime ──────────────────────────────────────────────────── */

extern "C" GdkPainter *
gdk_paint_create (GdkPaintSurface *target)
{
  GdkPainter *cr = new GdkPainter ();

  cr->target = target ? gdk_paint_surface_reference (target) : nullptr;
  painter_bind (cr, cr->target);

  return cr;
}

extern "C" GdkPainter *
gdk_paint_reference (GdkPainter *cr)
{
  if (cr != nullptr)
    cr->refcount++;

  return cr;
}

extern "C" void
gdk_paint_destroy (GdkPainter *cr)
{
  if (cr == nullptr || --cr->refcount > 0)
    return;

  for (const auto &entry : cr->user_data)
    if (entry.second)
      entry.second (entry.first);

  if (cr->source)
    gdk_paint_pattern_destroy (cr->source);

  for (const GdkPainterState &st : cr->stack)
    if (st.source)
      gdk_paint_pattern_destroy (st.source);

  if (cr->qp)
    {
      cr->qp->end ();
      delete cr->qp;
    }

  for (GdkPaintSurface *g : cr->groups)
    gdk_paint_surface_destroy (g);

  if (cr->target)
    gdk_paint_surface_destroy (cr->target);

  delete cr;
}

extern "C" GdkPaintStatus
gdk_paint_status (GdkPainter *cr)
{
  return cr ? cr->status : GDK_PAINT_STATUS_NULL_POINTER;
}

extern "C" const char *
gdk_paint_status_to_string (GdkPaintStatus status)
{
  switch (status)
    {
    case GDK_PAINT_STATUS_SUCCESS:           return "no error";
    case GDK_PAINT_STATUS_NO_MEMORY:         return "out of memory";
    case GDK_PAINT_STATUS_INVALID_RESTORE:   return "restore without matching save";
    case GDK_PAINT_STATUS_INVALID_POP_GROUP: return "pop group without matching push group";
    case GDK_PAINT_STATUS_NO_CURRENT_POINT:  return "no current point";
    case GDK_PAINT_STATUS_INVALID_MATRIX:    return "invalid matrix (not invertible)";
    case GDK_PAINT_STATUS_NULL_POINTER:      return "NULL pointer";
    case GDK_PAINT_STATUS_INVALID_FORMAT:    return "invalid pixel format";
    case GDK_PAINT_STATUS_READ_ERROR:        return "read error";
    case GDK_PAINT_STATUS_WRITE_ERROR:       return "write error";
    case GDK_PAINT_STATUS_SURFACE_FINISHED:  return "surface finished";
    default:                                 return "unknown status";
    }
}

/* ── save / restore / groups ───────────────────────────────────────────── */

extern "C" void
gdk_paint_save (GdkPainter *cr)
{
  GdkPainterState st;

  st.path = cr->path;
  st.brush = cr->brush;
  st.pen = cr->pen;
  st.source = cr->source ? gdk_paint_pattern_reference (cr->source) : nullptr;
  st.op = cr->op;
  st.fill_rule = cr->fill_rule;
  st.transform = cr->transform;
  st.clip = cr->clip;
  st.group_depth = cr->groups.size ();

  cr->stack.append (st);
}

extern "C" void
gdk_paint_restore (GdkPainter *cr)
{
  GdkPainterState st;

  if (cr->stack.isEmpty ())
    {
      cr->status = GDK_PAINT_STATUS_INVALID_RESTORE;
      return;
    }

  st = cr->stack.takeLast ();

  while (cr->groups.size () > st.group_depth)
    gdk_paint_surface_destroy (cr->groups.takeLast ());

  if (cr->source)
    gdk_paint_pattern_destroy (cr->source);

  cr->path = st.path;
  cr->brush = st.brush;
  cr->pen = st.pen;
  cr->source = st.source;
  cr->op = st.op;
  cr->fill_rule = st.fill_rule;
  cr->transform = st.transform;
  cr->clip = st.clip;

  painter_bind (cr, painter_current_surface (cr));
  painter_apply (cr);
}

extern "C" void
gdk_paint_push_group (GdkPainter *cr)
{
  gdk_paint_push_group_with_content (cr, GDK_PAINT_CONTENT_COLOR_ALPHA);
}

extern "C" void
gdk_paint_push_group_with_content (GdkPainter *cr, GdkPaintContent content)
{
  GdkPaintSurface *base = painter_current_surface (cr);
  GdkPaintSurface *group;

  if (base == nullptr)
    {
      cr->status = GDK_PAINT_STATUS_INVALID_POP_GROUP;
      return;
    }

  group = gdk_paint_surface_create_similar (base, content,
                                            base->image.width (), base->image.height ());
  group->image.fill (content == GDK_PAINT_CONTENT_COLOR_ALPHA ? QColor (Qt::transparent)
                                                             : QColor (Qt::white));

  cr->groups.append (group);
  painter_bind (cr, group);
  painter_apply (cr);
}

extern "C" GdkPaintPattern *
gdk_paint_pop_group (GdkPainter *cr)
{
  GdkPaintSurface *group;
  GdkPaintPattern *pattern;

  if (cr->groups.isEmpty ())
    {
      cr->status = GDK_PAINT_STATUS_INVALID_POP_GROUP;
      return nullptr;
    }

  group = cr->groups.takeLast ();
  painter_bind (cr, painter_current_surface (cr));
  painter_apply (cr);

  pattern = gdk_paint_pattern_create_for_surface (group);
  gdk_paint_surface_destroy (group);

  return pattern;
}

extern "C" void
gdk_paint_pop_group_to_source (GdkPainter *cr)
{
  GdkPaintPattern *pattern = gdk_paint_pop_group (cr);

  if (pattern == nullptr)
    return;

  gdk_paint_set_source (cr, pattern);
  gdk_paint_pattern_destroy (pattern);
}

extern "C" GdkPaintSurface *
gdk_paint_get_target (GdkPainter *cr)
{
  return cr->target;
}

extern "C" GdkPaintSurface *
gdk_paint_get_group_target (GdkPainter *cr)
{
  return painter_current_surface (cr);
}

/* ── source ────────────────────────────────────────────────────────────── */

extern "C" GdkPaintPattern *
gdk_paint_get_source (GdkPainter *cr)
{
  return cr->source;
}

extern "C" void
gdk_paint_set_source (GdkPainter *cr, GdkPaintPattern *source)
{
  if (cr->source)
    gdk_paint_pattern_destroy (cr->source);

  cr->source = source ? gdk_paint_pattern_reference (source) : nullptr;
  cr->brush = pattern_brush (source);
}

extern "C" void
gdk_paint_set_source_rgb (GdkPainter *cr, double red, double green, double blue)
{
  gdk_paint_set_source_rgba (cr, red, green, blue, 1.0);
}

extern "C" void
gdk_paint_set_source_rgba (GdkPainter *cr,
                           double      red,
                           double      green,
                           double      blue,
                           double      alpha)
{
  QColor color;

  color.setRgbF (qBound (0.0, red, 1.0),
                 qBound (0.0, green, 1.0),
                 qBound (0.0, blue, 1.0),
                 qBound (0.0, alpha, 1.0));

  if (cr->source)
    {
      gdk_paint_pattern_destroy (cr->source);
      cr->source = nullptr;
    }

  cr->brush = QBrush (color);
}

extern "C" void
gdk_paint_set_source_surface (GdkPainter *cr, GdkPaintSurface *surface, double x, double y)
{
  GdkPaintPattern *pattern;
  GdkPaintMatrix   matrix;

  gdk_paint_matrix_init_translate (&matrix, x, y);

  pattern = gdk_paint_pattern_create_for_surface (surface);
  gdk_paint_pattern_set_matrix (pattern, &matrix);
  gdk_paint_set_source (cr, pattern);
  gdk_paint_pattern_destroy (pattern);
}

/* ── painter state ─────────────────────────────────────────────────────── */

extern "C" void
gdk_paint_set_operator (GdkPainter *cr, GdkPaintOperator op)
{
  cr->op = op;
  painter_apply (cr);
}

extern "C" void
gdk_paint_set_fill_rule (GdkPainter *cr, GdkPaintFillRule fill_rule)
{
  cr->fill_rule = fill_rule;
  cr->path.setFillRule (qt_fill_rule (fill_rule));
}

extern "C" void
gdk_paint_set_line_width (GdkPainter *cr, double width)
{
  cr->pen.setWidthF (width);
}

extern "C" void
gdk_paint_set_line_cap (GdkPainter *cr, GdkPaintLineCap line_cap)
{
  switch (line_cap)
    {
    case GDK_PAINT_LINE_CAP_ROUND:  cr->pen.setCapStyle (Qt::RoundCap);  break;
    case GDK_PAINT_LINE_CAP_SQUARE: cr->pen.setCapStyle (Qt::SquareCap); break;
    default:                        cr->pen.setCapStyle (Qt::FlatCap);   break;
    }
}

extern "C" void
gdk_paint_set_line_join (GdkPainter *cr, GdkPaintLineJoin line_join)
{
  switch (line_join)
    {
    case GDK_PAINT_LINE_JOIN_ROUND: cr->pen.setJoinStyle (Qt::RoundJoin); break;
    case GDK_PAINT_LINE_JOIN_BEVEL: cr->pen.setJoinStyle (Qt::BevelJoin); break;
    default:                        cr->pen.setJoinStyle (Qt::MiterJoin); break;
    }
}

extern "C" void
gdk_paint_set_miter_limit (GdkPainter *cr, double limit)
{
  cr->pen.setMiterLimit (limit);
}

extern "C" void
gdk_paint_set_dash (GdkPainter *cr, const double *dashes, int num_dashes, double offset)
{
  QVector<qreal> pattern;

  for (int i = 0; i < num_dashes; i++)
    pattern.append (dashes[i]);

  cr->pen.setDashPattern (pattern);

  /* TODO: cairo shifts the dash pattern by offset; QPen has no offset. */
  (void) offset;
}

extern "C" double
gdk_paint_get_tolerance (GdkPainter *cr)
{
  return cr->tolerance;
}

/* ── transform ─────────────────────────────────────────────────────────── */

extern "C" void
gdk_paint_translate (GdkPainter *cr, double tx, double ty)
{
  cr->transform.translate (tx, ty);
  painter_apply (cr);
}

extern "C" void
gdk_paint_scale (GdkPainter *cr, double sx, double sy)
{
  cr->transform.scale (sx, sy);
  painter_apply (cr);
}

extern "C" void
gdk_paint_transform (GdkPainter *cr, const GdkPaintMatrix *matrix)
{
  /* cairo applies the new matrix in the current user space: CTM = M * CTM.
   * QTransform uses row vectors, where A*B applies A and then B, so the
   * equivalent is transform = transform * M. */
  cr->transform = cr->transform * qt_transform (matrix);
  painter_apply (cr);
}

extern "C" void
gdk_paint_set_matrix (GdkPainter *cr, const GdkPaintMatrix *matrix)
{
  cr->transform = qt_transform (matrix);
  painter_apply (cr);
}

extern "C" void
gdk_paint_get_matrix (GdkPainter *cr, GdkPaintMatrix *matrix)
{
  *matrix = matrix_from_qt (cr->transform);
}

extern "C" void
gdk_paint_identity_matrix (GdkPainter *cr)
{
  cr->transform = QTransform ();
  painter_apply (cr);
}

extern "C" void
gdk_paint_user_to_device (GdkPainter *cr, double *x, double *y)
{
  QPointF p = cr->transform.map (QPointF (*x, *y));

  *x = p.x ();
  *y = p.y ();
}

extern "C" void
gdk_paint_user_to_device_distance (GdkPainter *cr, double *dx, double *dy)
{
  QPointF o = cr->transform.map (QPointF (0, 0));
  QPointF p = cr->transform.map (QPointF (*dx, *dy));

  *dx = p.x () - o.x ();
  *dy = p.y () - o.y ();
}

/* ── path construction ─────────────────────────────────────────────────── */

extern "C" void
gdk_paint_new_path (GdkPainter *cr)
{
  cr->path = QPainterPath ();
  cr->path.setFillRule (qt_fill_rule (cr->fill_rule));
}

extern "C" void
gdk_paint_new_sub_path (GdkPainter *cr)
{
  if (cr->path.elementCount () > 0)
    cr->path.moveTo (cr->path.currentPosition ());
}

extern "C" void
gdk_paint_close_path (GdkPainter *cr)
{
  cr->path.closeSubpath ();
}

extern "C" void
gdk_paint_move_to (GdkPainter *cr, double x, double y)
{
  cr->path.moveTo (x, y);
}

extern "C" void
gdk_paint_line_to (GdkPainter *cr, double x, double y)
{
  if (cr->path.elementCount () == 0)
    cr->path.moveTo (x, y);
  else
    cr->path.lineTo (x, y);
}

extern "C" void
gdk_paint_curve_to (GdkPainter *cr, double x1, double y1,
                    double x2, double y2, double x3, double y3)
{
  if (cr->path.elementCount () == 0)
    cr->path.moveTo (x1, y1);

  cr->path.cubicTo (x1, y1, x2, y2, x3, y3);
}

extern "C" void
gdk_paint_arc (GdkPainter *cr, double xc, double yc, double radius,
               double angle1, double angle2)
{
  QRectF rect (xc - radius, yc - radius, 2 * radius, 2 * radius);

  if (cr->path.elementCount () == 0)
    cr->path.moveTo (xc + radius * cos (angle1), yc + radius * sin (angle1));

  cr->path.arcTo (rect, angle1 * 180.0 / M_PI, (angle2 - angle1) * 180.0 / M_PI);
}

extern "C" void
gdk_paint_arc_negative (GdkPainter *cr, double xc, double yc, double radius,
                        double angle1, double angle2)
{
  QRectF rect (xc - radius, yc - radius, 2 * radius, 2 * radius);

  if (cr->path.elementCount () == 0)
    cr->path.moveTo (xc + radius * cos (angle1), yc + radius * sin (angle1));

  cr->path.arcTo (rect, angle1 * 180.0 / M_PI, (angle2 - angle1) * 180.0 / M_PI);
}

extern "C" void
gdk_paint_rectangle (GdkPainter *cr, double x, double y, double width, double height)
{
  cr->path.addRect (x, y, width, height);
}

extern "C" int
gdk_paint_has_current_point (GdkPainter *cr)
{
  return cr->path.elementCount () > 0;
}

extern "C" void
gdk_paint_get_current_point (GdkPainter *cr, double *x, double *y)
{
  QPointF p;

  if (cr->path.elementCount () == 0)
    {
      *x = 0;
      *y = 0;
      cr->status = GDK_PAINT_STATUS_NO_CURRENT_POINT;
      return;
    }

  p = cr->path.currentPosition ();
  *x = p.x ();
  *y = p.y ();
}

/* QPainterPath's element stream is already cairo's: a MoveToElement or a
 * LineToElement is one point, a CurveToElement is followed by two
 * CurveToDataElements and is three points. */
static GdkPaintPath *
path_from_qt (const QPainterPath &path)
{
  GdkPaintPath     *result = new GdkPaintPath ();
  QVector<GdkPaintPathData> data;

  for (int i = 0; i < path.elementCount (); i++)
    {
      QPainterPath::Element e = path.elementAt (i);

      if (e.type == QPainterPath::MoveToElement ||
          e.type == QPainterPath::LineToElement)
        {
          GdkPaintPathData header;
          GdkPaintPathData point;

          header.header.type = e.type == QPainterPath::MoveToElement
                                   ? GDK_PAINT_PATH_MOVE_TO
                                   : GDK_PAINT_PATH_LINE_TO;
          header.header.length = 2;
          point.point.x = e.x;
          point.point.y = e.y;
          data.append (header);
          data.append (point);
        }
      else if (e.type == QPainterPath::CurveToElement)
        {
          GdkPaintPathData header;

          header.header.type = GDK_PAINT_PATH_CURVE_TO;
          header.header.length = 4;
          data.append (header);

          for (int j = 0; j < 3 && i + j < path.elementCount (); j++)
            {
              QPainterPath::Element c = path.elementAt (i + j);
              GdkPaintPathData point;

              point.point.x = c.x;
              point.point.y = c.y;
              data.append (point);
            }

          i += 2;
        }
      /* CurveToDataElement is consumed with its CurveToElement above. */
    }

  result->num_data = data.size ();
  result->data = new GdkPaintPathData[result->num_data > 0 ? result->num_data : 1];
  for (int i = 0; i < result->num_data; i++)
    result->data[i] = data[i];
  result->status = GDK_PAINT_STATUS_SUCCESS;

  return result;
}

static void
path_append_qt (QPainterPath *path, const GdkPaintPath *source)
{
  int i = 0;

  if (source == nullptr || source->data == nullptr)
    return;

  while (i < source->num_data)
    {
      int type = source->data[i].header.type;
      int length = source->data[i].header.length;

      if (type == GDK_PAINT_PATH_CLOSE_PATH)
        {
          path->closeSubpath ();
          i += 1;
        }
      else if (type == GDK_PAINT_PATH_MOVE_TO && length >= 2)
        {
          path->moveTo (source->data[i + 1].point.x, source->data[i + 1].point.y);
          i += length;
        }
      else if (type == GDK_PAINT_PATH_LINE_TO && length >= 2)
        {
          path->lineTo (source->data[i + 1].point.x, source->data[i + 1].point.y);
          i += length;
        }
      else if (type == GDK_PAINT_PATH_CURVE_TO && length >= 4)
        {
          path->cubicTo (source->data[i + 1].point.x, source->data[i + 1].point.y,
                         source->data[i + 2].point.x, source->data[i + 2].point.y,
                         source->data[i + 3].point.x, source->data[i + 3].point.y);
          i += length;
        }
      else
        {
          i += length > 0 ? length : 1;
        }
    }
}

extern "C" GdkPaintPath *
gdk_paint_copy_path (GdkPainter *cr)
{
  return path_from_qt (cr->path);
}

extern "C" void
gdk_paint_append_path (GdkPainter *cr, const GdkPaintPath *path)
{
  path_append_qt (&cr->path, path);
}

extern "C" void
gdk_paint_path_destroy (GdkPaintPath *path)
{
  if (path == nullptr)
    return;

  delete[] path->data;
  delete path;
}

/* ── drawing ───────────────────────────────────────────────────────────── */

extern "C" void
gdk_paint_fill (GdkPainter *cr)
{
  if (cr->qp == nullptr)
    return;

  painter_apply (cr);
  cr->path.setFillRule (qt_fill_rule (cr->fill_rule));
  cr->qp->setPen (Qt::NoPen);
  cr->qp->fillPath (cr->path, cr->brush);
  cr->path = QPainterPath ();
  cr->path.setFillRule (qt_fill_rule (cr->fill_rule));
}

extern "C" void
gdk_paint_stroke (GdkPainter *cr)
{
  if (cr->qp == nullptr)
    return;

  painter_apply (cr);
  cr->qp->setPen (painter_stroke_pen (cr));
  cr->qp->setBrush (Qt::NoBrush);
  cr->qp->drawPath (cr->path);
  cr->path = QPainterPath ();
  cr->path.setFillRule (qt_fill_rule (cr->fill_rule));
}

extern "C" void
gdk_paint_paint (GdkPainter *cr)
{
  if (cr->qp == nullptr)
    return;

  painter_apply (cr);
  cr->qp->fillRect (painter_user_rect (cr), cr->brush);
}

extern "C" void
gdk_paint_paint_with_alpha (GdkPainter *cr, double alpha)
{
  if (cr->qp == nullptr)
    return;

  painter_apply (cr);

  /* TODO: cairo paints the source at a global alpha.  QPainter has no
   * per-operation global alpha, so the alpha is folded into the brush where
   * the brush is a plain colour and ignored otherwise. */
  if (cr->brush.style () == Qt::SolidPattern)
    {
      QColor c = cr->brush.color ();
      c.setAlphaF (qBound (0.0, alpha * c.alphaF (), 1.0));
      cr->qp->fillRect (painter_user_rect (cr), QBrush (c));
    }
  else
    {
      cr->qp->fillRect (painter_user_rect (cr), cr->brush);
    }
}

/* cairo_mask has no QPainter equivalent: it is composited as a temporary
 * layer (source over transparent, then DestinationIn with the mask) so the
 * result matches the common "source through a mask" use. */
extern "C" void
gdk_paint_mask (GdkPainter *cr, GdkPaintPattern *pattern)
{
  QRect      dev;
  QImage     layer;
  QBrush     mask;
  QRectF     user;
  QTransform t;

  if (cr->qp == nullptr || pattern == nullptr)
    return;

  dev = painter_device_rect (cr);
  if (dev.isEmpty ())
    return;

  layer = QImage (dev.size (), QImage::Format_ARGB32_Premultiplied);
  layer.fill (Qt::transparent);
  mask = pattern_brush (pattern);
  user = painter_user_rect (cr);
  t = QTransform::fromTranslate (-dev.x (), -dev.y ()) * cr->transform;

  {
    QPainter p (&layer);

    p.setRenderHint (QPainter::Antialiasing, true);
    p.setTransform (t);
    p.setCompositionMode (QPainter::CompositionMode_SourceOver);
    p.fillRect (user, cr->brush);
    p.setCompositionMode (QPainter::CompositionMode_DestinationIn);
    p.fillRect (user, mask);
  }

  cr->qp->setCompositionMode (qt_composition (cr->op));
  cr->qp->setTransform (QTransform::fromTranslate (dev.x (), dev.y ()));
  cr->qp->fillRect (QRectF (0, 0, dev.width (), dev.height ()), QBrush (layer));
  painter_apply (cr);
}

extern "C" void
gdk_paint_mask_surface (GdkPainter *cr, GdkPaintSurface *surface, double x, double y)
{
  GdkPaintPattern *pattern;
  GdkPaintMatrix   matrix;

  if (surface == nullptr)
    return;

  pattern = gdk_paint_pattern_create_for_surface (surface);
  gdk_paint_matrix_init_translate (&matrix, x, y);
  gdk_paint_pattern_set_matrix (pattern, &matrix);
  gdk_paint_mask (cr, pattern);
  gdk_paint_pattern_destroy (pattern);
}

/* ── clip ──────────────────────────────────────────────────────────────── */

extern "C" void
gdk_paint_clip (GdkPainter *cr)
{
  QPainterPath device = cr->transform.map (cr->path);

  cr->clip.append (device);
  cr->path = QPainterPath ();
  cr->path.setFillRule (qt_fill_rule (cr->fill_rule));
  painter_apply (cr);
}

extern "C" void
gdk_paint_clip_extents (GdkPainter *cr, double *x1, double *y1, double *x2, double *y2)
{
  QTransform inv;
  QRectF     device = cr->clip.isEmpty () ? QRectF (painter_device_rect (cr))
                                          : cr->clip.first ().boundingRect ();
  QRectF     user;

  for (int i = 1; i < cr->clip.size (); i++)
    device = device.intersected (cr->clip[i].boundingRect ());

  user = cr->transform.inverted ().mapRect (device);

  *x1 = user.left ();
  *y1 = user.top ();
  *x2 = user.right ();
  *y2 = user.bottom ();
}

extern "C" void
gdk_paint_reset_clip (GdkPainter *cr)
{
  cr->clip.clear ();
  painter_apply (cr);
}

/* ── user data ─────────────────────────────────────────────────────────── */

extern "C" void *
gdk_paint_get_user_data (GdkPainter *cr, const GdkPaintUserDataKey *key)
{
  auto it = cr->user_data.constFind ((const void *) key);

  return it == cr->user_data.constEnd () ? nullptr : it.value ().first;
}

extern "C" GdkPaintStatus
gdk_paint_set_user_data (GdkPainter            *cr,
                         const GdkPaintUserDataKey *key,
                         void                  *user_data,
                         GdkPaintDestroyFunc    destroy)
{
  auto it = cr->user_data.find ((const void *) key);

  if (it != cr->user_data.end ())
    {
      if (it.value ().second && it.value ().first != user_data)
        it.value ().second (it.value ().first);

      if (user_data == nullptr)
        {
          cr->user_data.erase (it);
          return GDK_PAINT_STATUS_SUCCESS;
        }
    }

  cr->user_data[(const void *) key] =
      QPair<void *, GdkPaintDestroyFunc> (user_data, destroy);

  return GDK_PAINT_STATUS_SUCCESS;
}
