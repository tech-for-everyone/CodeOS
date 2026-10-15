/* GdkText
 * pangoxft.h:
 *
 * Copyright (C) 1999 Red Hat Software
 * Copyright (C) 2000 SuSE Linux Ltd
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

#ifndef __PANGOXFT_H__
#define __PANGOXFT_H__

#include <pango/pangofc-fontmap.h>
#include <pango/pango-context.h>
#include <pango/pango-ot.h>
#include <pango/pangofc-font.h>
#include <pango/pango-layout.h>
#include <pango/pangoxft-render.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__

/**
 * GDK_TEXT_RENDER_TYPE_XFT: (skip)
 *
 * A string constant that was used to identify shape engines that work
 * with the Xft backend. See %GDK_TEXT_RENDER_TYPE_FC for the replacement.
 */
#ifndef GDK_TEXT_DISABLE_DEPRECATED
#define GDK_TEXT_RENDER_TYPE_XFT "GdkTextRenderXft"
#endif

#endif /* __GI_SCANNER__ */

/**
 * GdkTextXftFontMap:
 *
 * `GdkTextXftFontMap` is an implementation of `GdkTextFcFontMap` suitable for
 * the Xft library as the renderer.  It is used in to create fonts of
 * type `GdkTextXftFont`.
 */

/* This is a hack needed because GdkTextXft hijacks the GdkText namespace
 * for the identifiers, but consumers expect these symbols to be under
 * the GdkTextXft namespace.
 */
#ifdef __GI_SCANNER__
#define GDK_TEXT_XFT_TYPE_FONT_MAP         (gdk_text_xft_font_map_get_type())
#define GDK_TEXT_XFT_FONT_MAP(obj)         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDK_TEXT_XFT_TYPE_FONT_MAP, GdkTextXftFontMap))
#define GDK_TEXT_XFT_IS_FONT_MAP(obj)      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDK_TEXT_XFT_TYPE_FONT_MAP))
#else
#define GDK_TEXT_TYPE_XFT_FONT_MAP         (gdk_text_xft_font_map_get_type ())
#define GDK_TEXT_XFT_FONT_MAP(object)      (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_XFT_FONT_MAP, GdkTextXftFontMap))
#define GDK_TEXT_XFT_IS_FONT_MAP(object)   (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_XFT_FONT_MAP))
#endif

typedef struct _PangoXftFontMap      GdkTextXftFontMap;

/**
 * GdkTextXftFont:
 *
 * `GdkTextXftFont` is an implementation of `GdkTextFcFont` using the Xft
 * library for rendering.  It is used in conjunction with `GdkTextXftFontMap`.
 */
typedef struct _PangoXftFont    GdkTextXftFont;

/**
 * GdkTextXftSubstituteFunc:
 * @pattern: the FcPattern to tweak.
 * @data: user data.
 *
 * Function type for doing final config tweaking on prepared FcPatterns.
 */
typedef void (*GdkTextXftSubstituteFunc) (FcPattern *pattern,
				        gpointer   data);

/* Calls for applications
 */
GDK_TEXT_AVAILABLE_IN_1_2
GdkTextFontMap *gdk_text_xft_get_font_map     (Display *display,
					  int      screen);
#ifndef GDK_TEXT_DISABLE_DEPRECATED
GDK_TEXT_DEPRECATED
GdkTextContext *gdk_text_xft_get_context      (Display *display,
					  int      screen);
#endif
GDK_TEXT_AVAILABLE_IN_1_2
void          gdk_text_xft_shutdown_display (Display *display,
					  int      screen);

#ifndef GDK_TEXT_DISABLE_DEPRECATED
GDK_TEXT_DEPRECATED_IN_1_48_FOR(gdk_text_fc_font_map_set_default_substitute)
void gdk_text_xft_set_default_substitute (Display                *display,
				       int                     screen,
				       GdkTextXftSubstituteFunc  func,
				       gpointer                data,
				       GDestroyNotify          notify);
GDK_TEXT_DEPRECATED_IN_1_48_FOR(gdk_text_fc_font_map_substitute_changed)
void gdk_text_xft_substitute_changed     (Display                *display,
				       int                     screen);
#endif

GDK_TEXT_AVAILABLE_IN_ALL
GType gdk_text_xft_font_map_get_type (void) G_GNUC_CONST;

#ifdef __GI_SCANNER__
#define GDK_TEXT_XFT_TYPE_FONT              (gdk_text_xft_font_get_type ())
#define GDK_TEXT_XFT_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_XFT_TYPE_FONT, GdkTextXftFont))
#define GDK_TEXT_XFT_IS_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_XFT_TYPE_FONT))
#else
#define GDK_TEXT_TYPE_XFT_FONT              (gdk_text_xft_font_get_type ())
#define GDK_TEXT_XFT_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_XFT_FONT, GdkTextXftFont))
#define GDK_TEXT_XFT_IS_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_XFT_FONT))
#endif

GDK_TEXT_AVAILABLE_IN_ALL
GType      gdk_text_xft_font_get_type (void) G_GNUC_CONST;

/* For shape engines
 */

#ifndef GDK_TEXT_DISABLE_DEPRECATED

GDK_TEXT_DEPRECATED
XftFont *     gdk_text_xft_font_get_font          (GdkTextFont *font);
GDK_TEXT_DEPRECATED
Display *     gdk_text_xft_font_get_display       (GdkTextFont *font);
GDK_TEXT_DEPRECATED_FOR(gdk_text_fc_font_lock_face)
FT_Face       gdk_text_xft_font_lock_face         (GdkTextFont *font);
GDK_TEXT_DEPRECATED_FOR(gdk_text_fc_font_unlock_face)
void	      gdk_text_xft_font_unlock_face       (GdkTextFont *font);
GDK_TEXT_DEPRECATED_FOR(gdk_text_fc_font_get_glyph)
guint	      gdk_text_xft_font_get_glyph	       (GdkTextFont *font,
						gunichar   wc);
GDK_TEXT_DEPRECATED_FOR(gdk_text_fc_font_has_char)
gboolean      gdk_text_xft_font_has_char          (GdkTextFont *font,
						gunichar   wc);
GDK_TEXT_DEPRECATED_FOR(GDK_TEXT_GET_UNKNOWN_GLYPH)
GdkTextGlyph    gdk_text_xft_font_get_unknown_glyph (GdkTextFont *font,
						gunichar   wc);
#endif /* GDK_TEXT_DISABLE_DEPRECATED */

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextXftFont, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextXftFontMap, g_object_unref)

G_END_DECLS

#endif /* __PANGOXFT_H__ */
