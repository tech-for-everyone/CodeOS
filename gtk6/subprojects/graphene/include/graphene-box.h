/* graphene-box.h: An axis aligned bounding box
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright 2014  Emmanuele Bassi
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#pragma once

#if !defined(GRAPHENE_H_INSIDE) && !defined(GRAPHENE_COMPILATION)
#error "Only graphene.h can be included directly."
#endif

#include "graphene-types.h"
#include "graphene-vec3.h"

GRAPHENE_BEGIN_DECLS

/**
 * graphene_box_t:
 *
 * A 3D box, described as the volume between a minimum and
 * a maximum vertices.
 *
 * Since: 1.2
 */
struct _graphene_box_t
{
  /*< private >*/
  GRAPHENE_PRIVATE_FIELD (graphene_vec3_t, min);
  GRAPHENE_PRIVATE_FIELD (graphene_vec3_t, max);
};

GRAPHENE_AVAILABLE_IN_1_2
graphene_box_t *        graphene_box_alloc                      (void);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_free                       (graphene_box_t           *box);

GRAPHENE_AVAILABLE_IN_1_2
graphene_box_t *        graphene_box_init                       (graphene_box_t           *box,
                                                                 const graphene_point3d_t *min,
                                                                 const graphene_point3d_t *max);
GRAPHENE_AVAILABLE_IN_1_2
graphene_box_t *        graphene_box_init_from_points           (graphene_box_t           *box,
                                                                 unsigned int              n_points,
                                                                 const graphene_point3d_t *points);
GRAPHENE_AVAILABLE_IN_1_2
graphene_box_t *        graphene_box_init_from_vectors          (graphene_box_t           *box,
                                                                 unsigned int              n_vectors,
                                                                 const graphene_vec3_t    *vectors);
GRAPHENE_AVAILABLE_IN_1_2
graphene_box_t *        graphene_box_init_from_box              (graphene_box_t           *box,
                                                                 const graphene_box_t     *src);
GRAPHENE_AVAILABLE_IN_1_2
graphene_box_t *        graphene_box_init_from_vec3             (graphene_box_t           *box,
                                                                 const graphene_vec3_t    *min,
                                                                 const graphene_vec3_t    *max);

GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_expand                     (const graphene_box_t     *box,
                                                                 const graphene_point3d_t *point,
                                                                 graphene_box_t           *res);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_expand_vec3                (const graphene_box_t     *box,
                                                                 const graphene_vec3_t    *vec,
                                                                 graphene_box_t           *res);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_expand_scalar              (const graphene_box_t     *box,
                                                                 float                     scalar,
                                                                 graphene_box_t           *res);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_union                      (const graphene_box_t     *a,
                                                                 const graphene_box_t     *b,
                                                                 graphene_box_t           *res);
GRAPHENE_AVAILABLE_IN_1_2
bool                    graphene_box_intersection               (const graphene_box_t     *a,
                                                                 const graphene_box_t     *b,
                                                                 graphene_box_t           *res);

GRAPHENE_AVAILABLE_IN_1_2
float                   graphene_box_get_width                  (const graphene_box_t     *box);
GRAPHENE_AVAILABLE_IN_1_2
float                   graphene_box_get_height                 (const graphene_box_t     *box);
GRAPHENE_AVAILABLE_IN_1_2
float                   graphene_box_get_depth                  (const graphene_box_t     *box);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_get_size                   (const graphene_box_t     *box,
                                                                 graphene_vec3_t          *size);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_get_center                 (const graphene_box_t     *box,
                                                                 graphene_point3d_t       *center);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_get_min                    (const graphene_box_t     *box,
                                                                 graphene_point3d_t       *min);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_get_max                    (const graphene_box_t     *box,
                                                                 graphene_point3d_t       *max);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_get_vertices               (const graphene_box_t     *box,
                                                                 graphene_vec3_t           vertices[]);
GRAPHENE_AVAILABLE_IN_1_2
void                    graphene_box_get_bounding_sphere        (const graphene_box_t     *box,
                                                                 graphene_sphere_t        *sphere);

GRAPHENE_AVAILABLE_IN_1_2
bool                    graphene_box_contains_point             (const graphene_box_t     *box,
                                                                 const graphene_point3d_t *point);
GRAPHENE_AVAILABLE_IN_1_2
bool                    graphene_box_contains_box               (const graphene_box_t     *a,
                                                                 const graphene_box_t     *b);

GRAPHENE_AVAILABLE_IN_1_2
bool                    graphene_box_equal                      (const graphene_box_t     *a,
                                                                 const graphene_box_t     *b);

GRAPHENE_AVAILABLE_IN_1_2
const graphene_box_t *  graphene_box_zero                       (void);
GRAPHENE_AVAILABLE_IN_1_2
const graphene_box_t *  graphene_box_one                        (void);
GRAPHENE_AVAILABLE_IN_1_2
const graphene_box_t *  graphene_box_minus_one                  (void);
GRAPHENE_AVAILABLE_IN_1_2
const graphene_box_t *  graphene_box_one_minus_one              (void);
GRAPHENE_AVAILABLE_IN_1_2
const graphene_box_t *  graphene_box_infinite                   (void);
GRAPHENE_AVAILABLE_IN_1_2
const graphene_box_t *  graphene_box_empty                      (void);

GRAPHENE_END_DECLS
