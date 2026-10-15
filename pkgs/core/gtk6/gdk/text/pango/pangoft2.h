/* GdkText
 * pangoft2.h:
 *
 * Copyright (C) 1999 Red Hat Software
 * Copyright (C) 2000 Tor Lillqvist
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

#ifndef __PANGOFT2_H__
#define __PANGOFT2_H__

#include <fontconfig/fontconfig.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <pango/pangofc-fontmap.h>
#include <pango/pango-layout.h>
#include <pango/pangofc-font.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__

#ifndef GDK_TEXT_DISABLE_DEPRECATED
/**
 * GDK_TEXT_RENDER_TYPE_FT2: (skip)
 *
 * A string constant that was used to identify shape engines that work
 * with the FreeType backend. See %GDK_TEXT_RENDER_TYPE_FC for the replacement.
 */
#define GDK_TEXT_RENDER_TYPE_FT2 "GdkTextRenderFT2"
#endif

#endif /* __GI_SCANNER__ */

#ifdef __GI_SCANNER__
#define GDK_TEXT_FT2_TYPE_FONT_MAP              (gdk_text_ft2_font_map_get_type ())
#define GDK_TEXT_FT2_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_FT2_TYPE_FONT_MAP, GdkTextFT2FontMap))
#define GDK_TEXT_FT2_IS_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_FT2_TYPE_FONT_MAP))
#else
#define GDK_TEXT_TYPE_FT2_FONT_MAP              (gdk_text_ft2_font_map_get_type ())
#define GDK_TEXT_FT2_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FT2_FONT_MAP, GdkTextFT2FontMap))
#define GDK_TEXT_FT2_IS_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FT2_FONT_MAP))
#endif

typedef struct _PangoFT2FontMap      GdkTextFT2FontMap;

/**
 * GdkTextFT2SubstituteFunc:
 * @pattern: the FcPattern to tweak.
 * @data: user data.
 *
 * Function type for doing final config tweaking on prepared FcPatterns.
 */
typedef void (*GdkTextFT2SubstituteFunc) (FcPattern *pattern,
				        gpointer   data);

/* Calls for applications */

GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_ft2_render             (FT_Bitmap         *bitmap,
				   GdkTextFont         *font,
				   GdkTextGlyphString  *glyphs,
				   gint               x,
				   gint               y);
GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_ft2_render_transformed (FT_Bitmap         *bitmap,
				   const GdkTextMatrix *matrix,
				   GdkTextFont         *font,
				   GdkTextGlyphString  *glyphs,
				   int                x,
				   int                y);

GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_ft2_render_layout_line          (FT_Bitmap        *bitmap,
					    GdkTextLayoutLine  *line,
					    int               x,
					    int               y);
GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_ft2_render_layout_line_subpixel (FT_Bitmap        *bitmap,
					    GdkTextLayoutLine  *line,
					    int               x,
					    int               y);
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_ft2_render_layout               (FT_Bitmap        *bitmap,
					    GdkTextLayout      *layout,
					    int               x,
					    int               y);
GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_ft2_render_layout_subpixel      (FT_Bitmap        *bitmap,
					    GdkTextLayout      *layout,
					    int               x,
					    int               y);

GDK_TEXT_AVAILABLE_IN_ALL
GType gdk_text_ft2_font_map_get_type (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_2
GdkTextFontMap *gdk_text_ft2_font_map_new                    (void);
GDK_TEXT_AVAILABLE_IN_1_2
void          gdk_text_ft2_font_map_set_resolution         (GdkTextFT2FontMap        *fontmap,
							 double                  dpi_x,
							 double                  dpi_y);
#ifndef GDK_TEXT_DISABLE_DEPRECATED
GDK_TEXT_DEPRECATED_IN_1_48_FOR(gdk_text_fc_font_map_set_default_substitute)
void          gdk_text_ft2_font_map_set_default_substitute (GdkTextFT2FontMap        *fontmap,
							 GdkTextFT2SubstituteFunc  func,
							 gpointer                data,
							 GDestroyNotify          notify);
GDK_TEXT_DEPRECATED_IN_1_48_FOR(gdk_text_fc_font_map_substitute_changed)
void          gdk_text_ft2_font_map_substitute_changed     (GdkTextFT2FontMap         *fontmap);
GDK_TEXT_DEPRECATED_IN_1_22_FOR(gdk_text_font_map_create_context)
GdkTextContext *gdk_text_ft2_font_map_create_context         (GdkTextFT2FontMap         *fontmap);
#endif


/* API for rendering modules
 */
#ifndef GDK_TEXT_DISABLE_DEPRECATED
GDK_TEXT_DEPRECATED_FOR(gdk_text_font_map_create_context)
GdkTextContext      *gdk_text_ft2_get_context          (double dpi_x,
						   double dpi_y);
GDK_TEXT_DEPRECATED_FOR(gdk_text_ft2_font_map_new)
GdkTextFontMap      *gdk_text_ft2_font_map_for_display (void);
GDK_TEXT_DEPRECATED
void               gdk_text_ft2_shutdown_display     (void);

GDK_TEXT_DEPRECATED_FOR(GDK_TEXT_GET_UNKNOWN_GLYPH)
GdkTextGlyph     gdk_text_ft2_get_unknown_glyph (GdkTextFont       *font);
GDK_TEXT_DEPRECATED_FOR(gdk_text_fc_font_kern_glyphs)
int            gdk_text_ft2_font_get_kerning  (GdkTextFont       *font,
					    GdkTextGlyph       left,
					    GdkTextGlyph       right);
GDK_TEXT_DEPRECATED_FOR(gdk_text_fc_font_lock_face)
FT_Face        gdk_text_ft2_font_get_face     (GdkTextFont       *font);
GDK_TEXT_DEPRECATED_FOR(gdk_text_font_get_coverage)
GdkTextCoverage *gdk_text_ft2_font_get_coverage (GdkTextFont       *font,
					    GdkTextLanguage   *language);
#endif /* GDK_TEXT_DISABLE_DEPRECATED */

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextFT2FontMap, g_object_unref)

G_END_DECLS

#endif /* __PANGOFT2_H__ */
