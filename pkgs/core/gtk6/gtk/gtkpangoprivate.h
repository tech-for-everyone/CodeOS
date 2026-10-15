/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#pragma once

#include <glib.h>
#include <pango/pangocairo.h>
#include "gtkbuildable.h"
#include "gtkaccessibletext.h"

G_BEGIN_DECLS

GdkTextAttrList *_gtk_pango_attr_list_merge (GdkTextAttrList *into,
                                           GdkTextAttrList *from) G_GNUC_WARN_UNUSED_RESULT;

typedef struct {
  GtkBuilder    *builder;
  GObject       *object;
  GdkTextAttrList *attrs;
} GtkTextAttributeParserData;

void
gtk_text_attribute_start_element (GtkBuildableParseContext  *context,
                                   const char                *element_name,
                                   const char               **names,
                                   const char               **values,
                                   gpointer                   user_data,
                                   GError                   **error);

const char *gdk_text_wrap_mode_to_string (GdkTextWrapMode mode);
const char *gdk_text_underline_to_string (GdkTextUnderline underline);
const char *gdk_text_overline_to_string (GdkTextOverline overline);
const char *gdk_text_stretch_to_string (GdkTextStretch stretch);
const char *gdk_text_style_to_string (GdkTextStyle style);
const char *gdk_text_variant_to_string (GdkTextVariant variant);
const char *gdk_text_align_to_string (GdkTextAlignment align);

void gtk_text_get_font_attributes (GdkTextFontDescription   *font,
                                    char                 ***attribute_names,
                                    char                 ***attribute_values);
void gtk_text_get_default_attributes (GdkTextLayout     *layout,
                                       char          ***attribute_names,
                                       char          ***attribute_values);
void gtk_text_get_run_attributes     (GdkTextLayout     *layout,
                                       unsigned int     offset,
                                       char          ***attribute_names,
                                       char          ***attribute_values,
                                       unsigned int    *start_offset,
                                       unsigned int    *end_offset);

char *gtk_text_get_string_at   (GdkTextLayout                  *layout,
                                 unsigned int                  offset,
                                 GtkAccessibleTextGranularity  granularity,
                                 unsigned int                 *start_offset,
                                 unsigned int                 *end_offset);

gboolean gtk_text_glyph_item_has_color_glyphs (GdkTextGlyphItem *item);
gboolean gtk_text_layout_has_color_glyphs     (GdkTextLayout    *layout);

G_END_DECLS
