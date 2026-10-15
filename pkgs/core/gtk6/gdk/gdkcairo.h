/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#if !defined (__GDK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gdk/gdk.h> can be included directly."
#endif

#include <gdk/gdktypes.h>
#include <gdk/gdkpixbuf.h>
#include <pango/pangocairo.h>

G_BEGIN_DECLS

GDK_AVAILABLE_IN_ALL
void       gdk_painter_set_source_rgba    (GdkPainter              *cr,
                                         const GdkRGBA        *rgba);
GDK_DEPRECATED_IN_4_20
void       gdk_painter_set_source_pixbuf  (GdkPainter              *cr,
                                         const GdkPixbuf      *pixbuf,
                                         double                pixbuf_x,
                                         double                pixbuf_y);

GDK_AVAILABLE_IN_ALL
void       gdk_painter_rectangle          (GdkPainter              *cr,
                                         const GdkRectangle   *rectangle);
GDK_AVAILABLE_IN_ALL
void       gdk_painter_region             (GdkPainter              *cr,
                                         const GdkRegion *region);

GDK_AVAILABLE_IN_ALL
GdkRegion *
           gdk_painter_region_create_from_surface
                                        (GdkPaintSurface      *surface);

GDK_DEPRECATED_IN_4_6_FOR(gdk_gl_texture_new)
void       gdk_painter_draw_from_gl (GdkPainter              *cr,
                                   GdkSurface            *surface,
                                   int                   source,
                                   int                   source_type,
                                   int                   buffer_scale,
                                   int                   x,
                                   int                   y,
                                   int                   width,
                                   int                   height);

G_END_DECLS

