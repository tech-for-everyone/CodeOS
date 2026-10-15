/* GdkText
 * pango-fontset-simple.h: Font set handling
 *
 * Copyright (C) 2001 Red Hat Software
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

#ifndef __PANGO_FONTSET_SIMPLE_H__
#define __PANGO_FONTSET_SIMPLE_H__

#include <pango/pango-coverage.h>
#include <pango/pango-types.h>
#include <pango/pango-fontset.h>

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GdkTextFontsetSimple:
 *
 * `GdkTextFontsetSimple` is a implementation of the abstract
 * `GdkTextFontset` base class as an array of fonts.
 *
 * When creating a `GdkTextFontsetSimple`, you have to provide
 * the array of fonts that make up the fontset.
 */
#define GDK_TEXT_TYPE_FONTSET_SIMPLE       (gdk_text_fontset_simple_get_type ())
#define GDK_TEXT_FONTSET_SIMPLE(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FONTSET_SIMPLE, GdkTextFontsetSimple))
#define GDK_TEXT_IS_FONTSET_SIMPLE(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FONTSET_SIMPLE))

typedef struct _PangoFontsetSimple  GdkTextFontsetSimple;
typedef struct _PangoFontsetSimpleClass  GdkTextFontsetSimpleClass;


GDK_TEXT_AVAILABLE_IN_ALL
GType                   gdk_text_fontset_simple_get_type (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontsetSimple *    gdk_text_fontset_simple_new    (GdkTextLanguage      *language);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_fontset_simple_append (GdkTextFontsetSimple *fontset,
                                                     GdkTextFont          *font);
GDK_TEXT_AVAILABLE_IN_ALL
int                     gdk_text_fontset_simple_size   (GdkTextFontsetSimple *fontset);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextFontsetSimple, g_object_unref)

G_END_DECLS

#endif /* __PANGO_FONTSET_SIMPLE_H__ */
