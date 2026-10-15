/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile-only fixture for the Phase 0/2/3/4 headers: if the public headers
 * stop being self-consistent for a freestanding C++ consumer, this stops
 * building.  It is deliberately never linked or run.
 */

#include "gdk/qt/gdkqt.h"
#include "gsk/qt/gskqt.h"
#include "qt/qttext.h"

static void
gtk6_paint (void   *user_data,
            void   *painter,
            double  width,
            double  height)
{
  GskQtRenderer *renderer = static_cast<GskQtRenderer *> (user_data);

  gsk_qt_renderer_begin_frame (renderer, painter, width, height);
  gsk_qt_renderer_fill_rect (renderer, 0, 0, width, height, 0x202020FFu);
  gsk_qt_renderer_end_frame (renderer);
}

int
gtk6_linkcheck (void)
{
  if (!gdk_qt_init (nullptr, nullptr))
    return 1;

  GskQtRenderer *renderer = gsk_qt_renderer_new ();
  GdkQtSurface  *surface  = gdk_qt_surface_new_toplevel ("check", 640, 480);

  gdk_qt_surface_set_paint_func (surface, gtk6_paint, renderer);
  gdk_qt_surface_show (surface);
  gdk_qt_surface_present (surface);

  GtkQtFont *font = gtk_qt_font_new ("Sans Serif", 12);
  GtkQtText *text = gtk_qt_text_new (font);

  gtk_qt_text_set_text (text, "hello", -1);
  gtk_qt_text_set_width (text, 640);

  double width = 0, height = 0;
  gtk_qt_text_get_size (text, &width, &height);
  (void) width;
  (void) height;
  (void) gtk_qt_text_get_line_count (text);
  (void) gtk_qt_text_get_ascent (text);

  uint32_t glyphs[16];
  float    positions[32];
  (void) gtk_qt_text_get_glyphs (text, 0, glyphs, positions, 16);

  gtk_qt_text_free (text);
  gtk_qt_font_unref (font);

  gsk_qt_renderer_free (renderer);
  gdk_qt_surface_destroy (surface);
  gdk_qt_display_finish ();

  return 0;
}
