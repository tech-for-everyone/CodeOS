/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile-only probe for the region half of the cairo surgery.
 *
 * Upstream GDK's region type is cairo's: cairo_region_t and the
 * cairo_region_*() API.  With cairo gone, GDK owns them (gdk/gdkregion.h,
 * GdkRegion, gdk_region_*()).  Like tests/gdktypes.c this is a negative
 * control: the CodeOS toolchain has no <cairo.h> on its include path, so if
 * anyone reintroduces it (or reintroduces a cairo_region_t) this stops
 * compiling.
 *
 * The array pins every signature -- the probe is compiled with -fsyntax-only,
 * never linked, so referring to the functions is enough.
 */

#include <gdk/gdkregion.h>

_Static_assert (GDK_REGION_OVERLAP_IN   == 0 &&
                GDK_REGION_OVERLAP_OUT  == 1 &&
                GDK_REGION_OVERLAP_PART == 2,
                "GdkRegionOverlap must keep cairo's IN/OUT/PART order");

void *gtk6_gdkregion_probe[] = {
  (void *) gdk_region_create,
  (void *) gdk_region_create_rectangle,
  (void *) gdk_region_copy,
  (void *) gdk_region_reference,
  (void *) gdk_region_destroy,
  (void *) gdk_region_equal,
  (void *) gdk_region_union,
  (void *) gdk_region_union_rectangle,
  (void *) gdk_region_intersect,
  (void *) gdk_region_intersect_rectangle,
  (void *) gdk_region_subtract,
  (void *) gdk_region_subtract_rectangle,
  (void *) gdk_region_is_empty,
  (void *) gdk_region_is_rectangle,
  (void *) gdk_region_contains_rectangle,
  (void *) gdk_region_get_extents,
  (void *) gdk_region_num_rectangles,
  (void *) gdk_region_get_rectangle,
  (void *) gdk_region_translate,
  (void *) gdk_region_scale_grow,
  (void *) gdk_region_to_debug_string,
  (void *) gdk_region_union_affine,
};
