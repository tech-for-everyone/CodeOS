/* GDK - The GIMP Drawing Kit
 *
 * gdkcairocontext.c: Cairo wrappers
 *
 * Copyright © 2018  Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gdkcairocontext.h"

#include "gdkcairocontextprivate.h"

#include "gdkcairo.h"
#include "gdksurface.h"

/**
 * GdkPaintContext:
 *
 * Represents the platform-specific draw context.
 *
 * `GdkPaintContext`s are created for a surface using
 * [method@Gdk.Surface.create_cairo_context], and the context
 * can then be used to draw on that surface.
 */

typedef struct _GdkPaintContextPrivate GdkPaintContextPrivate;

struct _GdkPaintContextPrivate {
  gpointer unused;
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GdkPaintContext, gdk_paint_context, GDK_TYPE_DRAW_CONTEXT,
                                  G_ADD_PRIVATE (GdkPaintContext))

static void
gdk_paint_context_finalize_frame (GdkDrawContext      *context,
                                  GdkDrawContextFrame *draw_frame)
{
  GdkPaintContextFrame *frame = (GdkPaintContextFrame *) draw_frame;

  g_clear_pointer (&frame->surface, gdk_paint_surface_destroy);

  GDK_DRAW_CONTEXT_CLASS (gdk_paint_context_parent_class)->finalize_frame (context, draw_frame);
}

static void
gdk_paint_context_class_init (GdkPaintContextClass *klass)
{
  GdkDrawContextClass *context_class = GDK_DRAW_CONTEXT_CLASS (klass);

  context_class->frame_size = sizeof (GdkPaintContextFrame);

  context_class->finalize_frame = gdk_paint_context_finalize_frame;
}

static void
gdk_paint_context_init (GdkPaintContext *self)
{
}

/**
 * gdk_paint_context_get_painter:
 * @self: a `GdkPaintContext` that is currently drawing
 *
 * Retrieves a Cairo context to be used to draw on the `GdkSurface`
 * of @context.
 *
 * A call to [method@Gdk.DrawContext.begin_frame] with this
 * @context must have been done or this function will return %NULL.
 *
 * The returned context is guaranteed to be valid until
 * [method@Gdk.DrawContext.end_frame] is called.
 *
 * Returns: (transfer full) (nullable): a Cairo context
 *   to draw on `GdkSurface
 *
 * Deprecated: 4.18: Drawing content with Cairo should be done via
 *   Cairo rendernodes, not by using renderers.
 */
GdkPainter *
gdk_paint_context_get_painter (GdkPaintContext *self)
{
  GdkDrawContext *draw_context;
  GdkDrawContextFrame *frame;
  GdkPaintContextFrame *cframe;
  GdkPainter *cr;
  double scale;

  g_return_val_if_fail (GDK_IS_CAIRO_CONTEXT (self), NULL);

  draw_context = GDK_DRAW_CONTEXT (self);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  if (!gdk_draw_context_is_in_frame (draw_context))
    return NULL;
G_GNUC_END_IGNORE_DEPRECATIONS

  frame = gdk_draw_context_get_current_frame (GDK_DRAW_CONTEXT (self));
  cframe = (GdkPaintContextFrame *) frame;
  if (cframe == NULL || cframe->surface == NULL)
    return NULL;

  cr = gdk_paint_create (cframe->surface);

  gdk_painter_region (cr, gdk_draw_context_frame_get_damage (frame));
  gdk_paint_clip (cr);

  scale = gdk_surface_get_scale (gdk_draw_context_get_surface (draw_context));
  gdk_paint_scale (cr, scale, scale);

  return cr;
}

/**
 * gdk_paint_context_frame_set_surface:
 * @frame: the frame
 * @surface: (transfer full) (nullable): the surface to use for rendering to this frame
 *
 * This API is called by backends to set the surface that belongs to this frame.
 *
 * The surface must have been set after begin_frame() and can be unset again in
 * end_frame(), but it must exist inbetween.
 *
 * Backends tend to use one of two methods here: Either they create and assign a
 * surface during initialize_frame() and keep reusing the surface or they recreate
 * a new surfaces every frame in begin_frame() and unset them again in end_frame().
 **/
void
gdk_paint_context_frame_set_surface (GdkPaintContextFrame *frame,
                                     GdkPaintSurface     *surface)
{
  g_clear_pointer (&frame->surface, gdk_paint_surface_destroy);
  frame->surface = surface;
}

/*
 * gdk_paint_context_frame_get_surface:
 * @frame: the frame
 *
 * Queries the surface that's used for this frame. A surface may only exist
 * between begin/end_frame() calls, backends are free to release the surface
 * afterwards.
 *
 * Returns: the surface to render to or NULL if none exists yet
 **/
GdkPaintSurface *
gdk_paint_context_frame_get_surface (GdkPaintContextFrame *frame)
{
  return frame->surface;
}

