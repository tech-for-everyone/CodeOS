/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK6 text engine — the Phase 4 replacement for Pango.
 *
 * GTK passes text around as UTF-8 (GtkStringList, GtkLabel, entry buffers) and
 * asks for three things: a shaped layout, a size, and a set of drawing calls.
 * Pango answers those with PangoLayout/PangoGlyphString and Cairo; this module
 * answers them with Qt6 (QTextLayout, QFontMetrics, QPainter).  There is no
 * Pango header in a GTK6 build.
 *
 * <glib.h> is deliberately not included: this header is consumed by the
 * freestanding build, where the only C++ runtime is Qt6 and glib's own
 * glib.h would drag in libc headers the target does not have.  The types
 * below are the whole contract.
 */

#ifndef GTK6_QT_TEXT_H
#define GTK6_QT_TEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _GtkQtFont GtkQtFont;
typedef struct _GtkQtText GtkQtText;

/* ── fonts ─────────────────────────────────────────────────────────────── */

GtkQtFont *gtk_qt_font_new   (const char *family, double size_pt);
GtkQtFont *gtk_qt_font_ref   (GtkQtFont *font);
void       gtk_qt_font_unref (GtkQtFont *font);

/* ── layout ────────────────────────────────────────────────────────────── */

GtkQtText *gtk_qt_text_new  (GtkQtFont *font);
void       gtk_qt_text_free (GtkQtText *text);

void gtk_qt_text_set_text  (GtkQtText *text, const char *utf8, int byte_len);
void gtk_qt_text_set_width (GtkQtText *text, double wrap_width);

/* Cheap once the layout is clean; the size survives repeated queries. */
void gtk_qt_text_get_size (GtkQtText *text, double *width, double *height);
int  gtk_qt_text_get_line_count (GtkQtText *text);

/* The ascent of the run, so callers can align on a baseline. */
double gtk_qt_text_get_ascent (GtkQtText *text);

/* Copy the shaped glyphs for one line out of Qt, for GSK text nodes.
 * positions is 2 * glyphs (x, y) and may be NULL.  Returns the glyph count. */
int gtk_qt_text_get_glyphs (GtkQtText *text,
                            int        line,
                            uint32_t  *glyphs,
                            float     *positions,
                            int        max_glyphs);

/* painter is a QPainter *; x/y are the top-left of the first line. */
void gtk_qt_text_draw (GtkQtText *text, void *painter, double x, double y);

#ifdef __cplusplus
}
#endif

#endif /* GTK6_QT_TEXT_H */
