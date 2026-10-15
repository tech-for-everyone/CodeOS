/* GdkText
 * pango-utils.c: Utilities for internal functions and modules
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

#ifndef __PANGO_UTILS_H__
#define __PANGO_UTILS_H__

#include <stdio.h>
#include <glib.h>
#include <pango/pango-font.h>

G_BEGIN_DECLS

GDK_TEXT_DEPRECATED
char **  gdk_text_split_file_list (const char *str);

GDK_TEXT_DEPRECATED
char    *gdk_text_trim_string     (const char *str);
GDK_TEXT_DEPRECATED
gint     gdk_text_read_line      (FILE        *stream,
			       GString     *str);
GDK_TEXT_DEPRECATED
gboolean gdk_text_skip_space     (const char **pos);
GDK_TEXT_DEPRECATED
gboolean gdk_text_scan_word      (const char **pos,
			       GString     *out);
GDK_TEXT_DEPRECATED
gboolean gdk_text_scan_string    (const char **pos,
			       GString     *out);
GDK_TEXT_DEPRECATED
gboolean gdk_text_scan_int       (const char **pos,
			       int         *out);

GDK_TEXT_DEPRECATED
gboolean gdk_text_parse_enum     (GType       type,
			       const char *str,
			       int        *value,
			       gboolean    warn,
			       char      **possible_values);

/* Functions for parsing textual representations
 * of GdkTextFontDescription fields. They return TRUE if the input string
 * contains a valid value, which then has been assigned to the corresponding
 * field in the GdkTextFontDescription. If the warn parameter is TRUE,
 * a warning is printed (with g_warning) if the string does not
 * contain a valid value.
 */
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_parse_style   (const char   *str,
			      GdkTextStyle   *style,
			      gboolean      warn);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_parse_variant (const char   *str,
			      GdkTextVariant *variant,
			      gboolean      warn);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_parse_weight  (const char   *str,
			      GdkTextWeight  *weight,
			      gboolean      warn);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_parse_stretch (const char   *str,
			      GdkTextStretch *stretch,
			      gboolean      warn);


/* Hint line position and thickness.
 */
GDK_TEXT_AVAILABLE_IN_1_12
void gdk_text_quantize_line_geometry (int *thickness,
				   int *position);

/* A routine from fribidi that we either wrap or provide ourselves.
 */
GDK_TEXT_AVAILABLE_IN_1_4
guint8 * gdk_text_log2vis_get_embedding_levels (const gchar    *text,
					     int             length,
					     GdkTextDirection *pbase_dir);

/* Unicode characters that are zero-width and should not be rendered
 * normally.
 */
GDK_TEXT_AVAILABLE_IN_1_10
gboolean gdk_text_is_zero_width (gunichar ch) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_find_paragraph_boundary (const char *text,
                                        int         length,
                                        int        *paragraph_delimiter_index,
                                        int        *next_paragraph_start);

/* GdkText version checking */

/* Encode a GdkText version as an integer */
/**
 * GDK_TEXT_VERSION_ENCODE:
 * @major: the major component of the version number
 * @minor: the minor component of the version number
 * @micro: the micro component of the version number
 *
 * This macro encodes the given GdkText version into an integer.  The numbers
 * returned by %GDK_TEXT_VERSION and gdk_text_version() are encoded using this macro.
 * Two encoded version numbers can be compared as integers.
 */
#define GDK_TEXT_VERSION_ENCODE(major, minor, micro) (     \
	  ((major) * 10000)                             \
	+ ((minor) *   100)                             \
	+ ((micro) *     1))

/* Encoded version of GdkText at compile-time */
/**
 * GDK_TEXT_VERSION:
 *
 * The version of GdkText available at compile-time, encoded using GDK_TEXT_VERSION_ENCODE().
 */
/**
 * GDK_TEXT_VERSION_STRING:
 *
 * A string literal containing the version of GdkText available at compile-time.
 */
/**
 * GDK_TEXT_VERSION_MAJOR:
 *
 * The major component of the version of GdkText available at compile-time.
 */
/**
 * GDK_TEXT_VERSION_MINOR:
 *
 * The minor component of the version of GdkText available at compile-time.
 */
/**
 * GDK_TEXT_VERSION_MICRO:
 *
 * The micro component of the version of GdkText available at compile-time.
 */
#define GDK_TEXT_VERSION GDK_TEXT_VERSION_ENCODE(     \
	GDK_TEXT_VERSION_MAJOR,                    \
	GDK_TEXT_VERSION_MINOR,                    \
	GDK_TEXT_VERSION_MICRO)

/* Check that compile-time GdkText is as new as required */
/**
 * GDK_TEXT_VERSION_CHECK:
 * @major: the major component of the version number
 * @minor: the minor component of the version number
 * @micro: the micro component of the version number
 *
 * Checks that the version of GdkText available at compile-time is not older than
 * the provided version number.
 */
#define GDK_TEXT_VERSION_CHECK(major,minor,micro)    \
	(GDK_TEXT_VERSION >= GDK_TEXT_VERSION_ENCODE(major,minor,micro))


/* Return encoded version of GdkText at run-time */
GDK_TEXT_AVAILABLE_IN_1_16
int gdk_text_version (void) G_GNUC_CONST;

/* Return run-time GdkText version as an string */
GDK_TEXT_AVAILABLE_IN_1_16
const char * gdk_text_version_string (void) G_GNUC_CONST;

/* Check that run-time GdkText is as new as required */
GDK_TEXT_AVAILABLE_IN_1_16
const char * gdk_text_version_check (int required_major,
                                  int required_minor,
                                  int required_micro) G_GNUC_CONST;

G_END_DECLS

#endif /* __PANGO_UTILS_H__ */
