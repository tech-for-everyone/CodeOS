/* GdkText
 * pango-context.h: Rendering contexts
 *
 * Copyright (C) 2000 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __PANGO_CONTEXT_H__
#define __PANGO_CONTEXT_H__

#include <pango/pango-types.h>
#include <pango/pango-font.h>
#include <pango/pango-fontmap.h>
#include <pango/pango-attributes.h>
#include <pango/pango-direction.h>

G_BEGIN_DECLS

typedef struct _PangoContextClass GdkTextContextClass;

#define GDK_TEXT_TYPE_CONTEXT              (gdk_text_context_get_type ())
#define GDK_TEXT_CONTEXT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_CONTEXT, GdkTextContext))
#define GDK_TEXT_CONTEXT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_CONTEXT, GdkTextContextClass))
#define GDK_TEXT_IS_CONTEXT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_CONTEXT))
#define GDK_TEXT_IS_CONTEXT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_CONTEXT))
#define GDK_TEXT_CONTEXT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_CONTEXT, GdkTextContextClass))


GDK_TEXT_AVAILABLE_IN_ALL
GType                   gdk_text_context_get_type                  (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextContext *          gdk_text_context_new                       (void);
GDK_TEXT_AVAILABLE_IN_1_32
void                    gdk_text_context_changed                   (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_context_set_font_map              (GdkTextContext                 *context,
                                                                 GdkTextFontMap                 *font_map);
GDK_TEXT_AVAILABLE_IN_1_6
GdkTextFontMap *          gdk_text_context_get_font_map              (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_1_32
guint                   gdk_text_context_get_serial                (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_context_list_families             (GdkTextContext                 *context,
                                                                 GdkTextFontFamily            ***families,
                                                                 int                          *n_families);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFont *             gdk_text_context_load_font                 (GdkTextContext                 *context,
                                                                 const GdkTextFontDescription   *desc);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontset *          gdk_text_context_load_fontset              (GdkTextContext                 *context,
                                                                 const GdkTextFontDescription   *desc,
                                                                 GdkTextLanguage                *language);

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontMetrics *      gdk_text_context_get_metrics               (GdkTextContext                 *context,
                                                                 const GdkTextFontDescription   *desc,
                                                                 GdkTextLanguage                *language);

GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_context_set_font_description      (GdkTextContext                 *context,
                                                                 const GdkTextFontDescription   *desc);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontDescription *  gdk_text_context_get_font_description      (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLanguage *         gdk_text_context_get_language              (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_context_set_language              (GdkTextContext                 *context,
                                                                 GdkTextLanguage                *language);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_context_set_base_dir              (GdkTextContext                 *context,
                                                                 GdkTextDirection                direction);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextDirection          gdk_text_context_get_base_dir              (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_1_16
void                    gdk_text_context_set_base_gravity          (GdkTextContext                 *context,
                                                                 GdkTextGravity                  gravity);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextGravity            gdk_text_context_get_base_gravity          (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextGravity            gdk_text_context_get_gravity               (GdkTextContext                 *context);
GDK_TEXT_AVAILABLE_IN_1_16
void                    gdk_text_context_set_gravity_hint          (GdkTextContext                 *context,
                                                                 GdkTextGravityHint              hint);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextGravityHint        gdk_text_context_get_gravity_hint          (GdkTextContext                 *context);

GDK_TEXT_AVAILABLE_IN_1_6
void                    gdk_text_context_set_matrix                (GdkTextContext                 *context,
                                                                 const GdkTextMatrix            *matrix);
GDK_TEXT_AVAILABLE_IN_1_6
const GdkTextMatrix *     gdk_text_context_get_matrix                (GdkTextContext                 *context);

GDK_TEXT_AVAILABLE_IN_1_44
void                    gdk_text_context_set_round_glyph_positions (GdkTextContext                 *context,
                                                                 gboolean                      round_positions);
GDK_TEXT_AVAILABLE_IN_1_44
gboolean                gdk_text_context_get_round_glyph_positions (GdkTextContext                 *context);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextContext, g_object_unref)

G_END_DECLS

#endif /* __PANGO_CONTEXT_H__ */
