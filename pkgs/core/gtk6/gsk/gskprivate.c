#include "config.h"

#include "gskresources.h"
#include "gskprivate.h"

#include <gdk/gdkpaint.h>
#include <pango/pangocairo.h>
#ifdef HAVE_PANGOFT
#include <pango/pangoft2.h>
#endif
#include <math.h>

static gpointer
register_resources (gpointer data)
{
  _gsk_register_resource ();
  return NULL;
}

void
gsk_ensure_resources (void)
{
  static GOnce register_resources_once = G_ONCE_INIT;

  g_once (&register_resources_once, register_resources, NULL);
}

/*< private >
 * gsk_reload_font:
 * @font: a `GdkTextFont`
 * @scale: the scale to apply
 * @hint_metris: hint metrics to use or `GDK_PAINT_HINT_METRICS_DEFAILT` to keep the
 *   hint metrics of the font unchanged
 * @hint_style: hint style to use or `GDK_PAINT_HINT_STYLE_DEFAULT` to keep the
 *   hint style of @font unchanged
 * @antialias: antialiasing to use, or `GDK_PAINT_ANTIALIAS_DEFAULT` to keep the
 *   antialias option of @font unchanged
 *
 * Returns a font that is just like @font, but uses the
 * given scale and hinting options for its glyphs and metrics.
 *
 * Returns: (transfer full): the modified `GdkTextFont`
 */
GdkTextFont *
gsk_reload_font (GdkTextFont            *font,
                 float                 scale,
                 GdkPaintHintMetrics  hint_metrics,
                 GdkPaintHintStyle    hint_style,
                 GdkPaintAntialias     antialias)
{
  static GdkPaintFontOptions *options = NULL;
  static GdkTextContext *context = NULL;
  GdkPaintScaledFont *sf;

  /* These requests often come in sequentially so keep the result
   * around and re-use it if everything matches.
   */
  static GdkTextFont *last_font;
  static float last_scale;
  static GdkPaintHintMetrics last_hint_metrics;
  static GdkPaintHintStyle last_hint_style;
  static GdkPaintAntialias last_antialias;
  static GdkTextFont *last_result;

  if (last_result != NULL &&
      last_font == font &&
      last_scale == scale &&
      last_hint_metrics == hint_metrics &&
      last_hint_style == hint_style &&
      last_antialias == antialias)
    return g_object_ref (last_result);

  last_scale = scale;
  last_hint_metrics = hint_metrics;
  last_hint_style = hint_style;
  last_antialias = antialias;

  g_set_object (&last_font, font);
  g_clear_object (&last_result);

  if (G_UNLIKELY (options == NULL))
    options = gdk_paint_font_options_create ();

  sf = gdk_text_cairo_font_get_scaled_font (GDK_TEXT_CAIRO_FONT (font));
  gdk_paint_scaled_font_get_font_options (sf, options);

  if (hint_metrics == GDK_PAINT_HINT_METRICS_DEFAULT)
    hint_metrics = gdk_paint_font_options_get_hint_metrics (options);

  if (hint_style == GDK_PAINT_HINT_STYLE_DEFAULT)
    hint_style = gdk_paint_font_options_get_hint_style (options);

  if (antialias == GDK_PAINT_ANTIALIAS_DEFAULT)
    antialias = gdk_paint_font_options_get_antialias (options);

  if (1.0 == scale &&
      gdk_paint_font_options_get_hint_metrics (options) == hint_metrics &&
      gdk_paint_font_options_get_hint_style (options) == hint_style &&
      gdk_paint_font_options_get_antialias (options) == antialias &&
      gdk_paint_font_options_get_subpixel_order (options) == GDK_PAINT_SUBPIXEL_ORDER_DEFAULT)
    {
      last_result = g_object_ref (font);
      return g_object_ref (font);
    }

  gdk_paint_font_options_set_hint_metrics (options, hint_metrics);
  gdk_paint_font_options_set_hint_style (options, hint_style);
  gdk_paint_font_options_set_antialias (options, antialias);
  gdk_paint_font_options_set_subpixel_order (options, GDK_PAINT_SUBPIXEL_ORDER_DEFAULT);

  if (G_UNLIKELY (context == NULL))
    context = gdk_text_context_new ();

  gdk_text_context_set_round_glyph_positions (context, hint_metrics == GDK_PAINT_HINT_METRICS_ON);
  gdk_text_cairo_context_set_font_options (context, options);

  last_result = gdk_text_font_map_reload_font (gdk_text_font_get_font_map (font), font, scale, context, NULL);

  return g_object_ref (last_result);
}

/*< private >
 * gsk_get_glyph_string_extents:
 * @glyphs: a `GdkTextGlyphString`
 * @font: a `GdkTextFont`
 * @ink_rect: (out): rectangle used to store the extents of the glyph string as drawn
 *
 * Compute the ink extents of a glyph string.
 *
 * This is like [method@GdkText.GlyphString.extents], but it
 * ignores hinting of the font.
 */
void
gsk_get_glyph_string_extents (GdkTextGlyphString *glyphs,
                              GdkTextFont        *font,
                              GdkTextRectangle   *ink_rect)
{
  GdkTextFont *unhinted;

  unhinted = gsk_reload_font (font,
                              1.0,
                              GDK_PAINT_HINT_METRICS_DEFAULT,
                              GDK_PAINT_HINT_STYLE_NONE,
                              GDK_PAINT_ANTIALIAS_DEFAULT);

  gdk_text_glyph_string_extents (glyphs, unhinted, ink_rect, NULL);

  g_object_unref (unhinted);
}

/*< private >
 * gsk_font_get_hint_style:
 * @font: a `GdkTextFont`
 *
 * Get the hint style from the cairo font options.
 *
 * Returns: the hint style
 */
GdkPaintHintStyle
gsk_font_get_hint_style (GdkTextFont *font)
{
  static GdkPaintFontOptions *options = NULL;
  GdkPaintScaledFont *sf;
  GdkPaintHintStyle style;

  if (G_UNLIKELY (options == NULL))
    options = gdk_paint_font_options_create ();

  sf = gdk_text_cairo_font_get_scaled_font (GDK_TEXT_CAIRO_FONT (font));
  gdk_paint_scaled_font_get_font_options (sf, options);
  style = gdk_paint_font_options_get_hint_style (options);

  return style;
}
