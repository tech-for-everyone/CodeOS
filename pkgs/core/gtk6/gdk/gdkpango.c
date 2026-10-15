/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2000 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gdkpango.h"

#include <glib/gi18n-lib.h>

#include <math.h>
#include <pango/pangocairo.h>


/* Get a clip region to draw only part of a layout. index_ranges
 * contains alternating range starts/stops. The region is the
 * region which contains the given ranges, i.e. if you draw with the
 * region as clip, only the given ranges are drawn.
 */
static GdkRegion*
layout_iter_get_line_clip_region (GdkTextLayoutIter *iter,
				  int              x_origin,
				  int              y_origin,
				  const int       *index_ranges,
				  int              n_ranges)
{
  GdkTextLayoutLine *line;
  GdkRegion *clip_region;
  GdkTextRectangle logical_rect;
  int baseline;
  int i;

  line = gdk_text_layout_iter_get_line_readonly (iter);

  clip_region = gdk_region_create ();

  gdk_text_layout_iter_get_line_extents (iter, NULL, &logical_rect);
  baseline = gdk_text_layout_iter_get_baseline (iter);

  i = 0;
  while (i < n_ranges)
    {
      int *pixel_ranges = NULL;
      int n_pixel_ranges = 0;
      int j;

      /* Note that get_x_ranges returns layout coordinates
       */
      if (index_ranges[i*2+1] >= gdk_text_layout_line_get_start_index (line) &&
	  index_ranges[i*2] < gdk_text_layout_line_get_start_index (line) + gdk_text_layout_line_get_length (line))
	gdk_text_layout_line_get_x_ranges (line,
					index_ranges[i*2],
					index_ranges[i*2+1],
					&pixel_ranges, &n_pixel_ranges);

      for (j = 0; j < n_pixel_ranges; j++)
        {
          GdkRectangle rect;
	  int x_off, y_off;

          x_off = GDK_TEXT_PIXELS (pixel_ranges[2*j] - logical_rect.x);
	  y_off = GDK_TEXT_PIXELS (baseline - logical_rect.y);

          rect.x = x_origin + x_off;
          rect.y = y_origin - y_off;
          rect.width = GDK_TEXT_PIXELS (pixel_ranges[2*j + 1] - logical_rect.x) - x_off;
          rect.height = GDK_TEXT_PIXELS (baseline - logical_rect.y + logical_rect.height) - y_off;

          gdk_region_union_rectangle (clip_region, &rect);
        }

      g_free (pixel_ranges);
      ++i;
    }
  return clip_region;
}

/**
 * gdk_text_layout_line_get_clip_region: (skip)
 * @line: a `GdkTextLayoutLine`
 * @x_origin: X pixel where you intend to draw the layout line with this clip
 * @y_origin: baseline pixel where you intend to draw the layout line with this clip
 * @index_ranges: (array): array of byte indexes into the layout, where even
 *   members of array are start indexes and odd elements are end indexes
 * @n_ranges: number of ranges in @index_ranges, i.e. half the size of @index_ranges
 *
 * Obtains a clip region which contains the areas where the given
 * ranges of text would be drawn.
 *
 * @x_origin and @y_origin are the top left position of the layout.
 * @index_ranges should contain ranges of bytes in the layout’s text.
 * The clip region will include space to the left or right of the line
 * (to the layout bounding box) if you have indexes above or below the
 * indexes contained inside the line. This is to draw the selection all
 * the way to the side of the layout. However, the clip region is in line
 * coordinates, not layout coordinates.
 *
 * Note that the regions returned correspond to logical extents of the text
 * ranges, not ink extents. So the drawn line may in fact touch areas out of
 * the clip region.  The clip region is mainly useful for highlightling parts
 * of text, such as when text is selected.
 *
 * Returns: a clip region containing the given ranges
 */
GdkRegion*
gdk_text_layout_line_get_clip_region (GdkTextLayoutLine *line,
                                       int              x_origin,
                                       int              y_origin,
                                       const int       *index_ranges,
                                       int              n_ranges)
{
  GdkRegion *clip_region;
  GdkTextLayoutIter *iter;

  g_return_val_if_fail (line != NULL, NULL);
  g_return_val_if_fail (index_ranges != NULL, NULL);

  iter = gdk_text_layout_get_iter (line->layout);
  while (gdk_text_layout_iter_get_line_readonly (iter) != line)
    gdk_text_layout_iter_next_line (iter);

  clip_region = layout_iter_get_line_clip_region(iter, x_origin, y_origin, index_ranges, n_ranges);

  gdk_text_layout_iter_free (iter);

  return clip_region;
}

/**
 * gdk_text_layout_get_clip_region: (skip)
 * @layout: a `GdkTextLayout`
 * @x_origin: X pixel where you intend to draw the layout with this clip
 * @y_origin: Y pixel where you intend to draw the layout with this clip
 * @index_ranges: array of byte indexes into the layout, where even members of array are start indexes and odd elements are end indexes
 * @n_ranges: number of ranges in @index_ranges, i.e. half the size of @index_ranges
 *
 * Obtains a clip region which contains the areas where the given ranges
 * of text would be drawn.
 *
 * @x_origin and @y_origin are the top left point to center the layout.
 * @index_ranges should contain ranges of bytes in the layout’s text.
 *
 * Note that the regions returned correspond to logical extents of the text
 * ranges, not ink extents. So the drawn layout may in fact touch areas out of
 * the clip region.  The clip region is mainly useful for highlightling parts
 * of text, such as when text is selected.
 *
 * Returns: a clip region containing the given ranges
 */
GdkRegion*
gdk_text_layout_get_clip_region (GdkTextLayout *layout,
                                  int          x_origin,
                                  int          y_origin,
                                  const int   *index_ranges,
                                  int          n_ranges)
{
  GdkTextLayoutIter *iter;
  GdkRegion *clip_region;

  g_return_val_if_fail (GDK_TEXT_IS_LAYOUT (layout), NULL);
  g_return_val_if_fail (index_ranges != NULL, NULL);

  clip_region = gdk_region_create ();

  iter = gdk_text_layout_get_iter (layout);

  do
    {
      GdkTextRectangle logical_rect;
      GdkRegion *line_region;
      int baseline;

      gdk_text_layout_iter_get_line_extents (iter, NULL, &logical_rect);
      baseline = gdk_text_layout_iter_get_baseline (iter);

      line_region = layout_iter_get_line_clip_region(iter,
						     x_origin + GDK_TEXT_PIXELS (logical_rect.x),
						     y_origin + GDK_TEXT_PIXELS (baseline),
						     index_ranges,
						     n_ranges);

      gdk_region_union (clip_region, line_region);
      gdk_region_destroy (line_region);
    }
  while (gdk_text_layout_iter_next_line (iter));

  gdk_text_layout_iter_free (iter);

  return clip_region;
}
