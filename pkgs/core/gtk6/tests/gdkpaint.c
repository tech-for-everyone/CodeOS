/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile-only probe for GDK's drawing API (gdk/gdkpaint.h).
 *
 * gdkpaint.h is the cairo replacement: it must be usable from the GTK4 C
 * sources, which reach it through <gdk/gdktypes.h>, without <cairo.h> on the
 * include path.  This probe names the types the renamed sources name and
 * calls a spread of the functions, so that a signature change or a missing
 * declaration is a build failure rather than a link error much later.  It is
 * never linked or run (see GTK4_PROBES in the Makefile).
 *
 * The negative control is the same probe without <gdk/gdktypes.h>: on this
 * build the symbol cairo_t has no declaration at all.
 */

#include <gdk/gdktypes.h>
#include <gdk/gdkrectangle.h>

static void
paint_path (GdkPainter *cr)
{
  double x, y;

  gdk_paint_move_to (cr, 0, 0);
  gdk_paint_line_to (cr, 10, 10);
  gdk_paint_curve_to (cr, 1, 2, 3, 4, 5, 6);
  gdk_paint_arc (cr, 5, 5, 4, 0, 3.14159);
  gdk_paint_arc_negative (cr, 5, 5, 4, 3.14159, 0);
  gdk_paint_rectangle (cr, 0, 0, 8, 8);
  gdk_paint_close_path (cr);
  gdk_paint_new_sub_path (cr);
  gdk_paint_get_current_point (cr, &x, &y);
  (void) gdk_paint_has_current_point (cr);
  gdk_paint_new_path (cr);
}

static void
paint_state (GdkPainter *cr)
{
  GdkPaintMatrix m;

  gdk_paint_set_source_rgba (cr, 1.0, 0.0, 0.0, 1.0);
  gdk_paint_set_source_rgb (cr, 1.0, 0.0, 0.0);
  gdk_paint_set_line_width (cr, 2.0);
  gdk_paint_set_line_cap (cr, GDK_PAINT_LINE_CAP_ROUND);
  gdk_paint_set_line_join (cr, GDK_PAINT_LINE_JOIN_ROUND);
  gdk_paint_set_miter_limit (cr, 4.0);
  gdk_paint_set_fill_rule (cr, GDK_PAINT_FILL_RULE_EVEN_ODD);
  gdk_paint_set_operator (cr, GDK_PAINT_OPERATOR_OVER);

  gdk_paint_save (cr);
  gdk_paint_translate (cr, 1, 2);
  gdk_paint_scale (cr, 2, 2);
  gdk_paint_matrix_init_identity (&m);
  gdk_paint_transform (cr, &m);
  gdk_paint_set_matrix (cr, &m);
  gdk_paint_get_matrix (cr, &m);
  gdk_paint_restore (cr);

  gdk_paint_fill (cr);
  gdk_paint_stroke (cr);
  gdk_paint_paint (cr);
  gdk_paint_paint_with_alpha (cr, 0.5);
  (void) gdk_paint_get_tolerance (cr);
  (void) gdk_paint_status (cr);
  (void) gdk_paint_status_to_string (GDK_PAINT_STATUS_SUCCESS);
}

static void
paint_surfaces (GdkPainter *cr)
{
  GdkPaintSurface *surface;
  GdkPaintPattern *pattern;
  GdkPaintPath    *path;
  double           x1, y1, x2, y2;

  surface = gdk_paint_image_surface_create (GDK_PAINT_FORMAT_ARGB32, 16, 16);
  gdk_paint_set_source_surface (cr, surface, 0, 0);
  gdk_paint_mask_surface (cr, surface, 0, 0);
  (void) gdk_paint_image_surface_get_width (surface);
  (void) gdk_paint_image_surface_get_height (surface);
  (void) gdk_paint_image_surface_get_stride (surface);
  (void) gdk_paint_image_surface_get_format (surface);
  (void) gdk_paint_image_surface_get_data (surface);
  (void) gdk_paint_surface_get_content (surface);
  (void) gdk_paint_surface_get_type (surface);
  (void) gdk_paint_surface_status (surface);
  gdk_paint_surface_mark_dirty (surface);
  gdk_paint_surface_flush (surface);
  gdk_paint_surface_destroy (surface);

  pattern = gdk_paint_pattern_create_linear (0, 0, 1, 1);
  gdk_paint_pattern_add_color_stop_rgba (pattern, 0.0, 0, 0, 0, 1);
  gdk_paint_pattern_set_extend (pattern, GDK_PAINT_EXTEND_PAD);
  gdk_paint_pattern_set_filter (pattern, GDK_PAINT_FILTER_BILINEAR);
  gdk_paint_set_source (cr, pattern);
  gdk_paint_pattern_destroy (pattern);
  (void) gdk_paint_pattern_create_radial (0, 0, 1, 0, 0, 2);

  path = gdk_paint_copy_path (cr);
  gdk_paint_append_path (cr, path);
  gdk_paint_path_destroy (path);

  gdk_paint_clip (cr);
  gdk_paint_clip_extents (cr, &x1, &y1, &x2, &y2);
  gdk_paint_reset_clip (cr);
  gdk_paint_push_group (cr);
  gdk_paint_pop_group_to_source (cr);
  gdk_paint_mask (cr, NULL);
  (void) gdk_paint_get_target (cr);
  (void) gdk_paint_get_group_target (cr);
  (void) gdk_paint_get_source (cr);
  gdk_paint_user_to_device (cr, &x1, &y1);
  gdk_paint_user_to_device_distance (cr, &x1, &y1);
}

/* The GType accessors that replaced cairo-gobject's CAIRO_GOBJECT_TYPE_*. */
int
gdkpaint_probe (void)
{
  GType types[] = {
    GDK_PAINT_TYPE_ANTIALIAS,
    GDK_PAINT_TYPE_HINT_STYLE,
    GDK_PAINT_TYPE_HINT_METRICS,
    GDK_PAINT_TYPE_SUBPIXEL_ORDER,
    GDK_TYPE_RECTANGLE,
    GDK_TYPE_REGION,
  };

  paint_path (NULL);
  paint_state (NULL);
  paint_surfaces (NULL);

  return (int) (types[0] + GDK_PAINT_HAS_PNG_FUNCTIONS);
}
