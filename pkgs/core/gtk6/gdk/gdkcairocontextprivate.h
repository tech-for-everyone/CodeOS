/* GDK - The GIMP Drawing Kit
 *
 * gdkcairocontextprivate.h: specific Cairo wrappers
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

#pragma once

#include "gdkcairocontext.h"

#include "gdkdrawcontextprivate.h"

#include <gdk/gdkpaint.h>

G_BEGIN_DECLS

typedef struct _GdkPaintContextFrame GdkPaintContextFrame;

struct _GdkPaintContextFrame
{
  GdkDrawContextFrame parent;

  GdkPaintSurface *surface;
};

struct _GdkPaintContext
{
  GdkDrawContext parent_instance;
};

struct _GdkPaintContextClass
{
  GdkDrawContextClass parent_class;
};

void                    gdk_paint_context_frame_set_surface     (GdkPaintContextFrame   *frame,
                                                                 GdkPaintSurface        *surface);
GdkPaintSurface *       gdk_paint_context_frame_get_surface     (GdkPaintContextFrame   *frame);

G_END_DECLS

