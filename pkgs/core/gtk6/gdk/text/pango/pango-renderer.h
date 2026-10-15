/* GdkText
 * pango-renderer.h: Base class for rendering
 *
 * Copyright (C) 2004, Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __PANGO_RENDERER_H_
#define __PANGO_RENDERER_H_

#include <pango/pango-layout.h>

G_BEGIN_DECLS

#define GDK_TEXT_TYPE_RENDERER            (gdk_text_renderer_get_type())
#define GDK_TEXT_RENDERER(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_RENDERER, GdkTextRenderer))
#define GDK_TEXT_IS_RENDERER(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_RENDERER))
#define GDK_TEXT_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_RENDERER, GdkTextRendererClass))
#define GDK_TEXT_IS_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_RENDERER))
#define GDK_TEXT_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_RENDERER, GdkTextRendererClass))

typedef struct _PangoRenderer        GdkTextRenderer;
typedef struct _PangoRendererClass   GdkTextRendererClass;
typedef struct _PangoRendererPrivate GdkTextRendererPrivate;

/**
 * GdkTextRenderPart:
 * @GDK_TEXT_RENDER_PART_FOREGROUND: the text itself
 * @GDK_TEXT_RENDER_PART_BACKGROUND: the area behind the text
 * @GDK_TEXT_RENDER_PART_UNDERLINE: underlines
 * @GDK_TEXT_RENDER_PART_STRIKETHROUGH: strikethrough lines
 * @GDK_TEXT_RENDER_PART_OVERLINE: overlines
 *
 * `GdkTextRenderPart` defines different items to render for such
 * purposes as setting colors.
 *
 * Since: 1.8
 **/
/* When extending, note N_RENDER_PARTS #define in pango-renderer.c */
typedef enum
{
  GDK_TEXT_RENDER_PART_FOREGROUND,
  GDK_TEXT_RENDER_PART_BACKGROUND,
  GDK_TEXT_RENDER_PART_UNDERLINE,
  GDK_TEXT_RENDER_PART_STRIKETHROUGH,
  GDK_TEXT_RENDER_PART_OVERLINE,
} GdkTextRenderPart;

/**
 * GdkTextRenderer:
 * @matrix: (nullable): the current transformation matrix for
 *   the Renderer; may be %NULL, which should be treated the
 *   same as the identity matrix.
 *
 * `GdkTextRenderer` is a base class for objects that can render text
 * provided as `GdkTextGlyphString` or `GdkTextLayout`.
 *
 * By subclassing `GdkTextRenderer` and overriding operations such as
 * @draw_glyphs and @draw_rectangle, renderers for particular font
 * backends and destinations can be created.
 *
 * Since: 1.8
 */
struct _PangoRenderer
{
  /*< private >*/
  GObject parent_instance;

  GdkTextUnderline underline;
  gboolean strikethrough;
  int active_count;

  /*< public >*/
  GdkTextMatrix *matrix;          /* May be NULL */

  /*< private >*/
  GdkTextRendererPrivate *priv;
};

/**
 * GdkTextRendererClass:
 * @draw_glyphs: draws a `GdkTextGlyphString`
 * @draw_rectangle: draws a rectangle
 * @draw_error_underline: draws a squiggly line that approximately
 * covers the given rectangle in the style of an underline used to
 * indicate a spelling error.
 * @draw_shape: draw content for a glyph shaped with `GdkTextAttrShape`
 *   @x, @y are the coordinates of the left edge of the baseline,
 *   in user coordinates.
 * @draw_trapezoid: draws a trapezoidal filled area
 * @draw_glyph: draws a single glyph
 * @part_changed: do renderer specific processing when rendering
 *  attributes change
 * @begin: Do renderer-specific initialization before drawing
 * @end: Do renderer-specific cleanup after drawing
 * @prepare_run: updates the renderer for a new run
 * @draw_glyph_item: draws a `GdkTextGlyphItem`
 *
 * Class structure for `GdkTextRenderer`.
 *
 * The following vfuncs take user space coordinates in GdkText units
 * and have default implementations:
 * - draw_glyphs
 * - draw_rectangle
 * - draw_error_underline
 * - draw_shape
 * - draw_glyph_item
 *
 * The default draw_shape implementation draws nothing.
 *
 * The following vfuncs take device space coordinates as doubles
 * and must be implemented:
 * - draw_trapezoid
 * - draw_glyph
 *
 * The following vfuncs should look at the components value and
 * skip color or plain glyphs accordingly:
 * - draw_glyphs
 * - draw_glyph_item
 * - draw_glyph
 *
 * Since: 1.8
 */
struct _PangoRendererClass
{
  /*< private >*/
  GObjectClass parent_class;

  /* vtable - not signals */
  /*< public >*/

  void (*draw_glyphs)          (GdkTextRenderer    *renderer,
                                GdkTextFont        *font,
                                GdkTextGlyphString *glyphs,
                                int               x,
                                int               y);
  void (*draw_rectangle)       (GdkTextRenderer    *renderer,
                                GdkTextRenderPart   part,
                                int               x,
                                int               y,
                                int               width,
                                int               height);
  void (*draw_error_underline) (GdkTextRenderer    *renderer,
                                int               x,
                                int               y,
                                int               width,
                                int               height);
  void (*draw_shape)           (GdkTextRenderer    *renderer,
                                GdkTextAttrShape   *attr,
                                int               x,
                                int               y);

  void (*draw_trapezoid)       (GdkTextRenderer    *renderer,
                                GdkTextRenderPart   part,
                                double            y1_,
                                double            x11,
                                double            x21,
                                double            y2,
                                double            x12,
                                double            x22);
  void (*draw_glyph)           (GdkTextRenderer    *renderer,
                                GdkTextFont        *font,
                                GdkTextGlyph        glyph,
                                double            x,
                                double            y);

  void (*part_changed)         (GdkTextRenderer    *renderer,
                                GdkTextRenderPart   part);

  void (*begin)                (GdkTextRenderer    *renderer);
  void (*end)                  (GdkTextRenderer    *renderer);

  void (*prepare_run)          (GdkTextRenderer    *renderer,
                                GdkTextLayoutRun   *run);

  void (*draw_glyph_item)      (GdkTextRenderer    *renderer,
                                const char       *text,
                                GdkTextGlyphItem   *glyph_item,
                                int               x,
                                int               y);

  /*< private >*/

  /* Padding for future expansion */
  void (*_pango_reserved2) (void);
  void (*_pango_reserved3) (void);
  void (*_pango_reserved4) (void);
};

GDK_TEXT_AVAILABLE_IN_1_8
GType gdk_text_renderer_get_type            (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_draw_layout          (GdkTextRenderer    *renderer,
                                          GdkTextLayout      *layout,
                                          int               x,
                                          int               y);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_draw_layout_line     (GdkTextRenderer    *renderer,
                                          GdkTextLayoutLine  *line,
                                          int               x,
                                          int               y);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_draw_glyphs          (GdkTextRenderer    *renderer,
                                          GdkTextFont        *font,
                                          GdkTextGlyphString *glyphs,
                                          int               x,
                                          int               y);
GDK_TEXT_AVAILABLE_IN_1_22
void gdk_text_renderer_draw_glyph_item      (GdkTextRenderer    *renderer,
                                          const char       *text,
                                          GdkTextGlyphItem   *glyph_item,
                                          int               x,
                                          int               y);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_draw_rectangle       (GdkTextRenderer    *renderer,
                                          GdkTextRenderPart   part,
                                          int               x,
                                          int               y,
                                          int               width,
                                          int               height);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_draw_error_underline (GdkTextRenderer    *renderer,
                                          int               x,
                                          int               y,
                                          int               width,
                                          int               height);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_draw_trapezoid       (GdkTextRenderer    *renderer,
                                          GdkTextRenderPart   part,
                                          double            y1_,
                                          double            x11,
                                          double            x21,
                                          double            y2,
                                          double            x12,
                                          double            x22);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_draw_glyph           (GdkTextRenderer    *renderer,
                                          GdkTextFont        *font,
                                          GdkTextGlyph        glyph,
                                          double            x,
                                          double            y);

GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_activate             (GdkTextRenderer    *renderer);
GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_deactivate           (GdkTextRenderer    *renderer);

GDK_TEXT_AVAILABLE_IN_1_8
void gdk_text_renderer_part_changed         (GdkTextRenderer   *renderer,
                                          GdkTextRenderPart  part);

GDK_TEXT_AVAILABLE_IN_1_8
void        gdk_text_renderer_set_color     (GdkTextRenderer    *renderer,
                                          GdkTextRenderPart   part,
                                          const GdkTextColor *color);
GDK_TEXT_AVAILABLE_IN_1_8
GdkTextColor *gdk_text_renderer_get_color     (GdkTextRenderer    *renderer,
                                          GdkTextRenderPart   part);

GDK_TEXT_AVAILABLE_IN_1_38
void        gdk_text_renderer_set_alpha     (GdkTextRenderer    *renderer,
                                          GdkTextRenderPart   part,
                                          guint16           alpha);
GDK_TEXT_AVAILABLE_IN_1_38
guint16     gdk_text_renderer_get_alpha     (GdkTextRenderer    *renderer,
                                          GdkTextRenderPart   part);

GDK_TEXT_AVAILABLE_IN_1_8
void               gdk_text_renderer_set_matrix      (GdkTextRenderer     *renderer,
                                                   const GdkTextMatrix *matrix);
GDK_TEXT_AVAILABLE_IN_1_8
const GdkTextMatrix *gdk_text_renderer_get_matrix      (GdkTextRenderer     *renderer);

GDK_TEXT_AVAILABLE_IN_1_20
GdkTextLayout       *gdk_text_renderer_get_layout      (GdkTextRenderer     *renderer);
GDK_TEXT_AVAILABLE_IN_1_20
GdkTextLayoutLine   *gdk_text_renderer_get_layout_line (GdkTextRenderer     *renderer);


/**
 * GdkTextRenderComponent:
 * @GDK_TEXT_RENDER_COMPONENT_NONE: No components
 * @GDK_TEXT_RENDER_COMPONENT_PLAIN_GLYPH: The plain glyphs of the layout
 * @GDK_TEXT_RENDER_COMPONENT_COLOR_GLYPH: The color glyphs of the layout
 * @GDK_TEXT_RENDER_COMPONENT_BACKGROUND: Background of the layout
 * @GDK_TEXT_RENDER_COMPONENT_UNDERLINE: Underlines of the layout
 * @GDK_TEXT_RENDER_COMPONENT_STRIKETHROUGH: Strikethrough lines of the layout
 * @GDK_TEXT_RENDER_COMPONENT_OVERLINE: Overlines of the layout
 *
 * Flags that specify which components of a layout to include
 * in renderer output.
 *
 * This is more or less parallel to the [enum@GdkText.RenderPart] enum,
 * but allows separating plain and color glyphs, and specifying more
 * than one component.
 *
 * Since: 1.58
 */
typedef enum
{
  GDK_TEXT_RENDER_COMPONENT_NONE          = 0,
  GDK_TEXT_RENDER_COMPONENT_PLAIN_GLYPH   = 1 << 1,
  GDK_TEXT_RENDER_COMPONENT_COLOR_GLYPH   = 1 << 2,
  GDK_TEXT_RENDER_COMPONENT_BACKGROUND    = 1 << 3,
  GDK_TEXT_RENDER_COMPONENT_UNDERLINE     = 1 << 3,
  GDK_TEXT_RENDER_COMPONENT_STRIKETHROUGH = 1 << 4,
  GDK_TEXT_RENDER_COMPONENT_OVERLINE      = 1 << 5,
} GdkTextRenderComponent;

#define GDK_TEXT_RENDER_COMPONENT_ALL (GDK_TEXT_RENDER_COMPONENT_PLAIN_GLYPH | \
                                    GDK_TEXT_RENDER_COMPONENT_COLOR_GLYPH | \
                                    GDK_TEXT_RENDER_COMPONENT_BACKGROUND | \
                                    GDK_TEXT_RENDER_COMPONENT_UNDERLINE | \
                                    GDK_TEXT_RENDER_COMPONENT_STRIKETHROUGH | \
                                    GDK_TEXT_RENDER_COMPONENT_OVERLINE)

GDK_TEXT_AVAILABLE_IN_1_58
void                  gdk_text_renderer_set_components  (GdkTextRenderer        *renderer,
                                                      GdkTextRenderComponent  components);
GDK_TEXT_AVAILABLE_IN_1_58
GdkTextRenderComponent  gdk_text_renderer_get_components (GdkTextRenderer         *renderer);


G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextRenderer, g_object_unref)

G_END_DECLS

#endif /* __PANGO_RENDERER_H_ */

