/* GdkText
 * pango-font.h: Font handling
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

#ifndef __PANGO_FONTMAP_H__
#define __PANGO_FONTMAP_H__

#include <pango/pango-types.h>
#include <pango/pango-font.h>
#include <pango/pango-fontset.h>

G_BEGIN_DECLS

#define GDK_TEXT_TYPE_FONT_MAP              (gdk_text_font_map_get_type ())
#define GDK_TEXT_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FONT_MAP, GdkTextFontMap))
#define GDK_TEXT_IS_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FONT_MAP))
#define GDK_TEXT_FONT_MAP_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_FONT_MAP, GdkTextFontMapClass))
#define GDK_TEXT_IS_FONT_MAP_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_FONT_MAP))
#define GDK_TEXT_FONT_MAP_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_FONT_MAP, GdkTextFontMapClass))

typedef struct _PangoFontMapClass GdkTextFontMapClass;

/**
 * GdkTextFontMap:
 *
 * A `GdkTextFontMap` represents the set of fonts available for a
 * particular rendering system.
 *
 * This is a virtual object with implementations being specific to
 * particular rendering systems.
 */
struct _PangoFontMap
{
  GObject parent_instance;
};

/**
 * GdkTextFontMapClass:
 * @parent_class: parent `GObjectClass`
 * @load_font: a function to load a font with a given description. See
 * gdk_text_font_map_load_font().
 * @list_families: A function to list available font families. See
 * gdk_text_font_map_list_families().
 * @load_fontset: a function to load a fontset with a given given description
 * suitable for a particular language. See gdk_text_font_map_load_fontset().
 * @shape_engine_type: the type of rendering-system-dependent engines that
 * can handle fonts of this fonts loaded with this fontmap.
 * @get_serial: a function to get the serial number of the fontmap.
 * See gdk_text_font_map_get_serial().
 * @changed: See gdk_text_font_map_changed()
 *
 * The `GdkTextFontMapClass` structure holds the virtual functions for
 * a particular `GdkTextFontMap` implementation.
 */
struct _PangoFontMapClass
{
  GObjectClass parent_class;

  /*< public >*/

  GdkTextFont *   (*load_font)     (GdkTextFontMap               *fontmap,
                                  GdkTextContext               *context,
                                  const GdkTextFontDescription *desc);
  void          (*list_families) (GdkTextFontMap               *fontmap,
                                  GdkTextFontFamily          ***families,
                                  int                        *n_families);
  GdkTextFontset *(*load_fontset)  (GdkTextFontMap               *fontmap,
                                  GdkTextContext               *context,
                                  const GdkTextFontDescription *desc,
                                  GdkTextLanguage              *language);

  const char     *shape_engine_type;

  guint         (*get_serial)    (GdkTextFontMap               *fontmap);
  void          (*changed)       (GdkTextFontMap               *fontmap);

  GdkTextFontFamily * (*get_family) (GdkTextFontMap               *fontmap,
                                   const char                 *name);

  GdkTextFontFace *   (*get_face)   (GdkTextFontMap               *fontmap,
                                   GdkTextFont                  *font);
};

GDK_TEXT_AVAILABLE_IN_ALL
GType         gdk_text_font_map_get_type       (void) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_1_22
GdkTextContext * gdk_text_font_map_create_context (GdkTextFontMap               *fontmap);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFont *   gdk_text_font_map_load_font     (GdkTextFontMap                 *fontmap,
					    GdkTextContext                 *context,
					    const GdkTextFontDescription   *desc);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontset *gdk_text_font_map_load_fontset  (GdkTextFontMap                 *fontmap,
					    GdkTextContext                 *context,
					    const GdkTextFontDescription   *desc,
					    GdkTextLanguage                *language);
GDK_TEXT_AVAILABLE_IN_ALL
void          gdk_text_font_map_list_families (GdkTextFontMap                 *fontmap,
					    GdkTextFontFamily            ***families,
					    int                          *n_families);
GDK_TEXT_AVAILABLE_IN_1_32
guint         gdk_text_font_map_get_serial    (GdkTextFontMap                 *fontmap);
GDK_TEXT_AVAILABLE_IN_1_34
void          gdk_text_font_map_changed       (GdkTextFontMap                 *fontmap);

GDK_TEXT_AVAILABLE_IN_1_46
GdkTextFontFamily *gdk_text_font_map_get_family (GdkTextFontMap                 *fontmap,
                                            const char                   *name);

GDK_TEXT_AVAILABLE_IN_1_52
GdkTextFont *   gdk_text_font_map_reload_font   (GdkTextFontMap                 *fontmap,
                                            GdkTextFont                    *font,
                                            double                        scale,
                                            GdkTextContext                 *context,
                                            const char                   *variations);

GDK_TEXT_AVAILABLE_IN_1_56
gboolean      gdk_text_font_map_add_font_file (GdkTextFontMap                 *fontmap,
                                            const char                   *filename,
                                            GError                      **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextFontMap, g_object_unref)

G_END_DECLS

#endif /* __PANGO_FONTMAP_H__ */
