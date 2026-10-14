/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK6 GSK-on-Qt6 renderer — the Phase 3 replacement for the GL/Vulkan/gpu
 * renderers.  It walks the same GskRenderNode tree GTK builds and executes the
 * nodes as QPainter calls on a raster surface, which is what QtFbSupport gives
 * us on a framebuffer and what QPainter gives us through QRasterWindow.
 *
 * The renderer keeps no GObject state and takes the QPainter opaquely, so the
 * C side (GTK) links against this without a Qt header in sight.
 */

#ifndef GTK6_GSK_QT_H
#define GTK6_GSK_QT_H

#include <stdint.h>

#include "qt/qttext.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _GskQtRenderer GskQtRenderer;

GskQtRenderer *gsk_qt_renderer_new  (void);
void           gsk_qt_renderer_free (GskQtRenderer *renderer);

/* painter is a QPainter *; width/height bound the surface. */
void gsk_qt_renderer_begin_frame (GskQtRenderer *renderer,
                                  void          *painter,
                                  double         width,
                                  double         height);
void gsk_qt_renderer_end_frame   (GskQtRenderer *renderer);

/* Intersect the current clip with a device-space rectangle. */
void gsk_qt_renderer_push_clip (GskQtRenderer *renderer,
                                double         x,
                                double         y,
                                double         width,
                                double         height);
void gsk_qt_renderer_pop_clip (GskQtRenderer *renderer);

/* Translate the current transform. */
void gsk_qt_renderer_push_transform (GskQtRenderer *renderer,
                                     double         dx,
                                     double         dy);
void gsk_qt_renderer_pop_transform (GskQtRenderer *renderer);

/* rgba is 0xRRGGBBAA. */
void gsk_qt_renderer_fill_rect (GskQtRenderer *renderer,
                                double         x,
                                double         y,
                                double         width,
                                double         height,
                                uint32_t       rgba);
void gsk_qt_renderer_stroke_rect (GskQtRenderer *renderer,
                                  double         x,
                                  double         y,
                                  double         width,
                                  double         height,
                                  double         line_width,
                                  uint32_t       rgba);

void gsk_qt_renderer_draw_text (GskQtRenderer *renderer,
                                GtkQtText     *text,
                                double         x,
                                double         y,
                                uint32_t       rgba);

#ifdef __cplusplus
}
#endif

#endif /* GTK6_GSK_QT_H */
