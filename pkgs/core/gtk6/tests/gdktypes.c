/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile-only probe for the cairo/pango surgery on gdk/gdktypes.h.
 *
 * gdktypes.h is the root of the dependency graph: every GDK, GSK and GTK
 * source includes it, and upstream it pulled in <cairo.h> (for
 * cairo_rectangle_int_t) and <pango/pango.h> (for nothing it used directly).
 * The CodeOS toolchain has neither header on its include path, so this is a
 * genuine negative control: if anyone reintroduces either #include, this
 * stops compiling.
 *
 * It also asserts the one fact the replacement relies on — that GDK's own
 * GdkRectangle is still layout-identical to cairo_rectangle_int_t.
 */

#include <gdk/gdktypes.h>

_Static_assert (sizeof (GdkRectangle) == 4 * sizeof (int),
                "GdkRectangle must stay layout-identical to cairo_rectangle_int_t");

int
gtk6_gdktypes_probe (GdkRectangle *rect)
{
  rect->x = 0;
  rect->y = 0;
  rect->width = 1;
  rect->height = 1;
  return rect->width + rect->height;
}
