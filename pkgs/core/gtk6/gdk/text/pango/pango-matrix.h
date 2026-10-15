/* GdkText
 * pango-matrix.h: Matrix manipulation routines
 *
 * Copyright (C) 2002, 2006 Red Hat Software
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

#ifndef __PANGO_MATRIX_H__
#define __PANGO_MATRIX_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _PangoMatrix    GdkTextMatrix;

/**
 * GdkTextMatrix:
 * @xx: 1st component of the transformation matrix
 * @xy: 2nd component of the transformation matrix
 * @yx: 3rd component of the transformation matrix
 * @yy: 4th component of the transformation matrix
 * @x0: x translation
 * @y0: y translation
 *
 * A `GdkTextMatrix` specifies a transformation between user-space
 * and device coordinates.
 *
 * The transformation is given by
 *
 * ```
 * x_device = x_user * matrix->xx + y_user * matrix->xy + matrix->x0;
 * y_device = x_user * matrix->yx + y_user * matrix->yy + matrix->y0;
 * ```
 *
 * Since: 1.6
 */
struct _PangoMatrix
{
  double xx;
  double xy;
  double yx;
  double yy;
  double x0;
  double y0;
};

#define GDK_TEXT_TYPE_MATRIX (gdk_text_matrix_get_type ())

/**
 * GDK_TEXT_MATRIX_INIT:
 *
 * Constant that can be used to initialize a `GdkTextMatrix` to
 * the identity transform.
 *
 * ```
 * GdkTextMatrix matrix = GDK_TEXT_MATRIX_INIT;
 * gdk_text_matrix_rotate (&matrix, 45.);
 * ```
 *
 * Since: 1.6
 **/
#define GDK_TEXT_MATRIX_INIT { 1., 0., 0., 1., 0., 0. }

/* for GdkTextRectangle */
#include <pango/pango-types.h>

GDK_TEXT_AVAILABLE_IN_1_6
GType gdk_text_matrix_get_type (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_6
GdkTextMatrix *gdk_text_matrix_copy   (const GdkTextMatrix *matrix);
GDK_TEXT_AVAILABLE_IN_1_6
void         gdk_text_matrix_free   (GdkTextMatrix *matrix);

GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_matrix_translate (GdkTextMatrix *matrix,
			     double       tx,
			     double       ty);
GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_matrix_scale     (GdkTextMatrix *matrix,
			     double       scale_x,
			     double       scale_y);
GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_matrix_rotate    (GdkTextMatrix *matrix,
			     double       degrees);
GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_matrix_concat    (GdkTextMatrix       *matrix,
			     const GdkTextMatrix *new_matrix);
GDK_TEXT_AVAILABLE_IN_1_16
void gdk_text_matrix_transform_point    (const GdkTextMatrix *matrix,
				      double            *x,
				      double            *y);
GDK_TEXT_AVAILABLE_IN_1_16
void gdk_text_matrix_transform_distance (const GdkTextMatrix *matrix,
				      double            *dx,
				      double            *dy);
GDK_TEXT_AVAILABLE_IN_1_16
void gdk_text_matrix_transform_rectangle (const GdkTextMatrix *matrix,
				       GdkTextRectangle    *rect);
GDK_TEXT_AVAILABLE_IN_1_16
void gdk_text_matrix_transform_pixel_rectangle (const GdkTextMatrix *matrix,
					     GdkTextRectangle    *rect);
GDK_TEXT_AVAILABLE_IN_1_12
double gdk_text_matrix_get_font_scale_factor (const GdkTextMatrix *matrix) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_38
void gdk_text_matrix_get_font_scale_factors (const GdkTextMatrix *matrix,
					  double *xscale, double *yscale);
GDK_TEXT_AVAILABLE_IN_1_50
double gdk_text_matrix_get_slant_ratio (const GdkTextMatrix *matrix) G_GNUC_PURE;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextMatrix, gdk_text_matrix_free)

G_END_DECLS

#endif /* __PANGO_MATRIX_H__ */
