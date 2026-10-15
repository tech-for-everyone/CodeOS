/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GDK region: GTK6's own region type, replacing cairo's.
 *
 * Upstream GDK takes its region type from cairo -- cairo_region_t and the
 * cairo_region_*() API -- because cairo was the drawing library.  Drawing is
 * QPainter here, so there is no cairo to take them from and GDK owns them
 * outright: GdkRegion is a sorted, non-overlapping rectangle list (the
 * representation cairo used internally) and gdk_region_*() is its API.
 *
 * The functions keep the names of the cairo functions they replace, so the
 * port is a mechanical rename of cairo_region_* -> gdk_region_* and
 * cairo_region_t -> GdkRegion; see PLAN.md "Dependency surgery".  The upside
 * of the region type is that it is pure integer rectangle arithmetic, so it
 * needs nothing from Qt -- the drawing bridge can come later.
 *
 * This header is pulled in from gdktypes.h so that every translation unit that
 * can see GdkRectangle can also see GdkRegion.
 */

#pragma once

#include <gdk/gdktypes.h>

G_BEGIN_DECLS

/* Matches CAIRO_REGION_OVERLAP_IN / _OUT / _PART, which is the order callers
 * already compare against. */
typedef enum
{
  GDK_REGION_OVERLAP_IN,
  GDK_REGION_OVERLAP_OUT,
  GDK_REGION_OVERLAP_PART
} GdkRegionOverlap;

typedef struct _GdkRegion GdkRegion;

GDK_AVAILABLE_IN_ALL
GdkRegion *       gdk_region_create              (void);
GDK_AVAILABLE_IN_ALL
GdkRegion *       gdk_region_create_rectangle    (const GdkRectangle *rect);
GDK_AVAILABLE_IN_ALL
GdkRegion *       gdk_region_copy                (const GdkRegion    *region);
GDK_AVAILABLE_IN_ALL
GdkRegion *       gdk_region_reference           (GdkRegion          *region);
GDK_AVAILABLE_IN_ALL
void              gdk_region_destroy             (GdkRegion          *region);
GDK_AVAILABLE_IN_ALL
gboolean          gdk_region_equal               (const GdkRegion    *region_a,
                                                  const GdkRegion    *region_b);
GDK_AVAILABLE_IN_ALL
void              gdk_region_union               (GdkRegion          *region,
                                                  const GdkRegion    *other);
GDK_AVAILABLE_IN_ALL
void              gdk_region_union_rectangle     (GdkRegion          *region,
                                                  const GdkRectangle *rect);
GDK_AVAILABLE_IN_ALL
void              gdk_region_intersect           (GdkRegion          *region,
                                                  const GdkRegion    *other);
GDK_AVAILABLE_IN_ALL
void              gdk_region_intersect_rectangle (GdkRegion          *region,
                                                  const GdkRectangle *rect);
GDK_AVAILABLE_IN_ALL
void              gdk_region_subtract            (GdkRegion          *region,
                                                  const GdkRegion    *other);
GDK_AVAILABLE_IN_ALL
void              gdk_region_subtract_rectangle  (GdkRegion          *region,
                                                  const GdkRectangle *rect);
GDK_AVAILABLE_IN_ALL
gboolean          gdk_region_is_empty            (const GdkRegion    *region);
GDK_AVAILABLE_IN_ALL
gboolean          gdk_region_is_rectangle        (const GdkRegion    *region);
GDK_AVAILABLE_IN_ALL
GdkRegionOverlap  gdk_region_contains_rectangle  (const GdkRegion    *region,
                                                  const GdkRectangle *rect);
GDK_AVAILABLE_IN_ALL
void              gdk_region_get_extents         (const GdkRegion    *region,
                                                  GdkRectangle       *rect);
GDK_AVAILABLE_IN_ALL
int               gdk_region_num_rectangles      (const GdkRegion    *region);
GDK_AVAILABLE_IN_ALL
void              gdk_region_get_rectangle       (const GdkRegion    *region,
                                                  int                 nth,
                                                  GdkRectangle       *rect);
GDK_AVAILABLE_IN_ALL
void              gdk_region_translate           (GdkRegion          *region,
                                                  int                 dx,
                                                  int                 dy);

/* Moved here from gdkcairoprivate.h: they were named gdk_cairo_region_*()
 * because they take a cairo region, but they are rectangle arithmetic and use
 * nothing from the drawing library. */
GDK_AVAILABLE_IN_ALL
GdkRegion *       gdk_region_scale_grow          (const GdkRegion    *region,
                                                  double              scale_x,
                                                  double              scale_y);
GDK_AVAILABLE_IN_ALL
char *            gdk_region_to_debug_string     (const GdkRegion    *region);
GDK_AVAILABLE_IN_ALL
void              gdk_region_union_affine        (GdkRegion          *region,
                                                  const GdkRegion    *sub,
                                                  float               scale_x,
                                                  float               scale_y,
                                                  float               offset_x,
                                                  float               offset_y);

G_END_DECLS
