/* GdkText
 * pangocairo.h:
 *
 * Copyright (C) 1999, 2004 Red Hat, Inc.
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

#ifndef __PANGOCAIRO_H__
#define __PANGOCAIRO_H__

#include <pango/pango.h>
#include <gdk/gdkpaint.h>

G_BEGIN_DECLS

/**
 * GdkTextCairoFont:
 *
 * `GdkTextCairoFont` is an interface exported by fonts for
 * use with Cairo.
 *
 * The actual type of the font will depend on the particular
 * font technology Cairo was compiled to use.
 *
 * Since: 1.18
 **/
typedef struct _PangoCairoFont      GdkTextCairoFont;

/* This is a hack because GdkTextCairo is hijacking the GdkText namespace, but
 * consumers of the GdkTextCairo API expect these symbols to live under the
 * GdkTextCairo namespace.
 */
#ifdef __GI_SCANNER__
#define GDK_TEXT_CAIRO_TYPE_FONT           (gdk_text_cairo_font_get_type())
#define GDK_TEXT_CAIRO_FONT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDK_TEXT_CAIRO_TYPE_FONT, GdkTextCairoFont))
#define GDK_TEXT_CAIRO_IS_FONT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDK_TEXT_CAIRO_TYPE_FONT))
#else
#define GDK_TEXT_TYPE_CAIRO_FONT           (gdk_text_cairo_font_get_type ())
#define GDK_TEXT_CAIRO_FONT(object)        (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_CAIRO_FONT, GdkTextCairoFont))
#define GDK_TEXT_IS_CAIRO_FONT(object)     (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_CAIRO_FONT))
#endif

/**
 * GdkTextCairoFontMap:
 *
 * `GdkTextCairoFontMap` is an interface exported by font maps for
 * use with Cairo.
 *
 * The actual type of the font map will depend on the particular
 * font technology Cairo was compiled to use.
 *
 * Since: 1.10
 **/
typedef struct _PangoCairoFontMap        GdkTextCairoFontMap;

#ifdef __GI_SCANNER__
#define GDK_TEXT_CAIRO_TYPE_FONT_MAP       (gdk_text_cairo_font_map_get_type())
#define GDK_TEXT_CAIRO_FONT_MAP(obj)       (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDK_TEXT_CAIRO_TYPE_FONT_MAP, GdkTextCairoFontMap))
#define GDK_TEXT_CAIRO_IS_FONT_MAP(obj)    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDK_TEXT_CAIRO_TYPE_FONT_MAP))
#else
#define GDK_TEXT_TYPE_CAIRO_FONT_MAP       (gdk_text_cairo_font_map_get_type ())
#define GDK_TEXT_CAIRO_FONT_MAP(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_CAIRO_FONT_MAP, GdkTextCairoFontMap))
#define GDK_TEXT_IS_CAIRO_FONT_MAP(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_CAIRO_FONT_MAP))
#endif

/**
 * GdkTextCairoShapeRendererFunc:
 * @cr: a Cairo context with current point set to where the shape should
 * be rendered
 * @attr: the %GDK_TEXT_ATTR_SHAPE to render
 * @do_path: whether only the shape path should be appended to current
 * path of @cr and no filling/stroking done.  This will be set
 * to %TRUE when called from gdk_text_cairo_layout_path() and
 * gdk_text_cairo_layout_line_path() rendering functions.
 * @data: (closure): user data passed to gdk_text_cairo_context_set_shape_renderer()
 *
 * Function type for rendering attributes of type %GDK_TEXT_ATTR_SHAPE
 * with GdkText's Cairo renderer.
 */
typedef void (* GdkTextCairoShapeRendererFunc) (GdkPainter        *cr,
					      GdkTextAttrShape *attr,
					      gboolean        do_path,
					      gpointer        data);

/*
 * GdkTextCairoFontMap
 */
GDK_TEXT_AVAILABLE_IN_1_10
GType         gdk_text_cairo_font_map_get_type          (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_10
GdkTextFontMap *gdk_text_cairo_font_map_new               (void);
GDK_TEXT_AVAILABLE_IN_1_18
GdkTextFontMap *gdk_text_cairo_font_map_new_for_font_type (GdkPaintFontType fonttype);
GDK_TEXT_AVAILABLE_IN_1_10
GdkTextFontMap *gdk_text_cairo_font_map_get_default       (void);
GDK_TEXT_AVAILABLE_IN_1_22
void          gdk_text_cairo_font_map_set_default       (GdkTextCairoFontMap *fontmap);
GDK_TEXT_AVAILABLE_IN_1_18
GdkPaintFontType gdk_text_cairo_font_map_get_font_type (GdkTextCairoFontMap *fontmap);

GDK_TEXT_AVAILABLE_IN_1_10
void          gdk_text_cairo_font_map_set_resolution (GdkTextCairoFontMap *fontmap,
						   double             dpi);
GDK_TEXT_AVAILABLE_IN_1_10
double        gdk_text_cairo_font_map_get_resolution (GdkTextCairoFontMap *fontmap);
#ifndef GDK_TEXT_DISABLE_DEPRECATED
GDK_TEXT_DEPRECATED_IN_1_22_FOR(gdk_text_font_map_create_context)
GdkTextContext *gdk_text_cairo_font_map_create_context (GdkTextCairoFontMap *fontmap);
#endif

/*
 * GdkTextCairoFont
 */
GDK_TEXT_AVAILABLE_IN_1_18
GType         gdk_text_cairo_font_get_type               (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_18
GdkPaintScaledFont *gdk_text_cairo_font_get_scaled_font (GdkTextCairoFont *font);

/* Update a GdkText context for the current state of a cairo context
 */
GDK_TEXT_AVAILABLE_IN_1_10
void         gdk_text_cairo_update_context (GdkPainter      *cr,
					 GdkTextContext *context);

GDK_TEXT_AVAILABLE_IN_1_10
void                        gdk_text_cairo_context_set_font_options (GdkTextContext               *context,
								  const GdkPaintFontOptions *options);
GDK_TEXT_AVAILABLE_IN_1_10
const GdkPaintFontOptions *gdk_text_cairo_context_get_font_options (GdkTextContext               *context);

GDK_TEXT_AVAILABLE_IN_1_10
void               gdk_text_cairo_context_set_resolution     (GdkTextContext       *context,
							   double              dpi);
GDK_TEXT_AVAILABLE_IN_1_10
double             gdk_text_cairo_context_get_resolution     (GdkTextContext       *context);

GDK_TEXT_AVAILABLE_IN_1_18
void                        gdk_text_cairo_context_set_shape_renderer (GdkTextContext                *context,
								    GdkTextCairoShapeRendererFunc  func,
								    gpointer                     data,
								    GDestroyNotify               dnotify);
GDK_TEXT_AVAILABLE_IN_1_18
GdkTextCairoShapeRendererFunc gdk_text_cairo_context_get_shape_renderer (GdkTextContext                *context,
								    gpointer                    *data);

/* Convenience
 */
GDK_TEXT_AVAILABLE_IN_1_22
GdkTextContext *gdk_text_cairo_create_context (GdkPainter   *cr);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLayout *gdk_text_cairo_create_layout (GdkPainter     *cr);
GDK_TEXT_AVAILABLE_IN_1_10
void         gdk_text_cairo_update_layout (GdkPainter     *cr,
					GdkTextLayout *layout);

/*
 * Rendering
 */
GDK_TEXT_AVAILABLE_IN_1_10
void gdk_text_cairo_show_glyph_string (GdkPainter          *cr,
				    GdkTextFont        *font,
				    GdkTextGlyphString *glyphs);
GDK_TEXT_AVAILABLE_IN_1_22
void gdk_text_cairo_show_glyph_item   (GdkPainter          *cr,
				    const char       *text,
				    GdkTextGlyphItem   *glyph_item);
GDK_TEXT_AVAILABLE_IN_1_10
void gdk_text_cairo_show_layout_line  (GdkPainter          *cr,
				    GdkTextLayoutLine  *line);
GDK_TEXT_AVAILABLE_IN_1_10
void gdk_text_cairo_show_layout       (GdkPainter          *cr,
				    GdkTextLayout      *layout);

GDK_TEXT_AVAILABLE_IN_1_14
void gdk_text_cairo_show_error_underline (GdkPainter       *cr,
				       double         x,
				       double         y,
				       double         width,
				       double         height);

/*
 * Rendering to a path
 */
GDK_TEXT_AVAILABLE_IN_1_10
void gdk_text_cairo_glyph_string_path (GdkPainter          *cr,
				    GdkTextFont        *font,
				    GdkTextGlyphString *glyphs);
GDK_TEXT_AVAILABLE_IN_1_10
void gdk_text_cairo_layout_line_path  (GdkPainter          *cr,
				    GdkTextLayoutLine  *line);
GDK_TEXT_AVAILABLE_IN_1_10
void gdk_text_cairo_layout_path       (GdkPainter          *cr,
				    GdkTextLayout      *layout);
GDK_TEXT_AVAILABLE_IN_1_58
void gdk_text_cairo_layout_path_for_components (GdkPainter              *cr,
                                             GdkTextLayout          *layout,
                                             GdkTextRenderComponent  components);

GDK_TEXT_AVAILABLE_IN_1_14
void gdk_text_cairo_error_underline_path (GdkPainter       *cr,
				       double         x,
				       double         y,
				       double         width,
				       double         height);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextCairoFont, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextCairoFontMap, g_object_unref)

G_END_DECLS

#endif /* __PANGOCAIRO_H__ */
