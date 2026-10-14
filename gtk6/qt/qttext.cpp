/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK6 text engine — see qttext.h.
 *
 * Qt owns shaping here.  QTextLayout breaks the paragraph, applies the
 * ctl/bidi algorithm and extracts glyph runs through QTextLine::glyphRuns();
 * QPainter::drawText on the layout does the balanced-glyph blit.  None of the
 * three names Pango, HarfBuzz or FreeType, which is the point: GTK keeps its
 * text API and Qt supplies the engine.
 */

#include "qttext.h"

#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtCore/QVector>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QPainter>
#include <QtGui/QGlyphRun>
#include <QtGui/QTextLayout>
#include <QtGui/QTextLine>

#include <string.h>

/* ── fonts ─────────────────────────────────────────────────────────────── */

struct _GtkQtFont
{
  QFont font;
  int   refcount = 1;
};

struct _GtkQtText
{
  GtkQtFont *font = nullptr;

  QString text;
  double  wrap_width = -1;

  bool    dirty  = true;
  double  width  = 0;
  double  height = 0;
  double  ascent = 0;
  int     lines  = 0;

  QTextLayout layout;
};

extern "C" GtkQtFont *
gtk_qt_font_new (const char *family,
                 double      size_pt)
{
  GtkQtFont *f = new GtkQtFont ();

  f->font = QFont (QString::fromUtf8 (family ? family : "Sans Serif"));
  f->font.setPointSizeF (size_pt > 0 ? size_pt : 10.0);

  return f;
}

extern "C" GtkQtFont *
gtk_qt_font_ref (GtkQtFont *font)
{
  if (font)
    font->refcount++;
  return font;
}

extern "C" void
gtk_qt_font_unref (GtkQtFont *font)
{
  if (!font)
    return;

  if (--font->refcount == 0)
    delete font;
}

/* ── layout ────────────────────────────────────────────────────────────── */

extern "C" GtkQtText *
gtk_qt_text_new (GtkQtFont *font)
{
  GtkQtText *t = new GtkQtText ();

  t->font = gtk_qt_font_ref (font);
  if (!t->font)
    t->font = gtk_qt_font_new ("Sans Serif", 10.0);

  return t;
}

extern "C" void
gtk_qt_text_free (GtkQtText *text)
{
  if (!text)
    return;

  gtk_qt_font_unref (text->font);
  delete text;
}

extern "C" void
gtk_qt_text_set_text (GtkQtText  *text,
                      const char *utf8,
                      int         byte_len)
{
  if (!text)
    return;

  text->text = byte_len >= 0
                 ? QString::fromUtf8 (utf8, byte_len)
                 : QString::fromUtf8 (utf8 ? utf8 : "");
  text->dirty = true;
}

extern "C" void
gtk_qt_text_set_width (GtkQtText *text,
                       double     wrap_width)
{
  if (!text)
    return;

  text->wrap_width = wrap_width;
  text->dirty = true;
}

static void
gtk_qt_text_relayout (GtkQtText *text)
{
  if (!text->dirty)
    return;

  QFontMetricsF metrics (text->font->font);

  text->layout.setFont (text->font->font);
  text->layout.setText (text->text);

  text->width  = 0;
  text->height = 0;
  text->lines  = 0;
  text->ascent = text->text.isEmpty () ? metrics.height () : metrics.ascent ();

  /* A layout without a line width would wrap at Qt's default, so an explicit
   * "1e9" carries the no-wrap case instead of a separate code path. */
  const double line_width = text->wrap_width > 0 ? text->wrap_width : 1e9;

  text->layout.beginLayout ();
  for (;;)
    {
      QTextLine line = text->layout.createLine ();

      if (!line.isValid ())
        break;

      line.setLineWidth (line_width);
      line.setPosition (QPointF (0, text->height));

      text->height += line.height ();
      text->width   = qMax (text->width, line.naturalTextWidth ());
      text->lines++;
    }
  text->layout.endLayout ();

  text->dirty = false;
}

extern "C" void
gtk_qt_text_get_size (GtkQtText *text,
                      double    *width,
                      double    *height)
{
  if (!text)
    return;

  gtk_qt_text_relayout (text);

  if (width)
    *width = text->width;
  if (height)
    *height = text->height;
}

extern "C" int
gtk_qt_text_get_line_count (GtkQtText *text)
{
  if (!text)
    return 0;

  gtk_qt_text_relayout (text);

  return text->lines;
}

extern "C" double
gtk_qt_text_get_ascent (GtkQtText *text)
{
  if (!text)
    return 0;

  gtk_qt_text_relayout (text);

  return text->ascent;
}

extern "C" int
gtk_qt_text_get_glyphs (GtkQtText *text,
                        int        line,
                        uint32_t   *glyphs,
                        float     *positions,
                        int        max_glyphs)
{
  if (!text)
    return 0;

  gtk_qt_text_relayout (text);

  if (line < 0 || line >= text->lines || max_glyphs <= 0)
    return 0;

  QTextLine qline = text->layout.lineAt (line);
  const QList<QGlyphRun> runs = qline.glyphRuns ();

  int out = 0;
  for (const QGlyphRun &run : runs)
    {
      const QList<quint32> indexes = run.glyphIndexes ();
      const QList<QPointF> points  = run.positions ();

      for (int i = 0; i < indexes.size () && i < points.size (); i++)
        {
          if (out >= max_glyphs)
            return out;

          glyphs[out] = indexes.at (i);
          if (positions)
            {
              positions[2 * out]     = points.at (i).x ();
              positions[2 * out + 1] = points.at (i).y ();
            }
          out++;
        }
    }

  return out;
}

extern "C" void
gtk_qt_text_draw (GtkQtText *text,
                  void      *painter,
                  double     x,
                  double     y)
{
  if (!text || !painter)
    return;

  gtk_qt_text_relayout (text);

  QPainter *p = static_cast<QPainter *> (painter);
  const QVector<QTextLayout::FormatRange> no_formats;

  p->setFont (text->font->font);
  text->layout.draw (p, QPointF (x, y), no_formats);
}
