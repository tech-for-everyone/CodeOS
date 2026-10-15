/* GSK - The GTK Scene Kit
 *
 * Copyright 2017 Red Hat, Inc.
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

#pragma once

#include <pango/pango.h>
#include "gtk/gtksnapshot.h"
#include "gtk/gtkcssstyleprivate.h"

G_BEGIN_DECLS

#define GSK_TYPE_PANGO_RENDERER            (gsk_text_renderer_get_type ())
#define GSK_PANGO_RENDERER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_PANGO_RENDERER, GskTextRenderer))
#define GSK_IS_PANGO_RENDERER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_PANGO_RENDERER))
#define GSK_PANGO_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_PANGO_RENDERER, GskTextRendererClass))
#define GSK_IS_PANGO_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_PANGO_RENDERER))
#define GSK_PANGO_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_PANGO_RENDERER, GskTextRendererClass))

typedef struct _GskPangoRenderer      GskTextRenderer;
typedef struct _GskPangoRendererClass GskTextRendererClass;

typedef enum
{
  GSK_PANGO_RENDERER_NORMAL,
  GSK_PANGO_RENDERER_SELECTED,
  GSK_PANGO_RENDERER_CURSOR
} GskTextRendererState;

typedef gboolean (*GskTextShapeHandler) (GdkTextAttrShape         *attr,
                                          GdkSnapshot            *snapshot,
                                          double                  width,
                                          double                  height);

/*
 * This is a GdkTextRenderer implementation that translates all the draw calls to
 * gsk render nodes, using the GtkSnapshot helper class. Glyphs are translated
 * to text nodes, other draw calls may fall back to cairo nodes.
 */

struct _GskPangoRenderer
{
  GdkTextRenderer          parent_instance;

  GtkWidget             *widget;
  GtkSnapshot           *snapshot;
  GdkColor               fg_color;
  GtkCssStyle           *shadow_style;

  /* Error underline color for this widget */
  GdkRGBA               *error_color;

  GskTextRendererState  state;

  guint                  is_cached_renderer : 1;

  GskTextShapeHandler   shape_handler;
};

struct _GskPangoRendererClass
{
  GdkTextRendererClass parent_class;
};

GType             gsk_text_renderer_get_type  (void);
void              gsk_text_renderer_set_state (GskTextRenderer      *crenderer,
                                                GskTextRendererState  state);
void              gsk_text_renderer_set_shape_handler (GskTextRenderer      *crenderer,
                                                        GskTextShapeHandler handler);
GskTextRenderer *gsk_text_renderer_acquire   (void);
void              gsk_text_renderer_release   (GskTextRenderer      *crenderer);

G_END_DECLS

