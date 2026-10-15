#pragma once

#include <glib.h>
#include <pango/pango.h>
#include <gdk/gdkpaint.h>

G_BEGIN_DECLS

void       gsk_ensure_resources  (void);

PangoFont *gsk_reload_font (PangoFont            *font,
                            float                 scale,
                            GdkPaintHintMetrics  hint_metrics,
                            GdkPaintHintStyle    hint_style,
                            GdkPaintAntialias     antialias);

void       gsk_get_glyph_string_extents (PangoGlyphString *glyphs,
                                         PangoFont        *font,
                                         PangoRectangle   *ink_rect);

GdkPaintHintStyle gsk_font_get_hint_style (PangoFont *font);

G_END_DECLS

