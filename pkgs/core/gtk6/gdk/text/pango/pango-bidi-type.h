/* GdkText
 * pango-bidi-type.h: Bidirectional Character Types
 *
 * Copyright (C) 2008 Jürg Billeter <j@bitron.ch>
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

#ifndef __PANGO_BIDI_TYPE_H__
#define __PANGO_BIDI_TYPE_H__

#include <glib.h>

#include <pango/pango-version-macros.h>
#include <pango/pango-direction.h>

G_BEGIN_DECLS

#ifndef GDK_TEXT_DISABLE_DEPRECATED
/**
 * GdkTextBidiType:
 * @GDK_TEXT_BIDI_TYPE_L: Left-to-Right
 * @GDK_TEXT_BIDI_TYPE_LRE: Left-to-Right Embedding
 * @GDK_TEXT_BIDI_TYPE_LRO: Left-to-Right Override
 * @GDK_TEXT_BIDI_TYPE_R: Right-to-Left
 * @GDK_TEXT_BIDI_TYPE_AL: Right-to-Left Arabic
 * @GDK_TEXT_BIDI_TYPE_RLE: Right-to-Left Embedding
 * @GDK_TEXT_BIDI_TYPE_RLO: Right-to-Left Override
 * @GDK_TEXT_BIDI_TYPE_PDF: Pop Directional Format
 * @GDK_TEXT_BIDI_TYPE_EN: European Number
 * @GDK_TEXT_BIDI_TYPE_ES: European Number Separator
 * @GDK_TEXT_BIDI_TYPE_ET: European Number Terminator
 * @GDK_TEXT_BIDI_TYPE_AN: Arabic Number
 * @GDK_TEXT_BIDI_TYPE_CS: Common Number Separator
 * @GDK_TEXT_BIDI_TYPE_NSM: Nonspacing Mark
 * @GDK_TEXT_BIDI_TYPE_BN: Boundary Neutral
 * @GDK_TEXT_BIDI_TYPE_B: Paragraph Separator
 * @GDK_TEXT_BIDI_TYPE_S: Segment Separator
 * @GDK_TEXT_BIDI_TYPE_WS: Whitespace
 * @GDK_TEXT_BIDI_TYPE_ON: Other Neutrals
 * @GDK_TEXT_BIDI_TYPE_LRI: Left-to-Right isolate. Since 1.48.6
 * @GDK_TEXT_BIDI_TYPE_RLI: Right-to-Left isolate. Since 1.48.6
 * @GDK_TEXT_BIDI_TYPE_FSI: First strong isolate. Since 1.48.6
 * @GDK_TEXT_BIDI_TYPE_PDI: Pop directional isolate. Since 1.48.6
 *
 * `GdkTextBidiType` represents the bidirectional character
 * type of a Unicode character.
 *
 * The values in this enumeration are specified by the
 * [Unicode bidirectional algorithm](http://www.unicode.org/reports/tr9/).
 *
 * Since: 1.22
 * Deprecated: 1.44: Use fribidi for this information
 **/
typedef enum {
  /* Strong types */
  GDK_TEXT_BIDI_TYPE_L,
  GDK_TEXT_BIDI_TYPE_LRE,
  GDK_TEXT_BIDI_TYPE_LRO,
  GDK_TEXT_BIDI_TYPE_R,
  GDK_TEXT_BIDI_TYPE_AL,
  GDK_TEXT_BIDI_TYPE_RLE,
  GDK_TEXT_BIDI_TYPE_RLO,

  /* Weak types */
  GDK_TEXT_BIDI_TYPE_PDF,
  GDK_TEXT_BIDI_TYPE_EN,
  GDK_TEXT_BIDI_TYPE_ES,
  GDK_TEXT_BIDI_TYPE_ET,
  GDK_TEXT_BIDI_TYPE_AN,
  GDK_TEXT_BIDI_TYPE_CS,
  GDK_TEXT_BIDI_TYPE_NSM,
  GDK_TEXT_BIDI_TYPE_BN,

  /* Neutral types */
  GDK_TEXT_BIDI_TYPE_B,
  GDK_TEXT_BIDI_TYPE_S,
  GDK_TEXT_BIDI_TYPE_WS,
  GDK_TEXT_BIDI_TYPE_ON,

  /* Explicit formatting */
  GDK_TEXT_BIDI_TYPE_LRI,
  GDK_TEXT_BIDI_TYPE_RLI,
  GDK_TEXT_BIDI_TYPE_FSI,
  GDK_TEXT_BIDI_TYPE_PDI
} GdkTextBidiType;

GDK_TEXT_DEPRECATED_IN_1_44
GdkTextBidiType gdk_text_bidi_type_for_unichar (gunichar ch) G_GNUC_CONST;

GDK_TEXT_DEPRECATED_IN_1_44
GdkTextDirection gdk_text_unichar_direction      (gunichar     ch) G_GNUC_CONST;
GDK_TEXT_DEPRECATED_IN_1_44
GdkTextDirection gdk_text_find_base_dir          (const gchar *text,
					     gint         length);

GDK_TEXT_DEPRECATED_IN_1_30_FOR(g_unichar_get_mirror_char)
gboolean       gdk_text_get_mirror_char        (gunichar     ch,
					     gunichar    *mirrored_ch);
#endif

G_END_DECLS

#endif /* __PANGO_BIDI_TYPE_H__ */
