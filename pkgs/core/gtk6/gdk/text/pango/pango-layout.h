/* GdkText
 * pango-layout.h: High-level layout driver
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __PANGO_LAYOUT_H__
#define __PANGO_LAYOUT_H__

#include <pango/pango-attributes.h>
#include <pango/pango-context.h>
#include <pango/pango-glyph-item.h>
#include <pango/pango-tabs.h>

G_BEGIN_DECLS

typedef struct _PangoLayout      GdkTextLayout;
typedef struct _PangoLayoutClass GdkTextLayoutClass;
typedef struct _PangoLayoutLine  GdkTextLayoutLine;

/**
 * GdkTextLayoutRun:
 *
 * A `GdkTextLayoutRun` represents a single run within a `GdkTextLayoutLine`.
 *
 * It is simply an alternate name for [struct@GdkText.GlyphItem].
 * See the [struct@GdkText.GlyphItem] docs for details on the fields.
 */
typedef GdkTextGlyphItem GdkTextLayoutRun;

/**
 * GdkTextAlignment:
 * @GDK_TEXT_ALIGN_LEFT: Put all available space on the right
 * @GDK_TEXT_ALIGN_CENTER: Center the line within the available space
 * @GDK_TEXT_ALIGN_RIGHT: Put all available space on the left
 *
 * `GdkTextAlignment` describes how to align the lines of a `GdkTextLayout`
 * within the available space.
 *
 * If the `GdkTextLayout` is set to justify using [method@GdkText.Layout.set_justify],
 * this only affects partial lines.
 *
 * See [method@GdkText.Layout.set_auto_dir] for how text direction affects
 * the interpretation of `GdkTextAlignment` values.
 */
typedef enum {
  GDK_TEXT_ALIGN_LEFT,
  GDK_TEXT_ALIGN_CENTER,
  GDK_TEXT_ALIGN_RIGHT
} GdkTextAlignment;

/**
 * GdkTextWrapMode:
 * @GDK_TEXT_WRAP_WORD: wrap lines at word boundaries.
 * @GDK_TEXT_WRAP_CHAR: wrap lines at character boundaries.
 * @GDK_TEXT_WRAP_WORD_CHAR: wrap lines at word boundaries, but fall back to
 *   character boundaries if there is not enough space for a full word.
 *
 * `GdkTextWrapMode` describes how to wrap the lines of a `GdkTextLayout`
 * to the desired width.
 *
 * For @GDK_TEXT_WRAP_WORD, GdkText uses break opportunities that are determined
 * by the Unicode line breaking algorithm. For @GDK_TEXT_WRAP_CHAR, GdkText allows
 * breaking at grapheme boundaries that are determined by the Unicode text
 * segmentation algorithm.
 */

/**
 * GDK_TEXT_WRAP_NONE:
 *
 * do not wrap.
 *
 * Since: 1.56
 */
typedef enum {
  GDK_TEXT_WRAP_WORD,
  GDK_TEXT_WRAP_CHAR,
  GDK_TEXT_WRAP_WORD_CHAR,
  GDK_TEXT_WRAP_NONE GDK_TEXT_AVAILABLE_ENUMERATOR_IN_1_56
} GdkTextWrapMode;

/**
 * GdkTextEllipsizeMode:
 * @GDK_TEXT_ELLIPSIZE_NONE: No ellipsization
 * @GDK_TEXT_ELLIPSIZE_START: Omit characters at the start of the text
 * @GDK_TEXT_ELLIPSIZE_MIDDLE: Omit characters in the middle of the text
 * @GDK_TEXT_ELLIPSIZE_END: Omit characters at the end of the text
 *
 * `GdkTextEllipsizeMode` describes what sort of ellipsization
 * should be applied to text.
 *
 * In the ellipsization process characters are removed from the
 * text in order to make it fit to a given width and replaced
 * with an ellipsis.
 */
typedef enum {
  GDK_TEXT_ELLIPSIZE_NONE,
  GDK_TEXT_ELLIPSIZE_START,
  GDK_TEXT_ELLIPSIZE_MIDDLE,
  GDK_TEXT_ELLIPSIZE_END
} GdkTextEllipsizeMode;

/**
 * GdkTextLayoutLine:
 * @layout: (nullable): the layout this line belongs to, might be %NULL
 * @start_index: start of line as byte index into layout->text
 * @length: length of line in bytes
 * @runs: (nullable) (element-type GdkText.LayoutRun): list of runs in the
 *   line, from left to right
 * @is_paragraph_start: #TRUE if this is the first line of the paragraph
 * @resolved_dir: #Resolved GdkTextDirection of line
 *
 * A `GdkTextLayoutLine` represents one of the lines resulting from laying
 * out a paragraph via `GdkTextLayout`.
 *
 * `GdkTextLayoutLine` structures are obtained by calling
 * [method@GdkText.Layout.get_line] and are only valid until the text,
 * attributes, or settings of the parent `GdkTextLayout` are modified.
 */
struct _PangoLayoutLine
{
  GdkTextLayout *layout;
  gint         start_index;     /* start of line as byte index into layout->text */
  gint         length;		/* length of line in bytes */
  GSList      *runs;
  guint        is_paragraph_start : 1;  /* TRUE if this is the first line of the paragraph */
  guint        resolved_dir : 3;  /* Resolved GdkTextDirection of line */
};

#define GDK_TEXT_TYPE_LAYOUT              (gdk_text_layout_get_type ())
#define GDK_TEXT_LAYOUT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_LAYOUT, GdkTextLayout))
#define GDK_TEXT_LAYOUT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_LAYOUT, GdkTextLayoutClass))
#define GDK_TEXT_IS_LAYOUT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_LAYOUT))
#define GDK_TEXT_IS_LAYOUT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_LAYOUT))
#define GDK_TEXT_LAYOUT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_LAYOUT, GdkTextLayoutClass))

/* The GdkTextLayout and GdkTextLayoutClass structs are private; if you
 * need to create a subclass of these, file a bug.
 */

GDK_TEXT_AVAILABLE_IN_ALL
GType        gdk_text_layout_get_type       (void) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLayout *gdk_text_layout_new            (GdkTextContext   *context);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLayout *gdk_text_layout_copy           (GdkTextLayout    *src);

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextContext  *gdk_text_layout_get_context    (GdkTextLayout    *layout);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_attributes (GdkTextLayout    *layout,
					    GdkTextAttrList  *attrs);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttrList *gdk_text_layout_get_attributes (GdkTextLayout    *layout);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_text       (GdkTextLayout    *layout,
					    const char     *text,
					    int             length);
GDK_TEXT_AVAILABLE_IN_ALL
const char    *gdk_text_layout_get_text       (GdkTextLayout    *layout);

GDK_TEXT_AVAILABLE_IN_1_30
gint           gdk_text_layout_get_character_count (GdkTextLayout *layout);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_markup     (GdkTextLayout    *layout,
					    const char     *markup,
					    int             length);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_markup_with_accel (GdkTextLayout    *layout,
						   const char     *markup,
						   int             length,
						   gunichar        accel_marker,
						   gunichar       *accel_char);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_font_description (GdkTextLayout                *layout,
						  const GdkTextFontDescription *desc);

GDK_TEXT_AVAILABLE_IN_1_8
const GdkTextFontDescription *gdk_text_layout_get_font_description (GdkTextLayout *layout);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_width            (GdkTextLayout                *layout,
						  int                         width);
GDK_TEXT_AVAILABLE_IN_ALL
int            gdk_text_layout_get_width            (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_1_20
void           gdk_text_layout_set_height           (GdkTextLayout                *layout,
						  int                         height);
GDK_TEXT_AVAILABLE_IN_1_20
int            gdk_text_layout_get_height           (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_wrap             (GdkTextLayout                *layout,
						  GdkTextWrapMode               wrap);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextWrapMode  gdk_text_layout_get_wrap             (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_1_16
gboolean       gdk_text_layout_is_wrapped           (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_indent           (GdkTextLayout                *layout,
						  int                         indent);
GDK_TEXT_AVAILABLE_IN_ALL
int            gdk_text_layout_get_indent           (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_spacing          (GdkTextLayout                *layout,
						  int                         spacing);
GDK_TEXT_AVAILABLE_IN_ALL
int            gdk_text_layout_get_spacing          (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_1_44
void           gdk_text_layout_set_line_spacing     (GdkTextLayout                *layout,
                                                  float                       factor);
GDK_TEXT_AVAILABLE_IN_1_44
float          gdk_text_layout_get_line_spacing     (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_justify          (GdkTextLayout                *layout,
                                                  gboolean                    justify);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean       gdk_text_layout_get_justify          (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_1_50
void           gdk_text_layout_set_justify_last_line (GdkTextLayout                *layout,
                                                   gboolean                    justify);
GDK_TEXT_AVAILABLE_IN_1_50
gboolean       gdk_text_layout_get_justify_last_line (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_1_4
void           gdk_text_layout_set_auto_dir         (GdkTextLayout                *layout,
						  gboolean                    auto_dir);
GDK_TEXT_AVAILABLE_IN_1_4
gboolean       gdk_text_layout_get_auto_dir         (GdkTextLayout                *layout);
GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_alignment        (GdkTextLayout                *layout,
						  GdkTextAlignment              alignment);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAlignment gdk_text_layout_get_alignment        (GdkTextLayout                *layout);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_tabs             (GdkTextLayout                *layout,
						  GdkTextTabArray              *tabs);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextTabArray* gdk_text_layout_get_tabs             (GdkTextLayout                *layout);

GDK_TEXT_AVAILABLE_IN_ALL
void           gdk_text_layout_set_single_paragraph_mode (GdkTextLayout                *layout,
						       gboolean                    setting);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean       gdk_text_layout_get_single_paragraph_mode (GdkTextLayout                *layout);

GDK_TEXT_AVAILABLE_IN_1_6
void               gdk_text_layout_set_ellipsize (GdkTextLayout        *layout,
					       GdkTextEllipsizeMode  ellipsize);
GDK_TEXT_AVAILABLE_IN_1_6
GdkTextEllipsizeMode gdk_text_layout_get_ellipsize (GdkTextLayout        *layout);
GDK_TEXT_AVAILABLE_IN_1_16
gboolean           gdk_text_layout_is_ellipsized (GdkTextLayout        *layout);

GDK_TEXT_AVAILABLE_IN_1_16
int      gdk_text_layout_get_unknown_glyphs_count (GdkTextLayout    *layout);

GDK_TEXT_AVAILABLE_IN_1_46
GdkTextDirection gdk_text_layout_get_direction (GdkTextLayout *layout,
                                           int          index);

GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_context_changed (GdkTextLayout    *layout);
GDK_TEXT_AVAILABLE_IN_1_32
guint    gdk_text_layout_get_serial      (GdkTextLayout    *layout);

GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_get_log_attrs (GdkTextLayout    *layout,
				     GdkTextLogAttr  **attrs,
				     gint           *n_attrs);

GDK_TEXT_AVAILABLE_IN_1_30
const GdkTextLogAttr *gdk_text_layout_get_log_attrs_readonly (GdkTextLayout *layout,
							 gint        *n_attrs);

GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_index_to_pos         (GdkTextLayout    *layout,
					    int             index_,
					    GdkTextRectangle *pos);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_index_to_line_x      (GdkTextLayout    *layout,
					    int             index_,
					    gboolean        trailing,
					    int            *line,
					    int            *x_pos);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_get_cursor_pos       (GdkTextLayout    *layout,
					    int             index_,
					    GdkTextRectangle *strong_pos,
					    GdkTextRectangle *weak_pos);

GDK_TEXT_AVAILABLE_IN_1_50
void     gdk_text_layout_get_caret_pos        (GdkTextLayout    *layout,
                                            int             index_,
                                            GdkTextRectangle *strong_pos,
                                            GdkTextRectangle *weak_pos);

GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_move_cursor_visually (GdkTextLayout    *layout,
					    gboolean        strong,
					    int             old_index,
					    int             old_trailing,
					    int             direction,
					    int            *new_index,
					    int            *new_trailing);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_layout_xy_to_index          (GdkTextLayout    *layout,
					    int             x,
					    int             y,
					    int            *index_,
					    int            *trailing);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_get_extents          (GdkTextLayout    *layout,
					    GdkTextRectangle *ink_rect,
					    GdkTextRectangle *logical_rect);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_get_pixel_extents    (GdkTextLayout    *layout,
					    GdkTextRectangle *ink_rect,
					    GdkTextRectangle *logical_rect);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_get_size             (GdkTextLayout    *layout,
					    int            *width,
					    int            *height);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_get_pixel_size       (GdkTextLayout    *layout,
					    int            *width,
					    int            *height);
GDK_TEXT_AVAILABLE_IN_1_22
int      gdk_text_layout_get_baseline         (GdkTextLayout    *layout);

GDK_TEXT_AVAILABLE_IN_ALL
int              gdk_text_layout_get_line_count       (GdkTextLayout    *layout);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLayoutLine *gdk_text_layout_get_line             (GdkTextLayout    *layout,
						    int             line);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextLayoutLine *gdk_text_layout_get_line_readonly    (GdkTextLayout    *layout,
						    int             line);
GDK_TEXT_AVAILABLE_IN_ALL
GSList *         gdk_text_layout_get_lines            (GdkTextLayout    *layout);
GDK_TEXT_AVAILABLE_IN_1_16
GSList *         gdk_text_layout_get_lines_readonly   (GdkTextLayout    *layout);

/**
 * GdkTextLayoutSerializeFlags:
 * @GDK_TEXT_LAYOUT_SERIALIZE_DEFAULT: Default behavior
 * @GDK_TEXT_LAYOUT_SERIALIZE_CONTEXT: Include context information
 * @GDK_TEXT_LAYOUT_SERIALIZE_OUTPUT: Include information about the formatted output
 *
 * Flags that influence the behavior of [method@GdkText.Layout.serialize].
 *
 * New members may be added to this enumeration over time.
 *
 * Since: 1.50
 */
typedef enum {
  GDK_TEXT_LAYOUT_SERIALIZE_DEFAULT = 0,
  GDK_TEXT_LAYOUT_SERIALIZE_CONTEXT = 1 << 0,
  GDK_TEXT_LAYOUT_SERIALIZE_OUTPUT = 1 << 1,
} GdkTextLayoutSerializeFlags;

GDK_TEXT_AVAILABLE_IN_1_50
GBytes *        gdk_text_layout_serialize             (GdkTextLayout                *layout,
                                                    GdkTextLayoutSerializeFlags   flags);

GDK_TEXT_AVAILABLE_IN_1_50
gboolean        gdk_text_layout_write_to_file         (GdkTextLayout                 *layout,
                                                    GdkTextLayoutSerializeFlags   flags,

                                                    const char                  *filename,
                                                    GError                     **error);

#define GDK_TEXT_LAYOUT_DESERIALIZE_ERROR (gdk_text_layout_deserialize_error_quark ())

/**
 * GdkTextLayoutDeserializeError:
 * @GDK_TEXT_LAYOUT_DESERIALIZE_INVALID: Unspecified error
 * @GDK_TEXT_LAYOUT_DESERIALIZE_INVALID_VALUE: A JSon value could not be
 *   interpreted
 * @GDK_TEXT_LAYOUT_DESERIALIZE_MISSING_VALUE: A required JSon member was
 *   not found
 *
 * Errors that can be returned by [func@GdkText.Layout.deserialize].
 *
 * Since: 1.50
 */
typedef enum {
  GDK_TEXT_LAYOUT_DESERIALIZE_INVALID,
  GDK_TEXT_LAYOUT_DESERIALIZE_INVALID_VALUE,
  GDK_TEXT_LAYOUT_DESERIALIZE_MISSING_VALUE,
} GdkTextLayoutDeserializeError;

GDK_TEXT_AVAILABLE_IN_1_50
GQuark          gdk_text_layout_deserialize_error_quark (void);

/**
 * GdkTextLayoutDeserializeFlags:
 * @GDK_TEXT_LAYOUT_DESERIALIZE_DEFAULT: Default behavior
 * @GDK_TEXT_LAYOUT_DESERIALIZE_CONTEXT: Apply context information
 *   from the serialization to the `GdkTextContext`
 *
 * Flags that influence the behavior of [func@GdkText.Layout.deserialize].
 *
 * New members may be added to this enumeration over time.
 *
 * Since: 1.50
 */
typedef enum {
  GDK_TEXT_LAYOUT_DESERIALIZE_DEFAULT = 0,
  GDK_TEXT_LAYOUT_DESERIALIZE_CONTEXT = 1 << 0,
} GdkTextLayoutDeserializeFlags;

GDK_TEXT_AVAILABLE_IN_1_50
GdkTextLayout *   gdk_text_layout_deserialize           (GdkTextContext                 *context,
                                                    GBytes                       *bytes,
                                                    GdkTextLayoutDeserializeFlags   flags,
                                                    GError                      **error);


#define GDK_TEXT_TYPE_LAYOUT_LINE (gdk_text_layout_line_get_type ())

GDK_TEXT_AVAILABLE_IN_ALL
GType    gdk_text_layout_line_get_type     (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_10
GdkTextLayoutLine *gdk_text_layout_line_ref   (GdkTextLayoutLine *line);
GDK_TEXT_AVAILABLE_IN_ALL
void             gdk_text_layout_line_unref (GdkTextLayoutLine *line);

GDK_TEXT_AVAILABLE_IN_1_50
int      gdk_text_layout_line_get_start_index (GdkTextLayoutLine *line);
GDK_TEXT_AVAILABLE_IN_1_50
int      gdk_text_layout_line_get_length      (GdkTextLayoutLine *line);
GDK_TEXT_AVAILABLE_IN_1_50
gboolean gdk_text_layout_line_is_paragraph_start (GdkTextLayoutLine *line);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextDirection gdk_text_layout_line_get_resolved_direction (GdkTextLayoutLine *line);

GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_layout_line_x_to_index   (GdkTextLayoutLine  *line,
					 int               x_pos,
					 int              *index_,
					 int              *trailing);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_line_index_to_x   (GdkTextLayoutLine  *line,
					 int               index_,
					 gboolean          trailing,
					 int              *x_pos);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_line_get_x_ranges (GdkTextLayoutLine  *line,
					 int               start_index,
					 int               end_index,
					 int             **ranges,
					 int              *n_ranges);
GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_line_get_extents  (GdkTextLayoutLine  *line,
					 GdkTextRectangle   *ink_rect,
					 GdkTextRectangle   *logical_rect);
GDK_TEXT_AVAILABLE_IN_1_44
void     gdk_text_layout_line_get_height   (GdkTextLayoutLine  *line,
					 int              *height);

GDK_TEXT_AVAILABLE_IN_ALL
void     gdk_text_layout_line_get_pixel_extents (GdkTextLayoutLine *layout_line,
					      GdkTextRectangle  *ink_rect,
					      GdkTextRectangle  *logical_rect);

typedef struct _PangoLayoutIter GdkTextLayoutIter;

#define GDK_TEXT_TYPE_LAYOUT_ITER         (gdk_text_layout_iter_get_type ())

GDK_TEXT_AVAILABLE_IN_ALL
GType            gdk_text_layout_iter_get_type (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLayoutIter *gdk_text_layout_get_iter  (GdkTextLayout     *layout);
GDK_TEXT_AVAILABLE_IN_1_20
GdkTextLayoutIter *gdk_text_layout_iter_copy (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_ALL
void             gdk_text_layout_iter_free (GdkTextLayoutIter *iter);

GDK_TEXT_AVAILABLE_IN_ALL
int              gdk_text_layout_iter_get_index  (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLayoutRun  *gdk_text_layout_iter_get_run    (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextLayoutRun  *gdk_text_layout_iter_get_run_readonly   (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextLayoutLine *gdk_text_layout_iter_get_line   (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextLayoutLine *gdk_text_layout_iter_get_line_readonly  (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean         gdk_text_layout_iter_at_last_line (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_1_20
GdkTextLayout     *gdk_text_layout_iter_get_layout (GdkTextLayoutIter *iter);

GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_layout_iter_next_char    (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_layout_iter_next_cluster (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_layout_iter_next_run     (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_layout_iter_next_line    (GdkTextLayoutIter *iter);

GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_layout_iter_get_char_extents    (GdkTextLayoutIter *iter,
					    GdkTextRectangle  *logical_rect);
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_layout_iter_get_cluster_extents (GdkTextLayoutIter *iter,
					    GdkTextRectangle  *ink_rect,
					    GdkTextRectangle  *logical_rect);
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_layout_iter_get_run_extents     (GdkTextLayoutIter *iter,
					    GdkTextRectangle  *ink_rect,
					    GdkTextRectangle  *logical_rect);
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_layout_iter_get_line_extents    (GdkTextLayoutIter *iter,
					    GdkTextRectangle  *ink_rect,
					    GdkTextRectangle  *logical_rect);
/* All the yranges meet, unlike the logical_rect's (i.e. the yranges
 * assign between-line spacing to the nearest line)
 */
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_layout_iter_get_line_yrange     (GdkTextLayoutIter *iter,
					    int             *y0_,
					    int             *y1_);
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_layout_iter_get_layout_extents  (GdkTextLayoutIter *iter,
					    GdkTextRectangle  *ink_rect,
					    GdkTextRectangle  *logical_rect);
GDK_TEXT_AVAILABLE_IN_ALL
int  gdk_text_layout_iter_get_baseline        (GdkTextLayoutIter *iter);
GDK_TEXT_AVAILABLE_IN_1_50
int  gdk_text_layout_iter_get_run_baseline    (GdkTextLayoutIter *iter);


G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextLayout, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextLayoutIter, gdk_text_layout_iter_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextLayoutLine, gdk_text_layout_line_unref)

G_END_DECLS

#endif /* __PANGO_LAYOUT_H__ */

