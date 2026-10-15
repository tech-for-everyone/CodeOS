#pragma once

#include "gskcairoblurprivate.h"
#include "gskroundedrect.h"

gboolean gsk_cairo_shadow_needs_blur (double radius);

void gsk_cairo_shadow_draw (GdkPainter              *cr,
                            GdkColorState        *ccs,
                            gboolean              inset,
                            const GskRoundedRect *box,
                            const GskRoundedRect *clip_box,
                            float                 radius,
                            const GdkColor       *color,
                            GskBlurFlags          blur_flags);

typedef enum {
  TOP,
  RIGHT,
  BOTTOM,
  LEFT
} GskShadowSide;

void gsk_cairo_shadow_draw_corner (GdkPainter               *cr,
                                   GdkColorState         *ccs,
                                   gboolean               inset,
                                   const GskRoundedRect  *box,
                                   const GskRoundedRect  *clip_box,
                                   float                  radius,
                                   const GdkColor        *color,
                                   GskCorner              corner,
                                   GdkRectangle *drawn_rect);

void gsk_cairo_shadow_draw_side (GdkPainter               *cr,
                                 GdkColorState         *ccs,
                                 gboolean               inset,
                                 const GskRoundedRect  *box,
                                 const GskRoundedRect  *clip_box,
                                 float                  radius,
                                 const GdkColor        *color,
                                 GskShadowSide          side,
                                 GdkRectangle *drawn_rect);
