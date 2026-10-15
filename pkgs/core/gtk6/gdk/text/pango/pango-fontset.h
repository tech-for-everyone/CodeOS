/* GdkText
 * pango-fontset.h: Font set handling
 *
 * Copyright (C) 2001 Red Hat Software
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

#ifndef __PANGO_FONTSET_H__
#define __PANGO_FONTSET_H__

#include <pango/pango-coverage.h>
#include <pango/pango-types.h>

#include <glib-object.h>

G_BEGIN_DECLS

/*
 * GdkTextFontset
 */

#define GDK_TEXT_TYPE_FONTSET              (gdk_text_fontset_get_type ())
#define GDK_TEXT_FONTSET(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FONTSET, GdkTextFontset))
#define GDK_TEXT_IS_FONTSET(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FONTSET))
#define GDK_TEXT_FONTSET_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_FONTSET, GdkTextFontsetClass))
#define GDK_TEXT_IS_FONTSET_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_FONTSET))
#define GDK_TEXT_FONTSET_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_FONTSET, GdkTextFontsetClass))


GDK_TEXT_AVAILABLE_IN_ALL
GType gdk_text_fontset_get_type (void) G_GNUC_CONST;

typedef struct _PangoFontset        GdkTextFontset;
typedef struct _PangoFontsetClass   GdkTextFontsetClass;

/**
 * GdkTextFontsetForeachFunc:
 * @fontset: a `GdkTextFontset`
 * @font: a font from @fontset
 * @user_data: callback data
 *
 * Callback used when enumerating fonts in a fontset.
 *
 * See [method@GdkText.Fontset.foreach].
 *
 * Returns: if %TRUE, stop iteration and return immediately.
 *
 * Since: 1.4
 */
typedef gboolean (*GdkTextFontsetForeachFunc) (GdkTextFontset  *fontset,
                                             GdkTextFont     *font,
                                             gpointer       user_data);

/**
 * GdkTextFontset:
 *
 * A `GdkTextFontset` represents a set of `GdkTextFont` to use when rendering text.
 *
 * A `GdkTextFontset` is the result of resolving a `GdkTextFontDescription`
 * against a particular `GdkTextContext`. It has operations for finding the
 * component font for a particular Unicode character, and for finding a
 * composite set of metrics for the entire fontset.
 */
struct _PangoFontset
{
  GObject parent_instance;
};

/**
 * GdkTextFontsetClass:
 * @parent_class: parent `GObjectClass`
 * @get_font: a function to get the font in the fontset that contains the
 *   best glyph for the given Unicode character; see [method@GdkText.Fontset.get_font]
 * @get_metrics: a function to get overall metric information for the fonts
 *   in the fontset; see [method@GdkText.Fontset.get_metrics]
 * @get_language: a function to get the language of the fontset.
 * @foreach: a function to loop over the fonts in the fontset. See
 *   [method@GdkText.Fontset.foreach]
 *
 * The `GdkTextFontsetClass` structure holds the virtual functions for
 * a particular `GdkTextFontset` implementation.
 */
struct _PangoFontsetClass
{
  GObjectClass parent_class;

  /*< public >*/

  GdkTextFont *       (*get_font)     (GdkTextFontset     *fontset,
                                     guint             wc);

  GdkTextFontMetrics *(*get_metrics)  (GdkTextFontset     *fontset);
  GdkTextLanguage *   (*get_language) (GdkTextFontset     *fontset);
  void              (*foreach)      (GdkTextFontset           *fontset,
                                     GdkTextFontsetForeachFunc func,
                                     gpointer                data);

  /*< private >*/

  /* Padding for future expansion */
  void (*_pango_reserved1) (void);
  void (*_pango_reserved2) (void);
  void (*_pango_reserved3) (void);
  void (*_pango_reserved4) (void);
};

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFont *             gdk_text_fontset_get_font          (GdkTextFontset                   *fontset,
                                                         guint                           wc);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontMetrics *      gdk_text_fontset_get_metrics       (GdkTextFontset                   *fontset);
GDK_TEXT_AVAILABLE_IN_1_4
void                    gdk_text_fontset_foreach           (GdkTextFontset                   *fontset,
                                                         GdkTextFontsetForeachFunc         func,
                                                         gpointer                        data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextFontset, g_object_unref)

G_END_DECLS

#endif /* __PANGO_FONTSET_H__ */
