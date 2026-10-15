/* GdkText
 * pango-tabs.h: Tab-related stuff
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

#ifndef __PANGO_TABS_H__
#define __PANGO_TABS_H__

#include <pango/pango-types.h>

G_BEGIN_DECLS

typedef struct _PangoTabArray GdkTextTabArray;

/**
 * GdkTextTabAlign:
 * @GDK_TEXT_TAB_LEFT: the text appears to the right of the tab stop position
 * @GDK_TEXT_TAB_RIGHT: the text appears to the left of the tab stop position
 *   until the available space is filled. Since: 1.50
 * @GDK_TEXT_TAB_CENTER: the text is centered at the tab stop position
 *   until the available space is filled. Since: 1.50
 * @GDK_TEXT_TAB_DECIMAL: text before the first occurrence of the decimal point
 *   character appears to the left of the tab stop position (until the available
 *   space is filled), the rest to the right. Since: 1.50
 *
 * `GdkTextTabAlign` specifies where the text appears relative to the tab stop
 * position.
 */
typedef enum
{
  GDK_TEXT_TAB_LEFT,
  GDK_TEXT_TAB_RIGHT,
  GDK_TEXT_TAB_CENTER,
  GDK_TEXT_TAB_DECIMAL
} GdkTextTabAlign;

#define GDK_TEXT_TYPE_TAB_ARRAY (gdk_text_tab_array_get_type ())

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextTabArray  *gdk_text_tab_array_new                 (gint           initial_size,
						     gboolean       positions_in_pixels);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextTabArray  *gdk_text_tab_array_new_with_positions  (gint           size,
						     gboolean       positions_in_pixels,
						     GdkTextTabAlign  first_alignment,
						     gint           first_position,
						     ...);
GDK_TEXT_AVAILABLE_IN_ALL
GType           gdk_text_tab_array_get_type            (void) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextTabArray  *gdk_text_tab_array_copy                (GdkTextTabArray *src);
GDK_TEXT_AVAILABLE_IN_ALL
void            gdk_text_tab_array_free                (GdkTextTabArray *tab_array);
GDK_TEXT_AVAILABLE_IN_ALL
gint            gdk_text_tab_array_get_size            (GdkTextTabArray *tab_array);
GDK_TEXT_AVAILABLE_IN_ALL
void            gdk_text_tab_array_resize              (GdkTextTabArray *tab_array,
						     gint           new_size);
GDK_TEXT_AVAILABLE_IN_ALL
void            gdk_text_tab_array_set_tab             (GdkTextTabArray *tab_array,
						     gint           tab_index,
						     GdkTextTabAlign  alignment,
						     gint           location);
GDK_TEXT_AVAILABLE_IN_ALL
void            gdk_text_tab_array_get_tab             (GdkTextTabArray *tab_array,
						     gint           tab_index,
						     GdkTextTabAlign *alignment,
						     gint          *location);
GDK_TEXT_AVAILABLE_IN_ALL
void            gdk_text_tab_array_get_tabs            (GdkTextTabArray *tab_array,
						     GdkTextTabAlign **alignments,
						     gint          **locations);

GDK_TEXT_AVAILABLE_IN_ALL
gboolean        gdk_text_tab_array_get_positions_in_pixels (GdkTextTabArray *tab_array);

GDK_TEXT_AVAILABLE_IN_1_50
void            gdk_text_tab_array_set_positions_in_pixels (GdkTextTabArray *tab_array,
                                                         gboolean       positions_in_pixels);

GDK_TEXT_AVAILABLE_IN_1_50
char *          gdk_text_tab_array_to_string           (GdkTextTabArray *tab_array);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextTabArray * gdk_text_tab_array_from_string         (const char    *text);

GDK_TEXT_AVAILABLE_IN_1_50
void            gdk_text_tab_array_set_decimal_point   (GdkTextTabArray *tab_array,
                                                     int            tab_index,
                                                     gunichar       decimal_point);
GDK_TEXT_AVAILABLE_IN_1_50
gunichar        gdk_text_tab_array_get_decimal_point   (GdkTextTabArray *tab_array,
                                                     int            tab_index);

GDK_TEXT_AVAILABLE_IN_1_50
void            gdk_text_tab_array_sort                (GdkTextTabArray *tab_array);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextTabArray, gdk_text_tab_array_free)

G_END_DECLS

#endif /* __PANGO_TABS_H__ */
