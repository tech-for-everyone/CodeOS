/* GdkText
 * pango-gravity.h: Gravity routines
 *
 * Copyright (C) 2006, 2007 Red Hat Software
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

#ifndef __PANGO_GRAVITY_H__
#define __PANGO_GRAVITY_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * GdkTextGravity:
 * @GDK_TEXT_GRAVITY_SOUTH: Glyphs stand upright (default) <img align="right" valign="center" src="m-south.png">
 * @GDK_TEXT_GRAVITY_EAST: Glyphs are rotated 90 degrees counter-clockwise. <img align="right" valign="center" src="m-east.png">
 * @GDK_TEXT_GRAVITY_NORTH: Glyphs are upside-down. <img align="right" valign="cener" src="m-north.png">
 * @GDK_TEXT_GRAVITY_WEST: Glyphs are rotated 90 degrees clockwise. <img align="right" valign="center" src="m-west.png">
 * @GDK_TEXT_GRAVITY_AUTO: Gravity is resolved from the context matrix
 *
 * `GdkTextGravity` represents the orientation of glyphs in a segment
 * of text.
 *
 * This is useful when rendering vertical text layouts. In those situations,
 * the layout is rotated using a non-identity [struct@GdkText.Matrix], and then
 * glyph orientation is controlled using `GdkTextGravity`.
 *
 * Not every value in this enumeration makes sense for every usage of
 * `GdkTextGravity`; for example, %GDK_TEXT_GRAVITY_AUTO only can be passed to
 * [method@GdkText.Context.set_base_gravity] and can only be returned by
 * [method@GdkText.Context.get_base_gravity].
 *
 * See also: [enum@GdkText.GravityHint]
 *
 * Since: 1.16
 */
typedef enum {
  GDK_TEXT_GRAVITY_SOUTH,
  GDK_TEXT_GRAVITY_EAST,
  GDK_TEXT_GRAVITY_NORTH,
  GDK_TEXT_GRAVITY_WEST,
  GDK_TEXT_GRAVITY_AUTO
} GdkTextGravity;

/**
 * GdkTextGravityHint:
 * @GDK_TEXT_GRAVITY_HINT_NATURAL: scripts will take their natural gravity based
 *   on the base gravity and the script.  This is the default.
 * @GDK_TEXT_GRAVITY_HINT_STRONG: always use the base gravity set, regardless of
 *   the script.
 * @GDK_TEXT_GRAVITY_HINT_LINE: for scripts not in their natural direction (eg.
 *   Latin in East gravity), choose per-script gravity such that every script
 *   respects the line progression. This means, Latin and Arabic will take
 *   opposite gravities and both flow top-to-bottom for example.
 *
 * `GdkTextGravityHint` defines how horizontal scripts should behave in a
 * vertical context.
 *
 * That is, English excerpts in a vertical paragraph for example.
 *
 * See also [enum@GdkText.Gravity]
 *
 * Since: 1.16
 */
typedef enum {
  GDK_TEXT_GRAVITY_HINT_NATURAL,
  GDK_TEXT_GRAVITY_HINT_STRONG,
  GDK_TEXT_GRAVITY_HINT_LINE
} GdkTextGravityHint;

/**
 * GDK_TEXT_GRAVITY_IS_VERTICAL:
 * @gravity: the `GdkTextGravity` to check
 *
 * Whether a `GdkTextGravity` represents vertical writing directions.
 *
 * Returns: %TRUE if @gravity is %GDK_TEXT_GRAVITY_EAST or %GDK_TEXT_GRAVITY_WEST,
 *   %FALSE otherwise.
 *
 * Since: 1.16
 */
#define GDK_TEXT_GRAVITY_IS_VERTICAL(gravity) \
	((gravity) == GDK_TEXT_GRAVITY_EAST || (gravity) == GDK_TEXT_GRAVITY_WEST)

/**
 * GDK_TEXT_GRAVITY_IS_IMPROPER:
 * @gravity: the `GdkTextGravity` to check
 *
 * Whether a `GdkTextGravity` represents a gravity that results in reversal
 * of text direction.
 *
 * Returns: %TRUE if @gravity is %GDK_TEXT_GRAVITY_WEST or %GDK_TEXT_GRAVITY_NORTH,
 *   %FALSE otherwise.
 *
 * Since: 1.32
 */
#define GDK_TEXT_GRAVITY_IS_IMPROPER(gravity) \
	((gravity) == GDK_TEXT_GRAVITY_WEST || (gravity) == GDK_TEXT_GRAVITY_NORTH)

#include <pango/pango-matrix.h>
#include <pango/pango-script.h>

GDK_TEXT_AVAILABLE_IN_1_16
double       gdk_text_gravity_to_rotation    (GdkTextGravity       gravity) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextGravity gdk_text_gravity_get_for_matrix (const GdkTextMatrix *matrix) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextGravity gdk_text_gravity_get_for_script (GdkTextScript        script,
					   GdkTextGravity       base_gravity,
					   GdkTextGravityHint   hint) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_1_26
GdkTextGravity gdk_text_gravity_get_for_script_and_width
					  (GdkTextScript        script,
					   gboolean           wide,
					   GdkTextGravity       base_gravity,
					   GdkTextGravityHint   hint) G_GNUC_CONST;


G_END_DECLS

#endif /* __PANGO_GRAVITY_H__ */
