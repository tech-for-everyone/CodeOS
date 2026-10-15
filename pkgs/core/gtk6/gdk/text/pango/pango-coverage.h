/* GdkText
 * pango-coverage.h: Coverage sets for fonts
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __PANGO_COVERAGE_H__
#define __PANGO_COVERAGE_H__

#include <glib-object.h>

#include <pango/pango-version-macros.h>
#include <hb.h>

G_BEGIN_DECLS

/**
 * GdkTextCoverage:
 *
 * A `GdkTextCoverage` structure is a map from Unicode characters
 * to [enum@GdkText.CoverageLevel] values.
 *
 * It is often necessary in GdkText to determine if a particular
 * font can represent a particular character, and also how well
 * it can represent that character. The `GdkTextCoverage` is a data
 * structure that is used to represent that information. It is an
 * opaque structure with no public fields.
 */
typedef struct _PangoCoverage GdkTextCoverage;

/**
 * GdkTextCoverageLevel:
 * @GDK_TEXT_COVERAGE_NONE: The character is not representable with
 *   the font.
 * @GDK_TEXT_COVERAGE_FALLBACK: The character is represented in a
 *   way that may be comprehensible but is not the correct
 *   graphical form. For instance, a Hangul character represented
 *   as a a sequence of Jamos, or a Latin transliteration of a
 *   Cyrillic word.
 * @GDK_TEXT_COVERAGE_APPROXIMATE: The character is represented as
 *   basically the correct graphical form, but with a stylistic
 *   variant inappropriate for the current script.
 * @GDK_TEXT_COVERAGE_EXACT: The character is represented as the
 *   correct graphical form.
 *
 * `GdkTextCoverageLevel` is used to indicate how well a font can
 * represent a particular Unicode character for a particular script.
 *
 * Since 1.44, only %GDK_TEXT_COVERAGE_NONE and %GDK_TEXT_COVERAGE_EXACT
 * will be returned.
 */
typedef enum {
  GDK_TEXT_COVERAGE_NONE,
  GDK_TEXT_COVERAGE_FALLBACK,
  GDK_TEXT_COVERAGE_APPROXIMATE,
  GDK_TEXT_COVERAGE_EXACT
} GdkTextCoverageLevel;

GDK_TEXT_AVAILABLE_IN_ALL
GType gdk_text_coverage_get_type (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextCoverage *    gdk_text_coverage_new     (void);
GDK_TEXT_DEPRECATED_IN_1_52_FOR(g_object_ref)
GdkTextCoverage *    gdk_text_coverage_ref     (GdkTextCoverage      *coverage);
GDK_TEXT_DEPRECATED_IN_1_52_FOR(g_object_unref)
void               gdk_text_coverage_unref   (GdkTextCoverage      *coverage);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextCoverage *    gdk_text_coverage_copy    (GdkTextCoverage      *coverage);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextCoverageLevel gdk_text_coverage_get     (GdkTextCoverage      *coverage,
					   int                 index_);
GDK_TEXT_AVAILABLE_IN_ALL
void               gdk_text_coverage_set     (GdkTextCoverage      *coverage,
					   int                 index_,
					   GdkTextCoverageLevel  level);
GDK_TEXT_DEPRECATED_IN_1_44
void               gdk_text_coverage_max     (GdkTextCoverage      *coverage,
					   GdkTextCoverage      *other);

GDK_TEXT_DEPRECATED_IN_1_44
void           gdk_text_coverage_to_bytes   (GdkTextCoverage  *coverage,
					  guchar        **bytes,
					  int            *n_bytes);
GDK_TEXT_DEPRECATED_IN_1_44
GdkTextCoverage *gdk_text_coverage_from_bytes (guchar         *bytes,
					  int             n_bytes);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextCoverage, g_object_unref)

G_END_DECLS

#endif /* __PANGO_COVERAGE_H__ */
