/* gdkdmabuftexture.h
 *
 * Copyright 2023 Red Hat, Inc.
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

#if !defined (__GDK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gdk/gdk.h> can be included directly."
#endif

#include <gdk/gdktypes.h>
#include <gdk/gdktexture.h>

G_BEGIN_DECLS

#define GDK_TYPE_DMABUF_TEXTURE (gdk_dmabuf_texture_get_type ())

GDK_AVAILABLE_IN_4_14
GDK_DECLARE_INTERNAL_TYPE (GdkDmabufTexture, gdk_dmabuf_texture, GDK, DMABUF_TEXTURE, GdkTexture)

#define GDK_DMABUF_ERROR       (gdk_dmabuf_error_quark ())

GDK_AVAILABLE_IN_4_14
GQuark                  gdk_dmabuf_error_quark                     (void);

G_END_DECLS
