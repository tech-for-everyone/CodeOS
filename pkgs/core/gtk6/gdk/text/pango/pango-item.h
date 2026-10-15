/* GdkText
 * pango-item.h: Structure for storing run information
 *
 * Copyright (C) 2000 Red Hat Software
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

#ifndef __PANGO_ITEM_H__
#define __PANGO_ITEM_H__

#include <pango/pango-types.h>
#include <pango/pango-attributes.h>

G_BEGIN_DECLS

typedef struct _PangoAnalysis GdkTextAnalysis;
typedef struct _PangoItem GdkTextItem;

/**
 * GDK_TEXT_ANALYSIS_FLAG_CENTERED_BASELINE:
 *
 * Whether the segment should be shifted to center around the baseline.
 *
 * This is mainly used in vertical writing directions.
 *
 * Since: 1.16
 */
#define GDK_TEXT_ANALYSIS_FLAG_CENTERED_BASELINE (1 << 0)

/**
 * GDK_TEXT_ANALYSIS_FLAG_IS_ELLIPSIS:
 *
 * Whether this run holds ellipsized text.
 *
 * Since: 1.36.7
 */
#define GDK_TEXT_ANALYSIS_FLAG_IS_ELLIPSIS (1 << 1)

/**
 * GDK_TEXT_ANALYSIS_FLAG_NEED_HYPHEN:
 *
 * Whether to add a hyphen at the end of the run during shaping.
 *
 * Since: 1.44
 */
#define GDK_TEXT_ANALYSIS_FLAG_NEED_HYPHEN (1 << 2)

/**
 * GdkTextAnalysis:
 * @shape_engine: unused, reserved
 * @lang_engine: unused, reserved
 * @font: the font for this segment.
 * @level: the bidirectional level for this segment.
 * @gravity: the glyph orientation for this segment (A `GdkTextGravity`).
 * @flags: boolean flags for this segment (Since: 1.16).
 * @script: the detected script for this segment (A `GdkTextScript`) (Since: 1.18).
 * @language: the detected language for this segment.
 * @extra_attrs: extra attributes for this segment.
 *
 * The `GdkTextAnalysis` structure stores information about
 * the properties of a segment of text.
 */
struct _PangoAnalysis
{
#ifndef __GI_SCANNER__
  GdkTextEngineShape *shape_engine;
  GdkTextEngineLang  *lang_engine;
#else
  gpointer shape_engine;
  gpointer lang_engine;
#endif
  GdkTextFont *font;

  guint8 level;
  guint8 gravity;
  guint8 flags;

  guint8 script;
  GdkTextLanguage *language;

  GSList *extra_attrs;
};

/**
 * GdkTextItem:
 * @offset: byte offset of the start of this item in text.
 * @length: length of this item in bytes.
 * @num_chars: number of Unicode characters in the item.
 * @analysis: analysis results for the item.
 *
 * The `GdkTextItem` structure stores information about a segment of text.
 *
 * You typically obtain `GdkTextItems` by itemizing a piece of text
 * with [func@itemize].
 */
struct _PangoItem
{
  int offset;
  int length;
  int num_chars;
  GdkTextAnalysis analysis;
};

#define GDK_TEXT_TYPE_ITEM (gdk_text_item_get_type ())

GDK_TEXT_AVAILABLE_IN_ALL
GType                   gdk_text_item_get_type          (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextItem *             gdk_text_item_new               (void);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextItem *             gdk_text_item_copy              (GdkTextItem         *item);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_item_free              (GdkTextItem         *item);


GDK_TEXT_AVAILABLE_IN_1_54
int                     gdk_text_item_get_char_offset   (GdkTextItem         *item);

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextItem *             gdk_text_item_split             (GdkTextItem         *orig,
                                                      int                split_index,
                                                      int                split_offset);

GDK_TEXT_AVAILABLE_IN_1_44
void                    gdk_text_item_apply_attrs       (GdkTextItem         *item,
                                                      GdkTextAttrIterator *iter);

GDK_TEXT_AVAILABLE_IN_ALL
GList *                 gdk_text_reorder_items          (GList             *items);

/* Itemization */

GDK_TEXT_AVAILABLE_IN_ALL
GList *                 gdk_text_itemize                (GdkTextContext      *context,
                                                      const char        *text,
                                                      int                start_index,
                                                      int                length,
                                                      GdkTextAttrList     *attrs,
                                                      GdkTextAttrIterator *cached_iter);

GDK_TEXT_AVAILABLE_IN_1_4
GList *                 gdk_text_itemize_with_base_dir  (GdkTextContext      *context,
                                                      GdkTextDirection     base_dir,
                                                      const char        *text,
                                                      int                start_index,
                                                      int                length,
                                                      GdkTextAttrList     *attrs,
                                                      GdkTextAttrIterator *cached_iter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextItem, gdk_text_item_free)

G_END_DECLS

#endif /* __PANGO_ITEM_H__ */
