/* GSK - The GTK Scene Kit
 *
 * Copyright 2016  Endless
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gskcaironodeprivate.h"

#include "gskrectprivate.h"
#include "gskrendernodeprivate.h"

#include "gdk/gdkcairoprivate.h"

/**
 * GskCairoNode:
 *
 * A render node for a Cairo surface.
 */
struct _GskCairoNode
{
  GskRenderNode render_node;

  GdkPaintSurface *surface;
};

static void
gsk_cairo_node_finalize (GskRenderNode *node)
{
  GskCairoNode *self = (GskCairoNode *) node;
  GskRenderNodeClass *parent_class = g_type_class_peek (g_type_parent (GSK_TYPE_CAIRO_NODE));

  if (self->surface)
    gdk_paint_surface_destroy (self->surface);

  parent_class->finalize (node);
}

static void
gsk_cairo_node_draw (GskRenderNode *node,
                     GdkPainter       *cr,
                     GskCairoData  *data)
{
  GskCairoNode *self = (GskCairoNode *) node;

  if (self->surface == NULL)
    return;

  if (gdk_color_state_equal (data->ccs, GDK_COLOR_STATE_SRGB))
    {
      gdk_paint_set_source_surface (cr, self->surface, 0, 0);
      gdk_paint_paint (cr);
    }
  else
    {
      gdk_paint_save (cr);
      gdk_painter_rect (cr, &node->bounds);
      gdk_paint_clip (cr);
      gdk_paint_push_group (cr);

      gdk_paint_set_source_surface (cr, self->surface, 0, 0);
      gdk_paint_paint (cr);
      gdk_painter_surface_convert_color_state (gdk_paint_get_group_target (cr),
                                             GDK_COLOR_STATE_SRGB,
                                             data->ccs);
      gdk_paint_pop_group_to_source (cr);
      gdk_paint_paint (cr);
      gdk_paint_restore (cr);
    }
}

static GskRenderNode *
gsk_cairo_node_replay (GskRenderNode   *node,
                       GskRenderReplay *replay)
{
  return gsk_render_node_ref (node);
}

static void
gsk_cairo_node_class_init (gpointer g_class,
                           gpointer class_data)
{
  GskRenderNodeClass *node_class = g_class;

  node_class->node_type = GSK_CAIRO_NODE;

  node_class->finalize = gsk_cairo_node_finalize;
  node_class->draw = gsk_cairo_node_draw;
  node_class->replay = gsk_cairo_node_replay;
}

GSK_DEFINE_RENDER_NODE_TYPE (GskCairoNode, gsk_cairo_node)

/**
 * gsk_cairo_node_get_surface:
 * @node: (type GskCairoNode): a `GskRenderNode` for a Cairo surface
 *
 * Retrieves the Cairo surface used by the render node.
 *
 * Returns: (transfer none): a Cairo surface
 */
GdkPaintSurface *
gsk_cairo_node_get_surface (GskRenderNode *node)
{
  GskCairoNode *self = (GskCairoNode *) node;

  g_return_val_if_fail (GSK_IS_RENDER_NODE_TYPE (node, GSK_CAIRO_NODE), NULL);

  return self->surface;
}

void
gsk_cairo_node_set_surface (GskRenderNode   *node,
                            GdkPaintSurface *surface)
{
  GskCairoNode *self = (GskCairoNode *) node;

  g_return_if_fail (GSK_IS_RENDER_NODE_TYPE (node, GSK_CAIRO_NODE));
  g_return_if_fail (self->surface == NULL);

  self->surface = gdk_paint_surface_reference (surface);
}

/**
 * gsk_cairo_node_new:
 * @bounds: the rectangle to render to
 *
 * Creates a `GskRenderNode` that will render a cairo surface
 * into the area given by @bounds.
 *
 * You can draw to the cairo surface using [method@Gsk.CairoNode.get_draw_context].
 *
 * Returns: (transfer full) (type GskCairoNode): A new `GskRenderNode`
 */
GskRenderNode *
gsk_cairo_node_new (const graphene_rect_t *bounds)
{
  GskCairoNode *self;
  GskRenderNode *node;

  g_return_val_if_fail (bounds != NULL, NULL);

  self = gsk_render_node_alloc (GSK_TYPE_CAIRO_NODE);
  node = (GskRenderNode *) self;
  node->preferred_depth = gdk_color_state_get_depth (GDK_COLOR_STATE_SRGB);

  gsk_rect_init_from_rect (&node->bounds, bounds);
  gsk_rect_normalize (&node->bounds);

  return node;
}

/**
 * gsk_cairo_node_get_draw_context:
 * @node: (type GskCairoNode): a `GskRenderNode` for a Cairo surface
 *
 * Creates a Cairo context for drawing using the surface associated
 * to the render node.
 *
 * If no surface exists yet, a surface will be created optimized for
 * rendering to @renderer.
 *
 * Returns: (transfer full): a Cairo context used for drawing; use
 *   gdk_paint_destroy() when done drawing
 */
GdkPainter *
gsk_cairo_node_get_draw_context (GskRenderNode *node)
{
  GskCairoNode *self = (GskCairoNode *) node;
  int width, height;
  GdkPainter *res;

  g_return_val_if_fail (GSK_IS_RENDER_NODE_TYPE (node, GSK_CAIRO_NODE), NULL);

  width = ceilf (node->bounds.size.width);
  height = ceilf (node->bounds.size.height);

  if (width <= 0 || height <= 0)
    {
      GdkPaintSurface *surface = gdk_paint_image_surface_create (GDK_PAINT_FORMAT_ARGB32, 0, 0);
      res = gdk_paint_create (surface);
      gdk_paint_surface_destroy (surface);
    }
  else if (self->surface == NULL)
    {
      self->surface = gdk_paint_recording_surface_create (GDK_PAINT_CONTENT_COLOR_ALPHA,
                                                      &(GdkPaintRectangle) {
                                                          node->bounds.origin.x,
                                                          node->bounds.origin.y,
                                                          node->bounds.size.width,
                                                          node->bounds.size.height
                                                      });
      res = gdk_paint_create (self->surface);
    }
  else
    {
      res = gdk_paint_create (self->surface);
    }

  gdk_painter_rect (res, &node->bounds);
  gdk_paint_clip (res);

  return res;
}

