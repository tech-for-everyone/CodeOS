/*
 * Copyright © 2020 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */

#include "config.h"

#include "gskpathprivate.h"

#include "gskcurveprivate.h"
#include "gskpathbuilder.h"
#include "gskpathpoint.h"
#include "gskcontourprivate.h"

static void
skip_whitespace (const char **p)
{
  while (g_ascii_isspace (**p))
    (*p)++;
}

static void
skip_optional_comma (const char **p)
{
  skip_whitespace (p);
  if (**p == ',')
    (*p)++;
}

static gboolean
parse_number (const char **p,
              double      *c)
{
  char *e;
  *c = g_ascii_strtod (*p, &e);
  if (e == *p)
    return FALSE;
  *p = e;
  skip_optional_comma (p);
  return TRUE;
}

static gboolean
parse_coordinate (const char **p,
                  double      *c)
{
  return parse_number (p, c);
}

static gboolean
parse_coordinate_pair (const char **p,
                       double      *x,
                       double      *y)
{
  double xx, yy;
  const char *o = *p;

  if (!parse_coordinate (p, &xx))
    {
      *p = o;
      return FALSE;
    }
  if (!parse_coordinate (p, &yy))
    {
      *p = o;
      return FALSE;
    }

  *x = xx;
  *y = yy;

  return TRUE;
}

static gboolean
parse_positive_number (const char **p,
                       double      *x)
{
  const char *o = *p;
  double n;

  if (!parse_number (p, &n))
    return FALSE;

  if (n <= 0)
    {
      *p = o;
      return FALSE;
    }

  *x = n;

  return TRUE;
}

/* This fixes a flaw in our use of strchr() below:
 *
 * If p already points at the end of the string,
 * we misinterpret strchr ("xyz", *p) returning
 * non-NULL to mean that we can increment p.
 *
 * But strchr() will return a pointer to the
 * final NUL byte in this case, and we walk off
 * the end of the string. Oops
 */
static inline const char *
_strchr (const char *str,
         int         c)
{
  if (c == 0)
    return NULL;
  else
    return strchr (str, c);
}

static gboolean
parse_flag (const char **p,
            gboolean    *f)
{
  skip_whitespace (p);
  if (_strchr ("01", **p))
    {
      *f = **p == '1';
      (*p)++;
      skip_optional_comma (p);
      return TRUE;
    }

  return FALSE;
}

static gboolean
parse_command (const char **p,
               char        *cmd)
{
  const char *s;
  const char *allowed;

  if (*cmd == 'X')
    allowed = "mM";
  else
    allowed = "mMhHvVzZlLcCsStTqQaAoO";

  skip_whitespace (p);
  s = _strchr (allowed, **p);
  if (s)
    {
      *cmd = *s;
      (*p)++;
      return TRUE;
    }
  return FALSE;
}

static gboolean
add_op (GskPathOperation        op,
        const graphene_point_t *pts,
        size_t                  n_pts,
        float                   weight,
        gpointer                user_data)
{
  GskPathBuilder *builder = user_data;

  gsk_path_builder_add_op (builder, op, pts, n_pts, weight);
  return TRUE;
}

static gboolean
add_arc (float    rx,
         float    ry,
         float    rotation,
         gboolean large,
         gboolean sweep,
         float    x,
         float    y,
         gpointer user_data)
{
  GskPathBuilder *builder = user_data;

  gsk_path_builder_svg_arc_to (builder, rx, ry, rotation, large, sweep, x, y);
  return TRUE;
}

/**
 * gsk_path_parse:
 * @string: a string
 *
 * Constructs a path from a serialized form.
 *
 * The string is expected to be in (a superset of)
 * [SVG path syntax](https://www.w3.org/TR/SVG11/paths.html#PathData),
 * as e.g. produced by [method@Gsk.Path.to_string].
 *
 * A high-level summary of the syntax:
 *
 * - `M x y` Move to `(x, y)`
 * - `L x y` Add a line from the current point to `(x, y)`
 * - `Q x1 y1 x2 y2` Add a quadratic Bézier from the current point to `(x2, y2)`, with control point `(x1, y1)`
 * - `C x1 y1 x2 y2 x3 y3` Add a cubic Bézier from the current point to `(x3, y3)`, with control points `(x1, y1)` and `(x2, y2)`
 * - `Z` Close the contour by drawing a line back to the start point
 * - `H x` Add a horizontal line from the current point to the given x value
 * - `V y` Add a vertical line from the current point to the given y value
 * - `T x2 y2` Add a quadratic Bézier, using the reflection of the previous segments' control point as control point
 * - `S x2 y2 x3 y3` Add a cubic Bézier, using the reflection of the previous segments' second control point as first control point
 * - `A rx ry r l s x y` Add an elliptical arc from the current point to `(x, y)` with radii rx and ry. See the SVG documentation for how the other parameters influence the arc.
 * - `O x1 y1 x2 y2 w` Add a rational quadratic Bézier from the current point to `(x2, y2)` with control point `(x1, y1)` and weight `w`.
 *
 * All the commands have lowercase variants that interpret coordinates
 * relative to the current point.
 *
 * The `O` command is an extension that is not supported in SVG.
 *
 * Returns: (nullable): a new `GskPath`, or `NULL` if @string could not be parsed
 *
 * Since: 4.14
 */
GskPath *
gsk_path_parse (const char *string)
{
  GskPathParser parser = {
    add_op, add_arc
  };
  GskPathBuilder *builder;

  builder = gsk_path_builder_new ();

  if (!gsk_path_parse_full (string, &parser, builder))
    {
      gsk_path_builder_unref (builder);
      return NULL;
    }

  return gsk_path_builder_free_to_path (builder);
}

#define move_to(builder, x0, y0) \
  if (!parser->add_op (GSK_PATH_MOVE, (const graphene_point_t []) { { x0, y0 }, }, 1, 1, builder)) \
    return FALSE;
#define close(builder) \
  if (!parser->add_op (GSK_PATH_CLOSE, \
                  (const graphene_point_t []) { \
                    { x, y }, \
                  }, \
                  0, 1, builder)) \
    return FALSE;
#define line_to(builder, x1, y1) \
  if (!parser->add_op (GSK_PATH_LINE, \
                       (const graphene_point_t []) { \
                         { x, y }, \
                         { x1, y1 }, \
                       }, \
                       2, 1, builder)) \
    return FALSE;
#define quad_to(builder, x1, y1, x2, y2) \
  if (!parser->add_op (GSK_PATH_QUAD, \
                  (const graphene_point_t []) { \
                    { x, y }, \
                    { x1, y1 }, \
                    { x2, y2 }, \
                  }, \
                  3, 1, builder)) \
    return FALSE;
#define cubic_to(builder, x1, y1, x2, y2, x3, y3) \
  if (!parser->add_op (GSK_PATH_CUBIC, \
                  (const graphene_point_t []) { \
                    { x, y }, \
                    { x1, y1 }, \
                    { x2, y2 }, \
                    { x3, y3 }, \
                  }, \
                  4, 1, builder)) \
    return FALSE;
#define conic_to(builder, x1, y1, x2, y2, w) \
  if (!parser->add_op (GSK_PATH_CONIC, \
                    (const graphene_point_t []) { \
                      { x, y }, \
                      { x1, y1 }, \
                      { x2, y2 }, \
                    }, \
                    3, w, builder)) \
    return FALSE;

gboolean
gsk_path_parse_full (const char    *string,
                     GskPathParser *parser,
                     gpointer       builder)
{
  double x, y; /* current point */
  double prev_x1, prev_y1;
  double path_x, path_y; /* start point of the current subpath */
  const char *p;
  char cmd;
  char prev_cmd;
  gboolean after_comma;
  gboolean repeat;

  cmd = 'X';
  path_x = path_y = 0;
  x = y = 0;
  prev_x1 = prev_y1 = 0;
  after_comma = FALSE;

  p = string;
  while (*p)
    {
      prev_cmd = g_ascii_toupper (cmd);
      repeat = !parse_command (&p, &cmd);

      if (after_comma && !repeat)
        return FALSE;

      switch (cmd)
        {
        case 'X':
          return FALSE;

        case 'Z':
        case 'z':
          if (repeat)
            return FALSE;
          else
            {
              close (builder);
              x = path_x;
              y = path_y;
            }
          break;

        case 'M':
        case 'm':
          {
            double x1, y1;

            if (parse_coordinate_pair (&p, &x1, &y1))
              {
                if (cmd == 'm')
                  {
                    x1 += x;
                    y1 += y;
                  }

                if (repeat)
                  {
                    line_to (builder, x1, y1);
                  }
                else
                  {
                    move_to (builder, x1, y1);
                    path_x = x1;
                    path_y = y1;
                  }

                x = x1;
                y = y1;
              }
            else
              return FALSE;
          }
          break;

        case 'L':
        case 'l':
          {
            double x1, y1;

            if (parse_coordinate_pair (&p, &x1, &y1))
              {
                if (cmd == 'l')
                  {
                    x1 += x;
                    y1 += y;
                  }

                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);
                    path_x = x;
                    path_y = y;
                  }
                line_to (builder, x1, y1);
                x = x1;
                y = y1;
              }
            else
              return FALSE;
          }
          break;

        case 'H':
        case 'h':
          {
            double x1;

            if (parse_coordinate (&p, &x1))
              {
                if (cmd == 'h')
                  x1 += x;
                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);

                    path_x = x;
                    path_y = y;
                  }
                line_to (builder, x1, y);
                x = x1;
              }
            else
              return FALSE;
          }
          break;

        case 'V':
        case 'v':
          {
            double y1;

            if (parse_coordinate (&p, &y1))
              {
                if (cmd == 'v')
                  y1 += y;
                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);

                    path_x = x;
                    path_y = y;
                  }
                line_to (builder, x, y1);
                y = y1;
              }
            else
              return FALSE;
          }
          break;

        case 'C':
        case 'c':
          {
            double x0, y0, x1, y1, x2, y2;

            if (parse_coordinate_pair (&p, &x0, &y0) &&
                parse_coordinate_pair (&p, &x1, &y1) &&
                parse_coordinate_pair (&p, &x2, &y2))
              {
                if (cmd == 'c')
                  {
                    x0 += x;
                    y0 += y;
                    x1 += x;
                    y1 += y;
                    x2 += x;
                    y2 += y;
                  }
                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);
                    path_x = x;
                    path_y = y;
                  }
                cubic_to (builder, x0, y0, x1, y1, x2, y2);
                prev_x1 = x1;
                prev_y1 = y1;
                x = x2;
                y = y2;
              }
            else
              return FALSE;
          }
          break;

        case 'S':
        case 's':
          {
            double x0, y0, x1, y1, x2, y2;

            if (parse_coordinate_pair (&p, &x1, &y1) &&
                parse_coordinate_pair (&p, &x2, &y2))
              {
                if (cmd == 's')
                  {
                    x1 += x;
                    y1 += y;
                    x2 += x;
                    y2 += y;
                  }
                if (prev_cmd == 'C' || prev_cmd == 'S')
                  {
                    x0 = 2 * x - prev_x1;
                    y0 = 2 * y - prev_y1;
                  }
                else
                  {
                    x0 = x;
                    y0 = y;
                  }
                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);
                    path_x = x;
                    path_y = y;
                  }
                cubic_to (builder, x0, y0, x1, y1, x2, y2);

                prev_x1 = x1;
                prev_y1 = y1;
                x = x2;
                y = y2;
              }
            else
              return FALSE;
          }
          break;

        case 'Q':
        case 'q':
          {
            double x1, y1, x2, y2;

            if (parse_coordinate_pair (&p, &x1, &y1) &&
                parse_coordinate_pair (&p, &x2, &y2))
              {
                if (cmd == 'q')
                  {
                    x1 += x;
                    y1 += y;
                    x2 += x;
                    y2 += y;
                  }
                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);
                    path_x = x;
                    path_y = y;
                  }
                quad_to (builder, x1, y1, x2, y2);
                prev_x1 = x1;
                prev_y1 = y1;
                x = x2;
                y = y2;
              }
            else
              return FALSE;
          }
          break;

        case 'T':
        case 't':
          {
            double x1, y1, x2, y2;

            if (parse_coordinate_pair (&p, &x2, &y2))
              {
                if (cmd == 't')
                  {
                    x2 += x;
                    y2 += y;
                  }
                if (prev_cmd == 'Q' || prev_cmd == 'T')
                  {
                    x1 = 2 * x - prev_x1;
                    y1 = 2 * y - prev_y1;
                  }
                else
                  {
                    x1 = x;
                    y1 = y;
                  }
                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);
                    path_x = x;
                    path_y = y;
                  }
                quad_to (builder, x1, y1, x2, y2);
                prev_x1 = x1;
                prev_y1 = y1;
                x = x2;
                y = y2;
              }
            else
              return FALSE;
          }
          break;

        case 'O':
        case 'o':
          {
            double x1, y1, x2, y2, weight;

            if (parse_coordinate_pair (&p, &x1, &y1) &&
                parse_coordinate_pair (&p, &x2, &y2) &&
                parse_positive_number (&p, &weight))
              {
                if (cmd == 'o')
                  {
                    x1 += x;
                    y1 += y;
                    x2 += x;
                    y2 += y;
                  }
                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);
                    path_x = x;
                    path_y = y;
                  }
                conic_to (builder, x1, y1, x2, y2, weight);
                x = x2;
                y = y2;
              }
            else
              return FALSE;
          }
          break;

        case 'A':
        case 'a':
          {
            double rx, ry;
            double x_axis_rotation;
            int large_arc, sweep;
            double x1, y1;

            if (parse_number (&p, &rx) &&
                parse_number (&p, &ry) &&
                parse_number (&p, &x_axis_rotation) &&
                parse_flag (&p, &large_arc) &&
                parse_flag (&p, &sweep) &&
                parse_coordinate_pair (&p, &x1, &y1))
              {
                if (cmd == 'a')
                  {
                    x1 += x;
                    y1 += y;
                  }

                /* If either rx or ry have negative signs, these are dropped;
                 * the absolute value is used instead. */
                if (rx < 0)
                  rx = -rx;
                if (ry < 0)
                  ry = -ry;

                if (prev_cmd == 'Z')
                  {
                    move_to (builder, x, y);
                    path_x = x;
                    path_y = y;
                  }

                /* If either rx or ry is 0, then this arc is treated as a straight line segment
                 * (a "lineto") joining the endpoints */
                if (rx == 0 || ry == 0)
                  {
                    line_to (builder, x1, y1);
                  }
                else
                  {
                    if (!parser->add_arc (rx, ry, x_axis_rotation, large_arc, sweep, x1, y1, builder))
                      return FALSE;
                  }
                x = x1;
                y = y1;
              }
            else
              return FALSE;
          }
          break;

        default:
          return FALSE;
        }

      after_comma = (p > string) && p[-1] == ',';

      skip_whitespace (&p);
    }

  if (after_comma)
    return FALSE;

  return TRUE;
}

/* vim:set foldmethod=marker: */
