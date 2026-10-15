/* GdkText
 * pangoxft-render.h: Rendering routines for the Xft library
 *
 * Copyright (C) 2004 Red Hat Software
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

#ifndef __PANGOXFT_RENDER_H__
#define __PANGOXFT_RENDER_H__

#include <pango/pango-renderer.h>

G_BEGIN_DECLS

#define _XFT_NO_COMPAT_
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#if defined(XftVersion) && XftVersion >= 20000
#else
#error "must have Xft version 2 or newer"
#endif

typedef struct _PangoXftRenderer        GdkTextXftRenderer;
typedef struct _PangoXftRendererClass   GdkTextXftRendererClass;
typedef struct _PangoXftRendererPrivate GdkTextXftRendererPrivate;

#ifdef __GI_SCANNER__
#define GDK_TEXT_XFT_TYPE_RENDERER            (gdk_text_xft_renderer_get_type())
#define GDK_TEXT_XFT_RENDERER(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_XFT_TYPE_RENDERER, GdkTextXftRenderer))
#define GDK_TEXT_XFT_IS_RENDERER(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_XFT_TYPE_RENDERER))
#define GDK_TEXT_XFT_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_XFT_TYPE_RENDERER, GdkTextXftRendererClass))
#define GDK_TEXT_XFT_IS_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_XFT_TYPE_RENDERER))
#define GDK_TEXT_XFT_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_XFT_TYPE_RENDERER, GdkTextXftRendererClass))
#else
#define GDK_TEXT_TYPE_XFT_RENDERER            (gdk_text_xft_renderer_get_type())
#define GDK_TEXT_XFT_RENDERER(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_XFT_RENDERER, GdkTextXftRenderer))
#define GDK_TEXT_IS_XFT_RENDERER(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_XFT_RENDERER))
#define GDK_TEXT_XFT_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_XFT_RENDERER, GdkTextXftRendererClass))
#define GDK_TEXT_IS_XFT_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_XFT_RENDERER))
#define GDK_TEXT_XFT_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_XFT_RENDERER, GdkTextXftRendererClass))
#endif

/**
 * GdkTextXftRenderer:
 *
 * `GdkTextXftRenderer` is a subclass of `GdkTextRenderer` used for rendering
 * with GdkText's Xft backend. It can be used directly, or it can be
 * further subclassed to modify exactly how drawing of individual
 * elements occurs.
 *
 * Since: 1.8
 */
struct _PangoXftRenderer
{
  /*< private >*/
  GdkTextRenderer parent_instance;

  Display *display;
  int screen;
  XftDraw *draw;

  GdkTextXftRendererPrivate *priv;
};

/**
 * GdkTextXftRendererClass:
 * @composite_trapezoids: draw the specified trapezoids using
 *   the current color and other attributes for @part
 * @composite_glyphs: draw the specified glyphs using
 *   the current foreground color and other foreground
 *   attributes
 *
 * The class structure for `GdkTextXftRenderer`
 *
 * Since: 1.8
 */
struct _PangoXftRendererClass
{
  /*< private >*/
  GdkTextRendererClass parent_class;

  /*< public >*/
  void (*composite_trapezoids) (GdkTextXftRenderer *xftrenderer,
				GdkTextRenderPart   part,
				XTrapezoid       *trapezoids,
				int               n_trapezoids);
  void (*composite_glyphs)     (GdkTextXftRenderer *xftrenderer,
				XftFont          *xft_font,
				XftGlyphSpec     *glyphs,
				int               n_glyphs);
};

GDK_TEXT_AVAILABLE_IN_1_8
GType gdk_text_xft_renderer_get_type    (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_8
GdkTextRenderer *gdk_text_xft_renderer_new                 (Display          *display,
						       int               screen);
GDK_TEXT_AVAILABLE_IN_1_8
void           gdk_text_xft_renderer_set_draw            (GdkTextXftRenderer *xftrenderer,
						       XftDraw          *draw);
GDK_TEXT_AVAILABLE_IN_1_8
void           gdk_text_xft_renderer_set_default_color   (GdkTextXftRenderer *xftrenderer,
						       GdkTextColor       *default_color);

GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_xft_render             (XftDraw          *draw,
				   XftColor         *color,
				   GdkTextFont        *font,
				   GdkTextGlyphString *glyphs,
				   gint              x,
				   gint              y);
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_xft_picture_render     (Display          *display,
				   Picture           src_picture,
				   Picture           dest_picture,
				   GdkTextFont        *font,
				   GdkTextGlyphString *glyphs,
				   gint              x,
				   gint              y);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_xft_render_transformed (XftDraw          *draw,
				   XftColor         *color,
				   GdkTextMatrix      *matrix,
				   GdkTextFont        *font,
				   GdkTextGlyphString *glyphs,
				   int               x,
				   int               y);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_xft_render_layout_line (XftDraw          *draw,
				   XftColor         *color,
				   GdkTextLayoutLine  *line,
				   int               x,
				   int               y);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_xft_render_layout      (XftDraw          *draw,
				   XftColor         *color,
				   GdkTextLayout      *layout,
				   int               x,
				   int               y);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextXftRenderer, g_object_unref)

G_END_DECLS

#endif /* __PANGOXFT_RENDER_H__ */

