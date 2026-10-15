/* GdkText
 * pango-color.h: A color struct
 *
 * Copyright (C) 2000 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __PANGO_COLOR_H__
#define __PANGO_COLOR_H__

#include <pango/pango-types.h>
#include <glib-object.h>

G_BEGIN_DECLS


typedef struct _PangoColor GdkTextColor;

/**
 * GdkTextColor:
 * @red: value of red component
 * @green: value of green component
 * @blue: value of blue component
 *
 * The `GdkTextColor` structure is used to
 * represent a color in an uncalibrated RGB color-space.
 */
struct _PangoColor
{
  guint16 red;
  guint16 green;
  guint16 blue;
};

#define GDK_TEXT_TYPE_COLOR (gdk_text_color_get_type ())

GDK_TEXT_AVAILABLE_IN_ALL
GType       gdk_text_color_get_type         (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextColor *gdk_text_color_copy             (const GdkTextColor *src);

GDK_TEXT_AVAILABLE_IN_ALL
void        gdk_text_color_free             (GdkTextColor       *color);

GDK_TEXT_AVAILABLE_IN_ALL
gboolean    gdk_text_color_parse            (GdkTextColor       *color,
                                          const char       *spec);

GDK_TEXT_AVAILABLE_IN_1_46
gboolean    gdk_text_color_parse_with_alpha (GdkTextColor       *color,
                                          guint16          *alpha,
                                          const char       *spec);

GDK_TEXT_AVAILABLE_IN_1_16
char       *gdk_text_color_to_string        (const GdkTextColor *color);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextColor, gdk_text_color_free)

G_END_DECLS

#endif /* __PANGO_COLOR_H__ */
