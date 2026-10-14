/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK6 GSK-on-Qt6 renderer — see gskqt.h.
 *
 * This is the software writer of the three GSK jobs: QPainter talks to
 * whichever paint engine Qt chose (raster on framebuffer, raster on QImage),
 * so one renderer covers the framebuffer, print-to-image and offscreen
 * snapshot cases GTK builds nodes for.
 */

#include "gskqt.h"

#include <QtCore/QRectF>
#include <QtGui/QColor>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtGui/QBrush>
#include <QtGui/QTransform>

struct _GskQtRenderer
{
  QPainter *painter = nullptr;
  double    width   = 0;
  double    height  = 0;
};

/* ── colour ────────────────────────────────────────────────────────────── */

static QColor
gsk_qt_color (uint32_t rgba)
{
  return QColor::fromRgba (rgba);
}

/* ── lifetime ──────────────────────────────────────────────────────────── */

extern "C" GskQtRenderer *
gsk_qt_renderer_new (void)
{
  return new GskQtRenderer ();
}

extern "C" void
gsk_qt_renderer_free (GskQtRenderer *renderer)
{
  delete renderer;
}

/* ── frames ────────────────────────────────────────────────────────────── */

extern "C" void
gsk_qt_renderer_begin_frame (GskQtRenderer *renderer,
                             void          *painter,
                             double         width,
                             double         height)
{
  if (!renderer)
    return;

  renderer->painter = static_cast<QPainter *> (painter);
  renderer->width   = width;
  renderer->height  = height;

  if (renderer->painter)
    {
      renderer->painter->setRenderHint (QPainter::Antialiasing, true);
      renderer->painter->setRenderHint (QPainter::TextAntialiasing, true);
      renderer->painter->save ();
    }
}

extern "C" void
gsk_qt_renderer_end_frame (GskQtRenderer *renderer)
{
  if (!renderer)
    return;

  if (renderer->painter)
    {
      renderer->painter->restore ();
      renderer->painter->end ();
    }

  renderer->painter = nullptr;
}

/* ── clips and transforms ──────────────────────────────────────────────── */

extern "C" void
gsk_qt_renderer_push_clip (GskQtRenderer *renderer,
                           double         x,
                           double         y,
                           double         width,
                           double         height)
{
  if (!renderer || !renderer->painter)
    return;

  renderer->painter->save ();
  renderer->painter->setClipRect (QRectF (x, y, width, height), Qt::IntersectClip);
}

extern "C" void
gsk_qt_renderer_pop_clip (GskQtRenderer *renderer)
{
  if (!renderer || !renderer->painter)
    return;

  renderer->painter->restore ();
}

extern "C" void
gsk_qt_renderer_push_transform (GskQtRenderer *renderer,
                                double         dx,
                                double         dy)
{
  if (!renderer || !renderer->painter)
    return;

  renderer->painter->save ();
  renderer->painter->translate (dx, dy);
}

extern "C" void
gsk_qt_renderer_pop_transform (GskQtRenderer *renderer)
{
  if (!renderer || !renderer->painter)
    return;

  renderer->painter->restore ();
}

/* ── primitives ────────────────────────────────────────────────────────── */

extern "C" void
gsk_qt_renderer_fill_rect (GskQtRenderer *renderer,
                           double         x,
                           double         y,
                           double         width,
                           double         height,
                           uint32_t        rgba)
{
  if (!renderer || !renderer->painter)
    return;

  renderer->painter->fillRect (QRectF (x, y, width, height), gsk_qt_color (rgba));
}

extern "C" void
gsk_qt_renderer_stroke_rect (GskQtRenderer *renderer,
                             double         x,
                             double         y,
                             double         width,
                             double         height,
                             double         line_width,
                             uint32_t        rgba)
{
  if (!renderer || !renderer->painter)
    return;

  QPen pen (gsk_qt_color (rgba));
  pen.setWidthF (line_width);

  renderer->painter->setPen (pen);
  renderer->painter->setBrush (Qt::NoBrush);
  renderer->painter->drawRect (QRectF (x, y, width, height));
}

extern "C" void
gsk_qt_renderer_draw_text (GskQtRenderer *renderer,
                           GtkQtText     *text,
                           double         x,
                           double         y,
                           uint32_t        rgba)
{
  if (!renderer || !renderer->painter || !text)
    return;

  renderer->painter->setPen (QPen (gsk_qt_color (rgba)));
  gtk_qt_text_draw (text, renderer->painter, x, y);
}
