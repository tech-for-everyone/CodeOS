/* GdkText
 * pangofc-font.h: Base fontmap type for fontconfig-based backends
 *
 * Copyright (C) 2003 Red Hat Software
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

#ifndef __PANGO_FC_FONT_H__
#define __PANGO_FC_FONT_H__

#include <pango/pango-glyph.h>
#include <pango/pango-font.h>
#include <pango/pango-glyph.h>

/* FreeType has undefined macros in its header */
#ifdef GDK_TEXT_COMPILATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#ifdef GDK_TEXT_COMPILATION
#pragma GCC diagnostic pop
#endif

G_BEGIN_DECLS

#ifdef __GI_SCANNER__
#define GDK_TEXT_FC_TYPE_FONT              (gdk_text_fc_font_get_type ())
#define GDK_TEXT_FC_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_FC_TYPE_FONT, GdkTextFcFont))
#define GDK_TEXT_FC_IS_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_FC_TYPE_FONT))
#else
#define GDK_TEXT_TYPE_FC_FONT              (gdk_text_fc_font_get_type ())
#define GDK_TEXT_FC_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FC_FONT, GdkTextFcFont))
#define GDK_TEXT_IS_FC_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FC_FONT))
#endif

typedef struct _PangoFcFont      GdkTextFcFont;
typedef struct _PangoFcFontClass GdkTextFcFontClass;

#ifndef GDK_TEXT_DISABLE_DEPRECATED

/**
 * GdkTextFcFont:
 *
 * `GdkTextFcFont` is a base class for font implementations
 * using the Fontconfig and FreeType libraries.
 *
 * It is used in onjunction with [class@GdkTextFc.FontMap].
 * When deriving from this class, you need to implement all
 * of its virtual functions other than shutdown() along with
 * the get_glyph_extents() virtual function from `GdkTextFont`.
 */
struct _PangoFcFont
{
  GdkTextFont parent_instance;

  FcPattern *font_pattern;          /* fully resolved pattern */
  GdkTextFontMap *fontmap;            /* associated map (no strong reference is held,
                                     * but a g_object_add_weak_pointer() guards it) */
  gpointer priv;                    /* used internally */
  GdkTextMatrix matrix;               /* unused */
  GdkTextFontDescription *description;

  GSList *metrics_by_lang;

  guint is_hinted : 1;
  guint is_transformed : 1;
};

#endif /* GDK_TEXT_DISABLE_DEPRECATED */

GDK_TEXT_AVAILABLE_IN_ALL
GType      gdk_text_fc_font_get_type (void) G_GNUC_CONST;

GDK_TEXT_DEPRECATED_IN_1_44
gboolean   gdk_text_fc_font_has_char          (GdkTextFcFont      *font,
                                            gunichar          wc);
GDK_TEXT_AVAILABLE_IN_1_4
guint      gdk_text_fc_font_get_glyph         (GdkTextFcFont      *font,
                                            gunichar          wc);

GDK_TEXT_DEPRECATED_IN_1_50_FOR(gdk_text_font_get_language)
GdkTextLanguage **
           gdk_text_fc_font_get_languages     (GdkTextFcFont      *font);

GDK_TEXT_AVAILABLE_IN_1_48
FcPattern *gdk_text_fc_font_get_pattern       (GdkTextFcFont      *font);

GDK_TEXT_DEPRECATED_FOR(GDK_TEXT_GET_UNKNOWN_GLYPH)
GdkTextGlyph gdk_text_fc_font_get_unknown_glyph (GdkTextFcFont      *font,
                                            gunichar          wc);
GDK_TEXT_DEPRECATED_IN_1_32
void       gdk_text_fc_font_kern_glyphs       (GdkTextFcFont      *font,
                                            GdkTextGlyphString *glyphs);

GDK_TEXT_DEPRECATED_IN_1_44_FOR(gdk_text_font_get_hb_font)
FT_Face    gdk_text_fc_font_lock_face         (GdkTextFcFont      *font);
GDK_TEXT_DEPRECATED_IN_1_44_FOR(gdk_text_font_get_hb_font)
void       gdk_text_fc_font_unlock_face       (GdkTextFcFont      *font);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextFcFont, g_object_unref)

G_END_DECLS
#endif /* __PANGO_FC_FONT_H__ */
