/* GdkText
 * pango-glyph.h: Glyph storage
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

#ifndef __PANGO_GLYPH_H__
#define __PANGO_GLYPH_H__

#include <pango/pango-types.h>
#include <pango/pango-item.h>
#include <pango/pango-break.h>

G_BEGIN_DECLS

typedef struct _PangoGlyphGeometry GdkTextGlyphGeometry;
typedef struct _PangoGlyphVisAttr GdkTextGlyphVisAttr;
typedef struct _PangoGlyphInfo GdkTextGlyphInfo;
typedef struct _PangoGlyphString GdkTextGlyphString;

/* 1024ths of a device unit */
/**
 * GdkTextGlyphUnit:
 *
 * The `GdkTextGlyphUnit` type is used to store dimensions within
 * GdkText.
 *
 * Dimensions are stored in 1/GDK_TEXT_SCALE of a device unit.
 * (A device unit might be a pixel for screen display, or
 * a point on a printer.) GDK_TEXT_SCALE is currently 1024, and
 * may change in the future (unlikely though), but you should not
 * depend on its exact value.
 *
 * The GDK_TEXT_PIXELS() macro can be used to convert from glyph units
 * into device units with correct rounding.
 */
typedef gint32 GdkTextGlyphUnit;

/* Positioning information about a glyph
 */
/**
 * GdkTextGlyphGeometry:
 * @width: the logical width to use for the the character.
 * @x_offset: horizontal offset from nominal character position.
 * @y_offset: vertical offset from nominal character position.
 *
 * The `GdkTextGlyphGeometry` structure contains width and positioning
 * information for a single glyph.
 *
 * Note that @width is not guaranteed to be the same as the glyph
 * extents. Kerning and other positioning applied during shaping will
 * affect both the @width and the @x_offset for the glyphs in the
 * glyph string that results from shaping.
 *
 * The information in this struct is intended for rendering the glyphs,
 * as follows:
 *
 * 1. Assume the current point is (x, y)
 * 2. Render the current glyph at (x + x_offset, y + y_offset),
 * 3. Advance the current point to (x + width, y)
 * 4. Render the next glyph
 */
struct _PangoGlyphGeometry
{
  GdkTextGlyphUnit width;
  GdkTextGlyphUnit x_offset;
  GdkTextGlyphUnit y_offset;
};

/* Visual attributes of a glyph
 */
/**
 * GdkTextGlyphVisAttr:
 * @is_cluster_start: set for the first logical glyph in each cluster.
 * @is_color: set if the the font will render this glyph with color. Since 1.50
 *
 * A `GdkTextGlyphVisAttr` structure communicates information between
 * the shaping and rendering phases.
 *
 * Currently, it contains cluster start and color information.
 * More attributes may be added in the future.
 *
 * Clusters are stored in visual order, within the cluster, glyphs
 * are always ordered in logical order, since visual order is meaningless;
 * that is, in Arabic text, accent glyphs follow the glyphs for the
 * base character.
 */
struct _PangoGlyphVisAttr
{
  guint is_cluster_start : 1;
  guint is_color         : 1;
};

/* A single glyph
 */
/**
 * GdkTextGlyphInfo:
 * @glyph: the glyph itself.
 * @geometry: the positional information about the glyph.
 * @attr: the visual attributes of the glyph.
 *
 * A `GdkTextGlyphInfo` structure represents a single glyph with
 * positioning information and visual attributes.
 */
struct _PangoGlyphInfo
{
  GdkTextGlyph    glyph;
  GdkTextGlyphGeometry geometry;
  GdkTextGlyphVisAttr  attr;
};

/**
 * GdkTextGlyphString:
 * @num_glyphs: number of glyphs in this glyph string
 * @glyphs: (array length=num_glyphs): array of glyph information
 * @log_clusters: logical cluster info, indexed by the byte index
 *   within the text corresponding to the glyph string
 *
 * A `GdkTextGlyphString` is used to store strings of glyphs with geometry
 * and visual attribute information.
 *
 * The storage for the glyph information is owned by the structure
 * which simplifies memory management.
 */
struct _PangoGlyphString {
  int num_glyphs;

  GdkTextGlyphInfo *glyphs;
  int *log_clusters;

  /*< private >*/
  int space;
};

#define GDK_TEXT_TYPE_GLYPH_STRING (gdk_text_glyph_string_get_type ())

GDK_TEXT_AVAILABLE_IN_ALL
GType                   gdk_text_glyph_string_get_type             (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextGlyphString *      gdk_text_glyph_string_new                  (void);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_glyph_string_set_size             (GdkTextGlyphString    *string,
                                                                 int                  new_len);

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextGlyphString *      gdk_text_glyph_string_copy                 (GdkTextGlyphString    *string);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_glyph_string_free                 (GdkTextGlyphString    *string);

GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_glyph_string_extents              (GdkTextGlyphString    *glyphs,
                                                                 GdkTextFont           *font,
                                                                 GdkTextRectangle      *ink_rect,
                                                                 GdkTextRectangle      *logical_rect);
GDK_TEXT_AVAILABLE_IN_1_14
int                     gdk_text_glyph_string_get_width            (GdkTextGlyphString    *glyphs);

GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_glyph_string_extents_range        (GdkTextGlyphString    *glyphs,
                                                                 int                  start,
                                                                 int                  end,
                                                                 GdkTextFont           *font,
                                                                 GdkTextRectangle      *ink_rect,
                                                                 GdkTextRectangle      *logical_rect);

GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_glyph_string_get_logical_widths   (GdkTextGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 int                  embedding_level,
                                                                 int                 *logical_widths);

GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_glyph_string_index_to_x           (GdkTextGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 GdkTextAnalysis       *analysis,
                                                                 int                  index_,
                                                                 gboolean             trailing,
                                                                 int                 *x_pos);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_glyph_string_x_to_index           (GdkTextGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 GdkTextAnalysis       *analysis,
                                                                 int                  x_pos,
                                                                 int                 *index_,
                                                                 int                 *trailing);

GDK_TEXT_AVAILABLE_IN_1_50
void                    gdk_text_glyph_string_index_to_x_full      (GdkTextGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 GdkTextAnalysis       *analysis,
                                                                 GdkTextLogAttr        *attrs,
                                                                 int                  index_,
                                                                 gboolean             trailing,
                                                                 int                 *x_pos);

/* Shaping */

/**
 * GdkTextShapeFlags:
 * @GDK_TEXT_SHAPE_NONE: Default value
 * @GDK_TEXT_SHAPE_ROUND_POSITIONS: Round glyph positions and widths to whole device units
 *   This option should be set if the target renderer can't do subpixel positioning of glyphs
 *
 * Flags influencing the shaping process.
 *
 * `GdkTextShapeFlags` can be passed to [func@GdkText.shape_with_flags].
 *
 * Since: 1.44
 */
typedef enum {
  GDK_TEXT_SHAPE_NONE            = 0,
  GDK_TEXT_SHAPE_ROUND_POSITIONS = 1 << 0,
} GdkTextShapeFlags;

GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_shape                             (const char          *text,
                                                                 int                  length,
                                                                 const GdkTextAnalysis *analysis,
                                                                 GdkTextGlyphString    *glyphs);

GDK_TEXT_AVAILABLE_IN_1_32
void                    gdk_text_shape_full                        (const char          *item_text,
                                                                 int                  item_length,
                                                                 const char          *paragraph_text,
                                                                 int                  paragraph_length,
                                                                 const GdkTextAnalysis *analysis,
                                                                 GdkTextGlyphString    *glyphs);

GDK_TEXT_AVAILABLE_IN_1_44
void                    gdk_text_shape_with_flags                  (const char          *item_text,
                                                                 int                  item_length,
                                                                 const char          *paragraph_text,
                                                                 int                  paragraph_length,
                                                                 const GdkTextAnalysis *analysis,
                                                                 GdkTextGlyphString    *glyphs,
                                                                 GdkTextShapeFlags      flags);


GDK_TEXT_AVAILABLE_IN_1_50
void                    gdk_text_shape_item                        (GdkTextItem           *item,
                                                                 const char          *paragraph_text,
                                                                 int                  paragraph_length,
                                                                 GdkTextLogAttr        *log_attrs,
                                                                 GdkTextGlyphString    *glyphs,
                                                                 GdkTextShapeFlags      flags);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextGlyphString, gdk_text_glyph_string_free)

G_END_DECLS

#endif /* __PANGO_GLYPH_H__ */
