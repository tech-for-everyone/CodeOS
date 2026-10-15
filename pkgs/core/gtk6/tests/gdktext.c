/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile-only probe for GDK's text API (gdk/gdktext.h).
 *
 * gdktext.h is the text replacement: the Pango public headers, vendored under
 * gdk/text/pango/ and renamed, reached through <gdk/gdktypes.h> without the
 * system Pango on the include path.  This probe names the types and constants
 * the renamed sources name, and takes the address of a spread of the
 * functions, so that a missing declaration is a build failure rather than a
 * link error much later.  It is never linked or run (see GTK4_PROBES in the
 * Makefile).
 *
 * The negative control is the same probe without <gdk/gdktypes.h>: on this
 * build the symbol PangoDirection has no declaration at all.
 */

#include <gdk/gdktypes.h>

/* The scalar and opaque types the GTK4 sources spell.  They are complete
 * enough to declare objects of, which is what the sources do. */
static GdkTextRectangle        rect;
static GdkTextMatrix           matrix;
static GdkTextColor            color;
static GdkTextLogAttr          log_attr;
static GdkTextItem             item;
static GdkTextGlyphItem        glyph_item;
static GdkTextGlyph            glyph;
static GdkTextDirection        direction;
static GdkTextGravity          gravity;
static GdkTextScript           script;
static GdkTextBidiType         bidi_type;
static GdkTextAttrType         attr_type;

static GdkTextContext          *context;
static GdkTextLayout           *layout;
static GdkTextAttribute        *attribute;
static GdkTextAttrList         *attr_list;
static GdkTextAttrIterator     *attr_iter;
static GdkTextFontDescription  *font_desc;
static GdkTextFontMap          *font_map;
static GdkTextFontset          *fontset;
static GdkTextLanguage         *language;
static GdkTextTabArray         *tabs;
static GdkTextCoverage         *coverage;
static GdkTextRenderer         *renderer;

/* The pangocairo half, which names cairo types: those must resolve to
 * gdkpaint.h's GdkPainter/GdkPaintFontOptions, not to <cairo.h>. */
static GdkTextCairoFontMap     *cairo_font_map;
static GdkPaintFontOptions     *font_options;
static GdkPainter              *painter;

int
gdktext_probe (void)
{
  const void *functions[] = {
    (const void *) gdk_text_layout_new,
    (const void *) gdk_text_layout_set_text,
    (const void *) gdk_text_layout_get_pixel_size,
    (const void *) gdk_text_context_new,
    (const void *) gdk_text_font_description_from_string,
    (const void *) gdk_text_attr_list_new,
    (const void *) gdk_text_attr_iterator_next,
    (const void *) gdk_text_matrix_translate,
    (const void *) gdk_text_language_from_string,
    (const void *) gdk_text_units_from_double,
    (const void *) gdk_text_version,
    (const void *) gdk_text_cairo_create_layout,
    (const void *) gdk_text_cairo_font_map_get_default,
    (const void *) gdk_text_cairo_show_layout,
  };
  const void *objects[] = {
    &rect, &matrix, &color, &log_attr, &item, &glyph_item, &glyph,
    &direction, &gravity, &script, &bidi_type, &attr_type,
    &context, &layout, &attribute, &attr_list, &attr_iter,
    &font_desc, &font_map, &fontset, &language, &tabs, &coverage, &renderer,
    &cairo_font_map, &font_options, &painter,
  };
  const int constants[] = {
    GDK_TEXT_DIRECTION_LTR,
    GDK_TEXT_SCALE,
    GDK_TEXT_GRAVITY_SOUTH,
    GDK_TEXT_SCRIPT_LATIN,
    GDK_TEXT_BIDI_TYPE_L,
    GDK_TEXT_ATTR_FAMILY,
    GDK_TEXT_DIRECTION_RTL,
  };

  (void) functions;
  (void) objects;
  (void) constants;
  (void) GDK_TEXT_ASCENT (rect);

  return 0;
}
