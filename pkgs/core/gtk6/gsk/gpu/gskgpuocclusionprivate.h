#pragma once

#include <graphene.h>
#include <gdk/gdk.h>
#include <gsk/gsktypes.h>
#include <gdk/gdkcolorprivate.h>

G_BEGIN_DECLS

typedef struct _GskGpuOcclusion GskGpuOcclusion;
typedef struct _GskGpuRenderPass GskGpuRenderPass;
typedef struct _GskGpuTransform GskGpuTransform;
typedef struct _GskGpuFrame GskGpuFrame;

GskGpuFrame *gsk_gpu_occlusion_get_frame (GskGpuOcclusion *self);

gboolean gsk_gpu_occlusion_push_clip (GskGpuOcclusion *self,
                                      const graphene_rect_t *clip,
                                      GskRectSnap snap,
                                      GdkRectangle *out_save);

void gsk_gpu_occlusion_pop_clip (GskGpuOcclusion *self,
                                 const GdkRectangle *saved);

gboolean gsk_gpu_occlusion_push_transform (GskGpuOcclusion *self,
                                           GskTransform *transform,
                                           GskGpuTransform *out_save);

void gsk_gpu_occlusion_pop_transform (GskGpuOcclusion *self,
                                      const GskGpuTransform *saved);

GskGpuRenderPass *gsk_gpu_occlusion_begin_rendering_whatever (GskGpuOcclusion *self);
GskGpuRenderPass *gsk_gpu_occlusion_begin_rendering_transparent (GskGpuOcclusion *self);
GskGpuRenderPass *gsk_gpu_occlusion_begin_rendering_color (GskGpuOcclusion *self,
                                                            const GdkColor *color);

GskGpuRenderPass *gsk_gpu_occlusion_try_node (GskGpuOcclusion *self,
                                              GskRenderNode *node,
                                              gsize pos);

G_END_DECLS
