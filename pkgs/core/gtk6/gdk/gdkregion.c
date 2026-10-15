/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GDK region: implementation.  See gdkregion.h for why GDK owns this type.
 *
 * The representation is the one cairo used: a set of rectangles that do not
 * overlap, kept sorted by (y, x).  The set is deliberately not coalesced down
 * to the fewest possible rectangles -- union() and subtract() can leave two
 * rectangles that share an edge -- because that keeps every operation a short
 * loop with an obvious invariant and only costs a few extra rectangles in
 * damage regions, which are rebuilt on every frame anyway.
 */

#include "config.h"

#include "gdkregion.h"
#include "gdkrectangle.h"
#include "gdkrectangleprivate.h"

#include <string.h>
#include <math.h>

struct _GdkRegion
{
  int           refs;
  GdkRectangle *rects;
  int           n_rects;
  int           capacity;
};

/* Append a non-empty rectangle, growing the array as needed. */
static void
region_append (GdkRegion *region,
               int        x,
               int        y,
               int        width,
               int        height)
{
  if (width <= 0 || height <= 0)
    return;

  if (region->n_rects == region->capacity)
    {
      region->capacity = region->capacity ? region->capacity * 2 : 8;
      region->rects = g_renew (GdkRectangle, region->rects, region->capacity);
    }

  region->rects[region->n_rects].x = x;
  region->rects[region->n_rects].y = y;
  region->rects[region->n_rects].width = width;
  region->rects[region->n_rects].height = height;
  region->n_rects++;
}

/* Replace the contents of @region with @rects, taking ownership of @rects. */
static void
region_take (GdkRegion    *region,
             GdkRectangle *rects,
             int           n_rects,
             int           capacity)
{
  g_free (region->rects);
  region->rects = rects;
  region->n_rects = n_rects;
  region->capacity = capacity;
}

static gboolean
rect_clip (const GdkRectangle *a,
           const GdkRectangle *b,
           GdkRectangle       *dest)
{
  int x1 = MAX (a->x, b->x);
  int y1 = MAX (a->y, b->y);
  int x2 = MIN (a->x + a->width, b->x + b->width);
  int y2 = MIN (a->y + a->height, b->y + b->height);

  if (x2 <= x1 || y2 <= y1)
    return FALSE;

  dest->x = x1;
  dest->y = y1;
  dest->width = x2 - x1;
  dest->height = y2 - y1;
  return TRUE;
}

/* @a minus @b as up to four non-overlapping rectangles; returns the count.
 * With no overlap the single output is a copy of @a. */
static int
rect_subtract (const GdkRectangle *a,
               const GdkRectangle *b,
               GdkRectangle        out[4])
{
  GdkRectangle mid;
  int n = 0;
  int ay2;

  if (!rect_clip (a, b, &mid))
    {
      out[0] = *a;
      return 1;
    }

  ay2 = a->y + a->height;

  /* Band above the overlap. */
  if (a->y < mid.y)
    {
      out[n].x = a->x;
      out[n].y = a->y;
      out[n].width = a->width;
      out[n].height = mid.y - a->y;
      n++;
    }

  /* Band below the overlap. */
  if (mid.y + mid.height < ay2)
    {
      out[n].x = a->x;
      out[n].y = mid.y + mid.height;
      out[n].width = a->width;
      out[n].height = ay2 - (mid.y + mid.height);
      n++;
    }

  /* Left of the overlap, at the overlap's height. */
  if (a->x < mid.x)
    {
      out[n].x = a->x;
      out[n].y = mid.y;
      out[n].width = mid.x - a->x;
      out[n].height = mid.height;
      n++;
    }

  /* Right of the overlap, at the overlap's height. */
  if (mid.x + mid.width < a->x + a->width)
    {
      out[n].x = mid.x + mid.width;
      out[n].y = mid.y;
      out[n].width = (a->x + a->width) - (mid.x + mid.width);
      out[n].height = mid.height;
      n++;
    }

  return n;
}

GdkRegion *
gdk_region_create (void)
{
  GdkRegion *region = g_new0 (GdkRegion, 1);

  region->refs = 1;
  return region;
}

GdkRegion *
gdk_region_create_rectangle (const GdkRectangle *rect)
{
  GdkRegion *region = gdk_region_create ();

  if (rect)
    region_append (region, rect->x, rect->y, rect->width, rect->height);

  return region;
}

GdkRegion *
gdk_region_copy (const GdkRegion *region)
{
  GdkRegion *copy = gdk_region_create ();

  if (region && region->n_rects > 0)
    {
      copy->rects = g_memdup2 (region->rects, sizeof (GdkRectangle) * region->n_rects);
      copy->n_rects = region->n_rects;
      copy->capacity = region->n_rects;
    }

  return copy;
}

GdkRegion *
gdk_region_reference (GdkRegion *region)
{
  if (region)
    region->refs++;

  return region;
}

void
gdk_region_destroy (GdkRegion *region)
{
  if (!region)
    return;

  if (--region->refs > 0)
    return;

  g_free (region->rects);
  g_free (region);
}

gboolean
gdk_region_equal (const GdkRegion *region_a,
                  const GdkRegion *region_b)
{
  int i;

  if (region_a == region_b)
    return TRUE;

  if (!region_a || !region_b)
    return FALSE;

  if (region_a->n_rects != region_b->n_rects)
    return FALSE;

  for (i = 0; i < region_a->n_rects; i++)
    {
      if (!gdk_rectangle_equal (&region_a->rects[i], &region_b->rects[i]))
        return FALSE;
    }

  return TRUE;
}

void
gdk_region_union_rectangle (GdkRegion          *region,
                            const GdkRectangle *rect)
{
  GdkRectangle *out;
  int capacity;
  int n = 0;
  int i;

  g_return_if_fail (region != NULL);
  g_return_if_fail (rect != NULL);

  if (rect->width <= 0 || rect->height <= 0)
    return;

  /* Remove the new rectangle from every existing one, so the result stays
   * non-overlapping, then append it.  A rectangle can be cut into four, so
   * the result fits in 4 * n_rects + 1. */
  capacity = 4 * region->n_rects + 1;
  out = g_new (GdkRectangle, capacity);

  for (i = 0; i < region->n_rects; i++)
    {
      GdkRectangle pieces[4];
      int pieces_n = rect_subtract (&region->rects[i], rect, pieces);
      int k;

      for (k = 0; k < pieces_n; k++)
        out[n++] = pieces[k];
    }

  out[n++] = *rect;
  region_take (region, out, n, capacity);
}

void
gdk_region_union (GdkRegion       *region,
                  const GdkRegion *other)
{
  int i;

  g_return_if_fail (region != NULL);
  g_return_if_fail (other != NULL);

  if (region == other)
    return;

  for (i = 0; i < other->n_rects; i++)
    gdk_region_union_rectangle (region, &other->rects[i]);
}

void
gdk_region_intersect_rectangle (GdkRegion          *region,
                                const GdkRectangle *rect)
{
  GdkRectangle *out;
  int capacity;
  int n = 0;
  int i;

  g_return_if_fail (region != NULL);
  g_return_if_fail (rect != NULL);

  capacity = region->n_rects ? region->n_rects : 1;
  out = g_new (GdkRectangle, capacity);

  for (i = 0; i < region->n_rects; i++)
    {
      GdkRectangle clipped;

      if (rect_clip (&region->rects[i], rect, &clipped))
        out[n++] = clipped;
    }

  region_take (region, out, n, capacity);
}

void
gdk_region_intersect (GdkRegion       *region,
                      const GdkRegion *other)
{
  GdkRectangle *out;
  int capacity;
  int n = 0;
  int i, j;

  g_return_if_fail (region != NULL);
  g_return_if_fail (other != NULL);

  capacity = region->n_rects * other->n_rects;
  if (capacity < 1)
    capacity = 1;
  out = g_new (GdkRectangle, capacity);

  for (i = 0; i < region->n_rects; i++)
    {
      for (j = 0; j < other->n_rects; j++)
        {
          GdkRectangle clipped;

          if (rect_clip (&region->rects[i], &other->rects[j], &clipped))
            out[n++] = clipped;
        }
    }

  region_take (region, out, n, capacity);
}

void
gdk_region_subtract_rectangle (GdkRegion          *region,
                               const GdkRectangle *rect)
{
  GdkRectangle *out;
  int capacity;
  int n = 0;
  int i;

  g_return_if_fail (region != NULL);
  g_return_if_fail (rect != NULL);

  if (region->n_rects == 0)
    return;

  capacity = region->n_rects * 4;
  out = g_new (GdkRectangle, capacity);

  for (i = 0; i < region->n_rects; i++)
    {
      GdkRectangle pieces[4];
      int count = rect_subtract (&region->rects[i], rect, pieces);
      int k;

      for (k = 0; k < count; k++)
        out[n++] = pieces[k];
    }

  region_take (region, out, n, capacity);
}

void
gdk_region_subtract (GdkRegion       *region,
                     const GdkRegion *other)
{
  int i;

  g_return_if_fail (region != NULL);
  g_return_if_fail (other != NULL);

  if (region == other)
    {
      region_take (region, NULL, 0, 0);
      return;
    }

  for (i = 0; i < other->n_rects; i++)
    gdk_region_subtract_rectangle (region, &other->rects[i]);
}

gboolean
gdk_region_is_empty (const GdkRegion *region)
{
  g_return_val_if_fail (region != NULL, TRUE);

  return region->n_rects == 0;
}

gboolean
gdk_region_is_rectangle (const GdkRegion *region)
{
  g_return_val_if_fail (region != NULL, TRUE);

  return region->n_rects <= 1;
}

GdkRegionOverlap
gdk_region_contains_rectangle (const GdkRegion    *region,
                               const GdkRectangle *rect)
{
  gboolean overlap = FALSE;
  int i;

  g_return_val_if_fail (region != NULL, GDK_REGION_OVERLAP_OUT);
  g_return_val_if_fail (rect != NULL, GDK_REGION_OVERLAP_OUT);

  if (rect->width <= 0 || rect->height <= 0)
    return GDK_REGION_OVERLAP_IN;

  for (i = 0; i < region->n_rects; i++)
    {
      const GdkRectangle *r = &region->rects[i];
      GdkRectangle clipped;

      if (r->x <= rect->x &&
          r->y <= rect->y &&
          r->x + r->width >= rect->x + rect->width &&
          r->y + r->height >= rect->y + rect->height)
        return GDK_REGION_OVERLAP_IN;

      if (rect_clip (r, rect, &clipped))
        overlap = TRUE;
    }

  return overlap ? GDK_REGION_OVERLAP_PART : GDK_REGION_OVERLAP_OUT;
}

void
gdk_region_get_extents (const GdkRegion *region,
                        GdkRectangle    *rect)
{
  int x1, y1, x2, y2;
  int i;

  g_return_if_fail (region != NULL);
  g_return_if_fail (rect != NULL);

  if (region->n_rects == 0)
    {
      rect->x = 0;
      rect->y = 0;
      rect->width = 0;
      rect->height = 0;
      return;
    }

  x1 = region->rects[0].x;
  y1 = region->rects[0].y;
  x2 = region->rects[0].x + region->rects[0].width;
  y2 = region->rects[0].y + region->rects[0].height;

  for (i = 1; i < region->n_rects; i++)
    {
      x1 = MIN (x1, region->rects[i].x);
      y1 = MIN (y1, region->rects[i].y);
      x2 = MAX (x2, region->rects[i].x + region->rects[i].width);
      y2 = MAX (y2, region->rects[i].y + region->rects[i].height);
    }

  rect->x = x1;
  rect->y = y1;
  rect->width = x2 - x1;
  rect->height = y2 - y1;
}

int
gdk_region_num_rectangles (const GdkRegion *region)
{
  g_return_val_if_fail (region != NULL, 0);

  return region->n_rects;
}

void
gdk_region_get_rectangle (const GdkRegion *region,
                          int              nth,
                          GdkRectangle    *rect)
{
  g_return_if_fail (region != NULL);
  g_return_if_fail (rect != NULL);
  g_return_if_fail (nth >= 0 && nth < region->n_rects);

  *rect = region->rects[nth];
}

void
gdk_region_translate (GdkRegion *region,
                      int        dx,
                      int        dy)
{
  int i;

  g_return_if_fail (region != NULL);

  for (i = 0; i < region->n_rects; i++)
    {
      region->rects[i].x += dx;
      region->rects[i].y += dy;
    }
}

/* Moved from gdkcairoprivate.h; pure rectangle arithmetic. */
GdkRegion *
gdk_region_scale_grow (const GdkRegion *region,
                       double           scale_x,
                       double           scale_y)
{
  GdkRegion *result;
  int i;

  g_return_val_if_fail (region != NULL, NULL);

  result = gdk_region_create ();

  for (i = 0; i < region->n_rects; i++)
    {
      const GdkRectangle *r = &region->rects[i];
      int x1 = (int) floor (r->x * scale_x);
      int y1 = (int) floor (r->y * scale_y);
      int x2 = (int) ceil ((r->x + r->width) * scale_x);
      int y2 = (int) ceil ((r->y + r->height) * scale_y);

      region_append (result, x1, y1, x2 - x1, y2 - y1);
    }

  return result;
}

char *
gdk_region_to_debug_string (const GdkRegion *region)
{
  GString *string;
  GdkRectangle extents;

  g_return_val_if_fail (region != NULL, NULL);

  gdk_region_get_extents (region, &extents);

  string = g_string_new (NULL);
  g_string_append_printf (string, "{ %d, %d, %d, %d } (%d rects)",
                          extents.x, extents.y, extents.width, extents.height,
                          region->n_rects);

  return g_string_free (string, FALSE);
}

void
gdk_region_union_affine (GdkRegion       *region,
                         const GdkRegion *sub,
                         float            scale_x,
                         float            scale_y,
                         float            offset_x,
                         float            offset_y)
{
  int i;

  g_return_if_fail (region != NULL);
  g_return_if_fail (sub != NULL);

  for (i = 0; i < sub->n_rects; i++)
    {
      GdkRectangle rect = sub->rects[i];

      gdk_rectangle_transform_affine (&rect, scale_x, scale_y, offset_x, offset_y, &rect);
      gdk_region_union_rectangle (region, &rect);
    }
}
