/*
 * Copyright © 2025 Red Hat, Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include "gtksvgpangoutilsprivate.h"
#include "gsk/gskpathbuilder.h"

GskPath *
svg_pango_layout_to_path (PangoLayout          *layout,
                          PangoRenderComponent  component)
{
  GdkPaintSurface *surface;
  GdkPainter *cr;
  GdkPaintPath *path;
  GskPathBuilder *builder;
  PangoRectangle rect;

  pango_layout_get_pixel_extents (layout, &rect, NULL);
  surface = gdk_paint_recording_surface_create (GDK_PAINT_CONTENT_COLOR_ALPHA,
                                            &(GdkPaintRectangle) { rect.x, rect.y, rect.width, rect.height });

  cr = gdk_paint_create (surface);
  pango_cairo_layout_path_for_components (cr, layout, component);
  path = gdk_paint_copy_path (cr);
  builder = gsk_path_builder_new ();
  gsk_path_builder_add_cairo_path (builder, path);
  gdk_paint_path_destroy (path);
  gdk_paint_destroy (cr);
  gdk_paint_surface_destroy (surface);

  return gsk_path_builder_free_to_path (builder);
}
