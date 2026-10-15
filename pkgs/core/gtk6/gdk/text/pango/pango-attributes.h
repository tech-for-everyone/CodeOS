/* GdkText
 * pango-attributes.h: Attributed text
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

#ifndef __PANGO_ATTRIBUTES_H__
#define __PANGO_ATTRIBUTES_H__

#include <pango/pango-font.h>
#include <pango/pango-color.h>
#include <glib-object.h>

G_BEGIN_DECLS


typedef struct _PangoAttribute        GdkTextAttribute;
typedef struct _PangoAttrClass        GdkTextAttrClass;

typedef struct _PangoAttrString       GdkTextAttrString;
typedef struct _PangoAttrLanguage     GdkTextAttrLanguage;
typedef struct _PangoAttrInt          GdkTextAttrInt;
typedef struct _PangoAttrSize         GdkTextAttrSize;
typedef struct _PangoAttrFloat        GdkTextAttrFloat;
typedef struct _PangoAttrColor        GdkTextAttrColor;
typedef struct _PangoAttrFontDesc     GdkTextAttrFontDesc;
typedef struct _PangoAttrShape        GdkTextAttrShape;
typedef struct _PangoAttrFontFeatures GdkTextAttrFontFeatures;

/**
 * GdkTextAttrType:
 * @GDK_TEXT_ATTR_INVALID: does not happen
 * @GDK_TEXT_ATTR_LANGUAGE: language ([struct@GdkText.AttrLanguage])
 * @GDK_TEXT_ATTR_FAMILY: font family name list ([struct@GdkText.AttrString])
 * @GDK_TEXT_ATTR_STYLE: font slant style ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_WEIGHT: font weight ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_VARIANT: font variant (normal or small caps) ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_STRETCH: font stretch ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_SIZE: font size in points scaled by %GDK_TEXT_SCALE ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_FONT_DESC: font description ([struct@GdkText.AttrFontDesc])
 * @GDK_TEXT_ATTR_FOREGROUND: foreground color ([struct@GdkText.AttrColor])
 * @GDK_TEXT_ATTR_BACKGROUND: background color ([struct@GdkText.AttrColor])
 * @GDK_TEXT_ATTR_UNDERLINE: whether the text has an underline ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_STRIKETHROUGH: whether the text is struck-through ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_RISE: baseline displacement ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_SHAPE: shape ([struct@GdkText.AttrShape])
 * @GDK_TEXT_ATTR_SCALE: font size scale factor ([struct@GdkText.AttrFloat])
 * @GDK_TEXT_ATTR_FALLBACK: whether fallback is enabled ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_LETTER_SPACING: letter spacing ([struct@GdkTextAttrInt])
 * @GDK_TEXT_ATTR_UNDERLINE_COLOR: underline color ([struct@GdkText.AttrColor])
 * @GDK_TEXT_ATTR_STRIKETHROUGH_COLOR: strikethrough color ([struct@GdkText.AttrColor])
 * @GDK_TEXT_ATTR_ABSOLUTE_SIZE: font size in pixels scaled by %GDK_TEXT_SCALE ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_GRAVITY: base text gravity ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_GRAVITY_HINT: gravity hint ([struct@GdkText.AttrInt])
 * @GDK_TEXT_ATTR_FONT_FEATURES: OpenType font features ([struct@GdkText.AttrFontFeatures]). Since 1.38
 * @GDK_TEXT_ATTR_FOREGROUND_ALPHA: foreground alpha ([struct@GdkText.AttrInt]). Since 1.38
 * @GDK_TEXT_ATTR_BACKGROUND_ALPHA: background alpha ([struct@GdkText.AttrInt]). Since 1.38
 * @GDK_TEXT_ATTR_ALLOW_BREAKS: whether breaks are allowed ([struct@GdkText.AttrInt]). Since 1.44
 * @GDK_TEXT_ATTR_SHOW: how to render invisible characters ([struct@GdkText.AttrInt]). Since 1.44
 * @GDK_TEXT_ATTR_INSERT_HYPHENS: whether to insert hyphens at intra-word line breaks ([struct@GdkText.AttrInt]). Since 1.44
 * @GDK_TEXT_ATTR_OVERLINE: whether the text has an overline ([struct@GdkText.AttrInt]). Since 1.46
 * @GDK_TEXT_ATTR_OVERLINE_COLOR: overline color ([struct@GdkText.AttrColor]). Since 1.46
 * @GDK_TEXT_ATTR_LINE_HEIGHT: line height factor ([struct@GdkText.AttrFloat]). Since: 1.50
 * @GDK_TEXT_ATTR_ABSOLUTE_LINE_HEIGHT: line height ([struct@GdkText.AttrInt]). Since: 1.50
 * @GDK_TEXT_ATTR_WORD: override segmentation to classify the range of the attribute as a single word ([struct@GdkText.AttrInt]). Since 1.50
 * @GDK_TEXT_ATTR_SENTENCE: override segmentation to classify the range of the attribute as a single sentence ([struct@GdkText.AttrInt]). Since 1.50
 * @GDK_TEXT_ATTR_BASELINE_SHIFT: baseline displacement ([struct@GdkText.AttrInt]). Since 1.50
 * @GDK_TEXT_ATTR_FONT_SCALE: font-relative size change ([struct@GdkText.AttrInt]). Since 1.50
 * @GDK_TEXT_ATTR_WIDTH: font width ([struct@GdkText.AttrInt]). Since: 1.58
 *
 * The `GdkTextAttrType` distinguishes between different types of attributes.
 *
 * Along with the predefined values, it is possible to allocate additional
 * values for custom attributes using [func@AttrType.register]. The predefined
 * values are given below. The type of structure used to store the attribute is
 * listed in parentheses after the description.
 */
typedef enum
{
  GDK_TEXT_ATTR_INVALID,           /* 0 is an invalid attribute type */
  GDK_TEXT_ATTR_LANGUAGE,          /* GdkTextAttrLanguage */
  GDK_TEXT_ATTR_FAMILY,            /* GdkTextAttrString */
  GDK_TEXT_ATTR_STYLE,             /* GdkTextAttrInt */
  GDK_TEXT_ATTR_WEIGHT,            /* GdkTextAttrInt */
  GDK_TEXT_ATTR_VARIANT,           /* GdkTextAttrInt */
  GDK_TEXT_ATTR_STRETCH,           /* GdkTextAttrInt */
  GDK_TEXT_ATTR_SIZE,              /* GdkTextAttrSize */
  GDK_TEXT_ATTR_FONT_DESC,         /* GdkTextAttrFontDesc */
  GDK_TEXT_ATTR_FOREGROUND,        /* GdkTextAttrColor */
  GDK_TEXT_ATTR_BACKGROUND,        /* GdkTextAttrColor */
  GDK_TEXT_ATTR_UNDERLINE,         /* GdkTextAttrInt */
  GDK_TEXT_ATTR_STRIKETHROUGH,     /* GdkTextAttrInt */
  GDK_TEXT_ATTR_RISE,              /* GdkTextAttrInt */
  GDK_TEXT_ATTR_SHAPE,             /* GdkTextAttrShape */
  GDK_TEXT_ATTR_SCALE,             /* GdkTextAttrFloat */
  GDK_TEXT_ATTR_FALLBACK,          /* GdkTextAttrInt */
  GDK_TEXT_ATTR_LETTER_SPACING,    /* GdkTextAttrInt */
  GDK_TEXT_ATTR_UNDERLINE_COLOR,   /* GdkTextAttrColor */
  GDK_TEXT_ATTR_STRIKETHROUGH_COLOR,/* GdkTextAttrColor */
  GDK_TEXT_ATTR_ABSOLUTE_SIZE,     /* GdkTextAttrSize */
  GDK_TEXT_ATTR_GRAVITY,           /* GdkTextAttrInt */
  GDK_TEXT_ATTR_GRAVITY_HINT,      /* GdkTextAttrInt */
  GDK_TEXT_ATTR_FONT_FEATURES,     /* GdkTextAttrFontFeatures */
  GDK_TEXT_ATTR_FOREGROUND_ALPHA,  /* GdkTextAttrInt */
  GDK_TEXT_ATTR_BACKGROUND_ALPHA,  /* GdkTextAttrInt */
  GDK_TEXT_ATTR_ALLOW_BREAKS,      /* GdkTextAttrInt */
  GDK_TEXT_ATTR_SHOW,              /* GdkTextAttrInt */
  GDK_TEXT_ATTR_INSERT_HYPHENS,    /* GdkTextAttrInt */
  GDK_TEXT_ATTR_OVERLINE,          /* GdkTextAttrInt */
  GDK_TEXT_ATTR_OVERLINE_COLOR,    /* GdkTextAttrColor */
  GDK_TEXT_ATTR_LINE_HEIGHT,       /* GdkTextAttrFloat */
  GDK_TEXT_ATTR_ABSOLUTE_LINE_HEIGHT, /* GdkTextAttrInt */
  GDK_TEXT_ATTR_TEXT_TRANSFORM,    /* GdkTextAttrInt */
  GDK_TEXT_ATTR_WORD,              /* GdkTextAttrInt */
  GDK_TEXT_ATTR_SENTENCE,          /* GdkTextAttrInt */
  GDK_TEXT_ATTR_BASELINE_SHIFT,    /* GdkTextAttrSize */
  GDK_TEXT_ATTR_FONT_SCALE,        /* GdkTextAttrInt */
  GDK_TEXT_ATTR_WIDTH,             /* GdkTextAttrInt */
} GdkTextAttrType;

/**
 * GdkTextUnderline:
 * @GDK_TEXT_UNDERLINE_NONE: no underline should be drawn
 * @GDK_TEXT_UNDERLINE_SINGLE: a single underline should be drawn
 * @GDK_TEXT_UNDERLINE_DOUBLE: a double underline should be drawn
 * @GDK_TEXT_UNDERLINE_LOW: a single underline should be drawn at a
 *   position beneath the ink extents of the text being
 *   underlined. This should be used only for underlining
 *   single characters, such as for keyboard accelerators.
 *   %GDK_TEXT_UNDERLINE_SINGLE should be used for extended
 *   portions of text.
 * @GDK_TEXT_UNDERLINE_ERROR: an underline indicating an error should
 *   be drawn below. The exact style of rendering is up to the
 *   `GdkTextRenderer` in use, but typical styles include wavy
 *   or dotted lines.
 *   This underline is typically used to indicate an error such
 *   as a possible mispelling; in some cases a contrasting color
 *   may automatically be used. This type of underlining is
 *   available since GdkText 1.4.
 * @GDK_TEXT_UNDERLINE_SINGLE_LINE: Like @GDK_TEXT_UNDERLINE_SINGLE, but
 *   drawn continuously across multiple runs. This type
 *   of underlining is available since GdkText 1.46.
 * @GDK_TEXT_UNDERLINE_DOUBLE_LINE: Like @GDK_TEXT_UNDERLINE_DOUBLE, but
 *   drawn continuously across multiple runs. This type
 *   of underlining is available since GdkText 1.46.
 * @GDK_TEXT_UNDERLINE_ERROR_LINE: Like @GDK_TEXT_UNDERLINE_ERROR, but
 *   drawn continuously across multiple runs. This type
 *   of underlining is available since GdkText 1.46.
 *
 * The `GdkTextUnderline` enumeration is used to specify whether text
 * should be underlined, and if so, the type of underlining.
 */
typedef enum {
  GDK_TEXT_UNDERLINE_NONE,
  GDK_TEXT_UNDERLINE_SINGLE,
  GDK_TEXT_UNDERLINE_DOUBLE,
  GDK_TEXT_UNDERLINE_LOW,
  GDK_TEXT_UNDERLINE_ERROR,
  GDK_TEXT_UNDERLINE_SINGLE_LINE,
  GDK_TEXT_UNDERLINE_DOUBLE_LINE,
  GDK_TEXT_UNDERLINE_ERROR_LINE
} GdkTextUnderline;


/**
 * GdkTextOverline:
 * @GDK_TEXT_OVERLINE_NONE: no overline should be drawn
 * @GDK_TEXT_OVERLINE_SINGLE: Draw a single line above the ink
 *   extents of the text being underlined.
 *
 * The `GdkTextOverline` enumeration is used to specify whether text
 * should be overlined, and if so, the type of line.
 *
 * Since: 1.46
 */
typedef enum {
  GDK_TEXT_OVERLINE_NONE,
  GDK_TEXT_OVERLINE_SINGLE
} GdkTextOverline;

/**
 * GdkTextShowFlags:
 * @GDK_TEXT_SHOW_NONE: No special treatment for invisible characters
 * @GDK_TEXT_SHOW_SPACES: Render spaces, tabs and newlines visibly
 * @GDK_TEXT_SHOW_LINE_BREAKS: Render line breaks visibly
 * @GDK_TEXT_SHOW_IGNORABLES: Render default-ignorable Unicode
 *   characters visibly
 *
 * These flags affect how GdkText treats characters that are normally
 * not visible in the output.
 *
 * Since: 1.44
 */
typedef enum {
  GDK_TEXT_SHOW_NONE        = 0,
  GDK_TEXT_SHOW_SPACES      = 1 << 0,
  GDK_TEXT_SHOW_LINE_BREAKS = 1 << 1,
  GDK_TEXT_SHOW_IGNORABLES  = 1 << 2
} GdkTextShowFlags;

/**
 * GdkTextTextTransform:
 * @GDK_TEXT_TEXT_TRANSFORM_NONE: Leave text unchanged
 * @GDK_TEXT_TEXT_TRANSFORM_LOWERCASE: Display letters and numbers as lowercase
 * @GDK_TEXT_TEXT_TRANSFORM_UPPERCASE: Display letters and numbers as uppercase
 * @GDK_TEXT_TEXT_TRANSFORM_CAPITALIZE: Display the first character of a word
 *   in titlecase
 *
 * An enumeration that affects how GdkText treats characters during shaping.
 *
 * Since: 1.50
 */
typedef enum {
  GDK_TEXT_TEXT_TRANSFORM_NONE,
  GDK_TEXT_TEXT_TRANSFORM_LOWERCASE,
  GDK_TEXT_TEXT_TRANSFORM_UPPERCASE,
  GDK_TEXT_TEXT_TRANSFORM_CAPITALIZE,
} GdkTextTextTransform;

/**
 * GdkTextBaselineShift:
 * @GDK_TEXT_BASELINE_SHIFT_NONE: Leave the baseline unchanged
 * @GDK_TEXT_BASELINE_SHIFT_SUPERSCRIPT: Shift the baseline to the superscript position,
 *   relative to the previous run
 * @GDK_TEXT_BASELINE_SHIFT_SUBSCRIPT: Shift the baseline to the subscript position,
 *   relative to the previous run
 *
 * An enumeration that affects baseline shifts between runs.
 *
 * Since: 1.50
 */
typedef enum {
  GDK_TEXT_BASELINE_SHIFT_NONE,
  GDK_TEXT_BASELINE_SHIFT_SUPERSCRIPT,
  GDK_TEXT_BASELINE_SHIFT_SUBSCRIPT,
} GdkTextBaselineShift;

/**
 * GdkTextFontScale:
 * @GDK_TEXT_FONT_SCALE_NONE: Leave the font size unchanged
 * @GDK_TEXT_FONT_SCALE_SUPERSCRIPT: Change the font to a size suitable for superscripts
 * @GDK_TEXT_FONT_SCALE_SUBSCRIPT: Change the font to a size suitable for subscripts
 * @GDK_TEXT_FONT_SCALE_SMALL_CAPS: Change the font to a size suitable for Small Caps
 *
 * An enumeration that affects font sizes for superscript
 * and subscript positioning and for (emulated) Small Caps.
 *
 * Since: 1.50
 */
typedef enum {
  GDK_TEXT_FONT_SCALE_NONE,
  GDK_TEXT_FONT_SCALE_SUPERSCRIPT,
  GDK_TEXT_FONT_SCALE_SUBSCRIPT,
  GDK_TEXT_FONT_SCALE_SMALL_CAPS,
} GdkTextFontScale;

/**
 * GDK_TEXT_ATTR_INDEX_FROM_TEXT_BEGINNING:
 *
 * Value for @start_index in `GdkTextAttribute` that indicates
 * the beginning of the text.
 *
 * Since: 1.24
 */
#define GDK_TEXT_ATTR_INDEX_FROM_TEXT_BEGINNING ((guint)0)

/**
 * GDK_TEXT_ATTR_INDEX_TO_TEXT_END: (value 4294967295)
 *
 * Value for @end_index in `GdkTextAttribute` that indicates
 * the end of the text.
 *
 * Since: 1.24
 */
#define GDK_TEXT_ATTR_INDEX_TO_TEXT_END ((guint)(G_MAXUINT + 0))

/**
 * GdkTextAttribute: (copy-func gdk_text_attribute_copy) (free-func gdk_text_attribute_destroy)
 * @klass: the class structure holding information about the type of the attribute
 * @start_index: the start index of the range (in bytes).
 * @end_index: end index of the range (in bytes). The character at this index
 *   is not included in the range.
 *
 * The `GdkTextAttribute` structure represents the common portions of all
 * attributes.
 *
 * Particular types of attributes include this structure as their initial
 * portion. The common portion of the attribute holds the range to which
 * the value in the type-specific part of the attribute applies and should
 * be initialized using [method@GdkText.Attribute.init]. By default, an attribute
 * will have an all-inclusive range of [0,%G_MAXUINT].
 */
struct _PangoAttribute
{
  const GdkTextAttrClass *klass;
  guint start_index;
  guint end_index;
};

/**
 * GdkTextAttrFilterFunc:
 * @attribute: a GdkText attribute
 * @user_data: user data passed to the function
 *
 * Type of a function filtering a list of attributes.
 *
 * Return value: %TRUE if the attribute should be selected for
 *   filtering, %FALSE otherwise.
 */
typedef gboolean (*GdkTextAttrFilterFunc) (GdkTextAttribute *attribute,
                                         gpointer        user_data);

/**
 * GdkTextAttrDataCopyFunc:
 * @user_data: user data to copy
 *
 * Type of a function that can duplicate user data for an attribute.
 *
 * Return value: new copy of @user_data.
 **/
typedef gpointer (*GdkTextAttrDataCopyFunc) (gconstpointer user_data);

/**
 * GdkTextAttrClass:
 * @type: the type ID for this attribute
 * @copy: function to duplicate an attribute of this type
 *   (see [method@GdkText.Attribute.copy])
 * @destroy: function to free an attribute of this type
 *   (see [method@GdkText.Attribute.destroy])
 * @equal: function to check two attributes of this type for equality
 *   (see [method@GdkText.Attribute.equal])
 *
 * The `GdkTextAttrClass` structure stores the type and operations for
 * a particular type of attribute.
 *
 * The functions in this structure should not be called directly. Instead,
 * one should use the wrapper functions provided for `GdkTextAttribute`.
 */
struct _PangoAttrClass
{
  /*< public >*/
  GdkTextAttrType type;
  GdkTextAttribute * (*copy) (const GdkTextAttribute *attr);
  void             (*destroy) (GdkTextAttribute *attr);
  gboolean         (*equal) (const GdkTextAttribute *attr1, const GdkTextAttribute *attr2);
};

/**
 * GdkTextAttrString:
 * @attr: the common portion of the attribute
 * @value: the string which is the value of the attribute
 *
 * The `GdkTextAttrString` structure is used to represent attributes with
 * a string value.
 */
struct _PangoAttrString
{
  GdkTextAttribute attr;
  char *value;
};
/**
 * GdkTextAttrLanguage:
 * @attr: the common portion of the attribute
 * @value: the `GdkTextLanguage` which is the value of the attribute
 *
 * The `GdkTextAttrLanguage` structure is used to represent attributes that
 * are languages.
 */
struct _PangoAttrLanguage
{
  GdkTextAttribute attr;
  GdkTextLanguage *value;
};
/**
 * GdkTextAttrInt:
 * @attr: the common portion of the attribute
 * @value: the value of the attribute
 *
 * The `GdkTextAttrInt` structure is used to represent attributes with
 * an integer or enumeration value.
 */
struct _PangoAttrInt
{
  GdkTextAttribute attr;
  int value;
};
/**
 * GdkTextAttrFloat:
 * @attr: the common portion of the attribute
 * @value: the value of the attribute
 *
 * The `GdkTextAttrFloat` structure is used to represent attributes with
 * a float or double value.
 */
struct _PangoAttrFloat
{
  GdkTextAttribute attr;
  double value;
};
/**
 * GdkTextAttrColor:
 * @attr: the common portion of the attribute
 * @color: the `GdkTextColor` which is the value of the attribute
 *
 * The `GdkTextAttrColor` structure is used to represent attributes that
 * are colors.
 */
struct _PangoAttrColor
{
  GdkTextAttribute attr;
  GdkTextColor color;
};

/**
 * GdkTextAttrSize:
 * @attr: the common portion of the attribute
 * @size: size of font, in units of 1/%GDK_TEXT_SCALE of a point (for
 *   %GDK_TEXT_ATTR_SIZE) or of a device unit (for %GDK_TEXT_ATTR_ABSOLUTE_SIZE)
 * @absolute: whether the font size is in device units or points.
 *   This field is only present for compatibility with GdkText-1.8.0
 *   (%GDK_TEXT_ATTR_ABSOLUTE_SIZE was added in 1.8.1); and always will
 *   be %FALSE for %GDK_TEXT_ATTR_SIZE and %TRUE for %GDK_TEXT_ATTR_ABSOLUTE_SIZE.
 *
 * The `GdkTextAttrSize` structure is used to represent attributes which
 * set font size.
 */
struct _PangoAttrSize
{
  GdkTextAttribute attr;
  int size;
  guint absolute : 1;
};

/**
 * GdkTextAttrShape:
 * @attr: the common portion of the attribute
 * @ink_rect: the ink rectangle to restrict to
 * @logical_rect: the logical rectangle to restrict to
 * @data: user data set (see [func@GdkText.AttrShape.new_with_data])
 * @copy_func: copy function for the user data
 * @destroy_func: destroy function for the user data
 *
 * The `GdkTextAttrShape` structure is used to represent attributes which
 * impose shape restrictions.
 */
struct _PangoAttrShape
{
  GdkTextAttribute attr;
  GdkTextRectangle ink_rect;
  GdkTextRectangle logical_rect;

  gpointer              data;
  GdkTextAttrDataCopyFunc copy_func;
  GDestroyNotify        destroy_func;
};

/**
 * GdkTextAttrFontDesc:
 * @attr: the common portion of the attribute
 * @desc: the font description which is the value of this attribute
 *
 * The `GdkTextAttrFontDesc` structure is used to store an attribute that
 * sets all aspects of the font description at once.
 */
struct _PangoAttrFontDesc
{
  GdkTextAttribute attr;
  GdkTextFontDescription *desc;
};

/**
 * GdkTextAttrFontFeatures:
 * @attr: the common portion of the attribute
 * @features: the features, as a string in CSS syntax
 *
 * The `GdkTextAttrFontFeatures` structure is used to represent OpenType
 * font features as an attribute.
 *
 * Since: 1.38
 */
struct _PangoAttrFontFeatures
{
  GdkTextAttribute attr;
  gchar *features;
};

GDK_TEXT_AVAILABLE_IN_ALL
GType                   gdk_text_attribute_get_type                (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttrType           gdk_text_attr_type_register                (const char                 *name);
GDK_TEXT_AVAILABLE_IN_1_22
const char *            gdk_text_attr_type_get_name                (GdkTextAttrType               type) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_1_20
void                    gdk_text_attribute_init                    (GdkTextAttribute             *attr,
                                                                 const GdkTextAttrClass       *klass);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attribute_copy                    (const GdkTextAttribute       *attr);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attribute_destroy                 (GdkTextAttribute             *attr);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean                gdk_text_attribute_equal                   (const GdkTextAttribute       *attr1,
                                                                 const GdkTextAttribute       *attr2) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_language_new                 (GdkTextLanguage              *language);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_family_new                   (const char                 *family);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_foreground_new               (guint16                     red,
                                                                 guint16                     green,
                                                                 guint16                     blue);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_background_new               (guint16                     red,
                                                                 guint16                     green,
                                                                 guint16                     blue);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_size_new                     (int                         size);
GDK_TEXT_AVAILABLE_IN_1_8
GdkTextAttribute *        gdk_text_attr_size_new_absolute            (int                         size);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_style_new                    (GdkTextStyle                  style);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_weight_new                   (GdkTextWeight                 weight);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_variant_new                  (GdkTextVariant                variant);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_stretch_new                  (GdkTextStretch                stretch);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_font_desc_new                (const GdkTextFontDescription *desc);

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_underline_new                (GdkTextUnderline              underline);
GDK_TEXT_AVAILABLE_IN_1_8
GdkTextAttribute *        gdk_text_attr_underline_color_new          (guint16                     red,
                                                                 guint16                     green,
                                                                 guint16                     blue);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_strikethrough_new            (gboolean                    strikethrough);
GDK_TEXT_AVAILABLE_IN_1_8
GdkTextAttribute *        gdk_text_attr_strikethrough_color_new      (guint16                     red,
                                                                 guint16                     green,
                                                                 guint16                     blue);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_rise_new                     (int                         rise);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttribute *        gdk_text_attr_baseline_shift_new           (int                         shift);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttribute *        gdk_text_attr_font_scale_new               (GdkTextFontScale              scale);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_scale_new                    (double                      scale_factor);
GDK_TEXT_AVAILABLE_IN_1_4
GdkTextAttribute *        gdk_text_attr_fallback_new                 (gboolean                    enable_fallback);
GDK_TEXT_AVAILABLE_IN_1_6
GdkTextAttribute *        gdk_text_attr_letter_spacing_new           (int                         letter_spacing);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_shape_new                    (const GdkTextRectangle        *ink_rect,
                                                                 const GdkTextRectangle        *logical_rect);
GDK_TEXT_AVAILABLE_IN_1_8
GdkTextAttribute *        gdk_text_attr_shape_new_with_data          (const GdkTextRectangle        *ink_rect,
                                                                 const GdkTextRectangle        *logical_rect,
                                                                 gpointer                     data,
                                                                 GdkTextAttrDataCopyFunc        copy_func,
                                                                 GDestroyNotify               destroy_func);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextAttribute *        gdk_text_attr_gravity_new                  (GdkTextGravity                 gravity);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextAttribute *        gdk_text_attr_gravity_hint_new             (GdkTextGravityHint             hint);
GDK_TEXT_AVAILABLE_IN_1_38
GdkTextAttribute *        gdk_text_attr_font_features_new            (const char                  *features);
GDK_TEXT_AVAILABLE_IN_1_38
GdkTextAttribute *        gdk_text_attr_foreground_alpha_new         (guint16                      alpha);
GDK_TEXT_AVAILABLE_IN_1_38
GdkTextAttribute *        gdk_text_attr_background_alpha_new         (guint16                      alpha);
GDK_TEXT_AVAILABLE_IN_1_44
GdkTextAttribute *        gdk_text_attr_allow_breaks_new             (gboolean                     allow_breaks);

GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttribute *        gdk_text_attr_word_new                     (void);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttribute *        gdk_text_attr_sentence_new                 (void);

GDK_TEXT_AVAILABLE_IN_1_44
GdkTextAttribute *        gdk_text_attr_insert_hyphens_new           (gboolean                     insert_hyphens);
GDK_TEXT_AVAILABLE_IN_1_46
GdkTextAttribute *        gdk_text_attr_overline_new                 (GdkTextOverline                overline);
GDK_TEXT_AVAILABLE_IN_1_46
GdkTextAttribute *        gdk_text_attr_overline_color_new           (guint16                      red,
                                                                 guint16                      green,
                                                                 guint16                      blue);
GDK_TEXT_AVAILABLE_IN_1_44
GdkTextAttribute *        gdk_text_attr_show_new                     (GdkTextShowFlags               flags);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttribute *        gdk_text_attr_line_height_new              (double                       factor);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttribute *        gdk_text_attr_line_height_new_absolute     (int                          height);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttribute *        gdk_text_attr_text_transform_new           (GdkTextTextTransform transform);
GDK_TEXT_AVAILABLE_IN_1_58
GdkTextAttribute *        gdk_text_attr_width_new                    (GdkTextWidth                  width);

GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrString       * gdk_text_attribute_as_string               (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrLanguage     * gdk_text_attribute_as_language             (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrInt          * gdk_text_attribute_as_int                  (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrSize         * gdk_text_attribute_as_size                 (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrFloat        * gdk_text_attribute_as_float                (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrColor        * gdk_text_attribute_as_color                (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrFontDesc     * gdk_text_attribute_as_font_desc            (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrShape        * gdk_text_attribute_as_shape                (GdkTextAttribute              *attr);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrFontFeatures * gdk_text_attribute_as_font_features        (GdkTextAttribute              *attr);

/* Attribute lists */

typedef struct _PangoAttrList     GdkTextAttrList;
typedef struct _PangoAttrIterator GdkTextAttrIterator;

#define GDK_TEXT_TYPE_ATTR_LIST gdk_text_attr_list_get_type ()

/**
 * GdkTextAttrIterator:
 *
 * A `GdkTextAttrIterator` is used to iterate through a `GdkTextAttrList`.
 *
 * A new iterator is created with [method@GdkText.AttrList.get_iterator].
 * Once the iterator is created, it can be advanced through the style
 * changes in the text using [method@GdkText.AttrIterator.next]. At each
 * style change, the range of the current style segment and the attributes
 * currently in effect can be queried.
 */

/**
 * GdkTextAttrList:
 *
 * A `GdkTextAttrList` represents a list of attributes that apply to a section
 * of text.
 *
 * The attributes in a `GdkTextAttrList` are, in general, allowed to overlap in
 * an arbitrary fashion. However, if the attributes are manipulated only through
 * [method@GdkText.AttrList.change], the overlap between properties will meet
 * stricter criteria.
 *
 * Since the `GdkTextAttrList` structure is stored as a linear list, it is not
 * suitable for storing attributes for large amounts of text. In general, you
 * should not use a single `GdkTextAttrList` for more than one paragraph of text.
 */

GDK_TEXT_AVAILABLE_IN_ALL
GType                   gdk_text_attr_list_get_type        (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttrList *         gdk_text_attr_list_new             (void);
GDK_TEXT_AVAILABLE_IN_1_10
GdkTextAttrList *         gdk_text_attr_list_ref             (GdkTextAttrList         *list);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_list_unref           (GdkTextAttrList         *list);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttrList *         gdk_text_attr_list_copy            (GdkTextAttrList         *list);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_list_insert          (GdkTextAttrList         *list,
                                                         GdkTextAttribute        *attr);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_list_insert_before   (GdkTextAttrList         *list,
                                                         GdkTextAttribute        *attr);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_list_change          (GdkTextAttrList         *list,
                                                         GdkTextAttribute        *attr);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_list_splice          (GdkTextAttrList         *list,
                                                         GdkTextAttrList         *other,
                                                         int                    pos,
                                                         int                    len);
GDK_TEXT_AVAILABLE_IN_1_44
void                    gdk_text_attr_list_update          (GdkTextAttrList         *list,
                                                         int                    pos,
                                                         int                    remove,
                                                         int                    add);

GDK_TEXT_AVAILABLE_IN_1_2
GdkTextAttrList *         gdk_text_attr_list_filter          (GdkTextAttrList         *list,
                                                         GdkTextAttrFilterFunc    func,
                                                         gpointer               data);

GDK_TEXT_AVAILABLE_IN_1_44
GSList *                gdk_text_attr_list_get_attributes  (GdkTextAttrList         *list);

GDK_TEXT_AVAILABLE_IN_1_46
gboolean                gdk_text_attr_list_equal           (GdkTextAttrList         *list,
                                                         GdkTextAttrList         *other_list);

GDK_TEXT_AVAILABLE_IN_1_50
char *                  gdk_text_attr_list_to_string       (GdkTextAttrList         *list);
GDK_TEXT_AVAILABLE_IN_1_50
GdkTextAttrList *         gdk_text_attr_list_from_string     (const char            *text);

GDK_TEXT_AVAILABLE_IN_1_44
GType                   gdk_text_attr_iterator_get_type    (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttrIterator *     gdk_text_attr_list_get_iterator    (GdkTextAttrList         *list);

GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_iterator_range       (GdkTextAttrIterator     *iterator,
                                                         int                   *start,
                                                         int                   *end);
GDK_TEXT_AVAILABLE_IN_ALL
gboolean                gdk_text_attr_iterator_next        (GdkTextAttrIterator     *iterator);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttrIterator *     gdk_text_attr_iterator_copy        (GdkTextAttrIterator     *iterator);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_iterator_destroy     (GdkTextAttrIterator     *iterator);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextAttribute *        gdk_text_attr_iterator_get         (GdkTextAttrIterator     *iterator,
                                                         GdkTextAttrType          type);
GDK_TEXT_AVAILABLE_IN_ALL
void                    gdk_text_attr_iterator_get_font    (GdkTextAttrIterator     *iterator,
                                                         GdkTextFontDescription  *desc,
                                                         GdkTextLanguage        **language,
                                                         GSList               **extra_attrs);
GDK_TEXT_AVAILABLE_IN_1_2
GSList *                gdk_text_attr_iterator_get_attrs   (GdkTextAttrIterator     *iterator);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextAttribute, gdk_text_attribute_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextAttrList, gdk_text_attr_list_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextAttrIterator, gdk_text_attr_iterator_destroy)

G_END_DECLS

#endif /* __PANGO_ATTRIBUTES_H__ */
