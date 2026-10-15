/* GdkText
 * pango-font.h: Font handling
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

#ifndef __PANGO_FONT_H__
#define __PANGO_FONT_H__

#include <pango/pango-coverage.h>
#include <pango/pango-types.h>

#include <glib-object.h>
#include <hb.h>

G_BEGIN_DECLS

/**
 * GdkTextFontDescription:
 *
 * A `GdkTextFontDescription` describes a font in an implementation-independent
 * manner.
 *
 * `GdkTextFontDescription` structures are used both to list what fonts are
 * available on the system and also for specifying the characteristics of
 * a font to load.
 */
typedef struct _PangoFontDescription GdkTextFontDescription;

/**
 * GdkTextFontMetrics:
 *
 * A `GdkTextFontMetrics` structure holds the overall metric information
 * for a font.
 *
 * The information in a `GdkTextFontMetrics` structure may be restricted
 * to a script. The fields of this structure are private to implementations
 * of a font backend. See the documentation of the corresponding getters
 * for documentation of their meaning.
 *
 * For an overview of the most important metrics, see:
 *
 * <picture>
 *   <source srcset="fontmetrics-dark.png" media="(prefers-color-scheme: dark)">
 *   <img alt="Font metrics" src="fontmetrics-light.png">
 * </picture>

 */
typedef struct _PangoFontMetrics GdkTextFontMetrics;

/**
 * GdkTextStyle:
 * @GDK_TEXT_STYLE_NORMAL: the font is upright.
 * @GDK_TEXT_STYLE_OBLIQUE: the font is slanted, but in a roman style.
 * @GDK_TEXT_STYLE_ITALIC: the font is slanted in an italic style.
 *
 * An enumeration specifying the various slant styles possible for a font.
 **/
typedef enum {
  GDK_TEXT_STYLE_NORMAL,
  GDK_TEXT_STYLE_OBLIQUE,
  GDK_TEXT_STYLE_ITALIC
} GdkTextStyle;

/**
 * GdkTextVariant:
 * @GDK_TEXT_VARIANT_NORMAL: A normal font.
 * @GDK_TEXT_VARIANT_SMALL_CAPS: A font with the lower case characters
 *   replaced by smaller variants of the capital characters.
 * @GDK_TEXT_VARIANT_ALL_SMALL_CAPS: A font with all characters
 *   replaced by smaller variants of the capital characters. Since: 1.50
 * @GDK_TEXT_VARIANT_PETITE_CAPS: A font with the lower case characters
 *   replaced by smaller variants of the capital characters.
 *   Petite Caps can be even smaller than Small Caps. Since: 1.50
 * @GDK_TEXT_VARIANT_ALL_PETITE_CAPS: A font with all characters
 *   replaced by smaller variants of the capital characters.
 *   Petite Caps can be even smaller than Small Caps. Since: 1.50
 * @GDK_TEXT_VARIANT_UNICASE: A font with the upper case characters
 *   replaced by smaller variants of the capital letters. Since: 1.50
 * @GDK_TEXT_VARIANT_TITLE_CAPS: A font with capital letters that
 *   are more suitable for all-uppercase titles. Since: 1.50
 *
 * An enumeration specifying capitalization variant of the font.
 */
typedef enum {
  GDK_TEXT_VARIANT_NORMAL,
  GDK_TEXT_VARIANT_SMALL_CAPS,
  GDK_TEXT_VARIANT_ALL_SMALL_CAPS,
  GDK_TEXT_VARIANT_PETITE_CAPS,
  GDK_TEXT_VARIANT_ALL_PETITE_CAPS,
  GDK_TEXT_VARIANT_UNICASE,
  GDK_TEXT_VARIANT_TITLE_CAPS
} GdkTextVariant;

/**
 * GdkTextWeight:
 * @GDK_TEXT_WEIGHT_THIN: the thin weight (= 100) Since: 1.24
 * @GDK_TEXT_WEIGHT_ULTRALIGHT: the ultralight weight (= 200)
 * @GDK_TEXT_WEIGHT_LIGHT: the light weight (= 300)
 * @GDK_TEXT_WEIGHT_SEMILIGHT: the semilight weight (= 350) Since: 1.36.7
 * @GDK_TEXT_WEIGHT_BOOK: the book weight (= 380) Since: 1.24)
 * @GDK_TEXT_WEIGHT_NORMAL: the default weight (= 400)
 * @GDK_TEXT_WEIGHT_MEDIUM: the medium weight (= 500) Since: 1.24
 * @GDK_TEXT_WEIGHT_SEMIBOLD: the semibold weight (= 600)
 * @GDK_TEXT_WEIGHT_BOLD: the bold weight (= 700)
 * @GDK_TEXT_WEIGHT_ULTRABOLD: the ultrabold weight (= 800)
 * @GDK_TEXT_WEIGHT_HEAVY: the heavy weight (= 900)
 * @GDK_TEXT_WEIGHT_ULTRAHEAVY: the ultraheavy weight (= 1000) Since: 1.24
 *
 * An enumeration specifying the weight (boldness) of a font.
 *
 * Weight is specified as a numeric value ranging from 100 to 1000.
 * This enumeration simply provides some common, predefined values.
 */
typedef enum {
  GDK_TEXT_WEIGHT_THIN = 100,
  GDK_TEXT_WEIGHT_ULTRALIGHT = 200,
  GDK_TEXT_WEIGHT_LIGHT = 300,
  GDK_TEXT_WEIGHT_SEMILIGHT = 350,
  GDK_TEXT_WEIGHT_BOOK = 380,
  GDK_TEXT_WEIGHT_NORMAL = 400,
  GDK_TEXT_WEIGHT_MEDIUM = 500,
  GDK_TEXT_WEIGHT_SEMIBOLD = 600,
  GDK_TEXT_WEIGHT_BOLD = 700,
  GDK_TEXT_WEIGHT_ULTRABOLD = 800,
  GDK_TEXT_WEIGHT_HEAVY = 900,
  GDK_TEXT_WEIGHT_ULTRAHEAVY = 1000
} GdkTextWeight;

/**
 * GdkTextStretch:
 * @GDK_TEXT_STRETCH_ULTRA_CONDENSED: ultra condensed width
 * @GDK_TEXT_STRETCH_EXTRA_CONDENSED: extra condensed width
 * @GDK_TEXT_STRETCH_CONDENSED: condensed width
 * @GDK_TEXT_STRETCH_SEMI_CONDENSED: semi condensed width
 * @GDK_TEXT_STRETCH_NORMAL: the normal width
 * @GDK_TEXT_STRETCH_SEMI_EXPANDED: semi expanded width
 * @GDK_TEXT_STRETCH_EXPANDED: expanded width
 * @GDK_TEXT_STRETCH_EXTRA_EXPANDED: extra expanded width
 * @GDK_TEXT_STRETCH_ULTRA_EXPANDED: ultra expanded width
 *
 * An enumeration specifying the width of the font relative to other designs
 * within a family.
 */
typedef enum {
  GDK_TEXT_STRETCH_ULTRA_CONDENSED,
  GDK_TEXT_STRETCH_EXTRA_CONDENSED,
  GDK_TEXT_STRETCH_CONDENSED,
  GDK_TEXT_STRETCH_SEMI_CONDENSED,
  GDK_TEXT_STRETCH_NORMAL,
  GDK_TEXT_STRETCH_SEMI_EXPANDED,
  GDK_TEXT_STRETCH_EXPANDED,
  GDK_TEXT_STRETCH_EXTRA_EXPANDED,
  GDK_TEXT_STRETCH_ULTRA_EXPANDED
} GdkTextStretch;

/**
 * GdkTextWidth:
 * @GDK_TEXT_WIDTH_ULTRA_CONDENSED: ultra condensed width
 * @GDK_TEXT_WIDTH_EXTRA_CONDENSED: extra condensed width
 * @GDK_TEXT_WIDTH_CONDENSED: condensed width
 * @GDK_TEXT_WIDTH_SEMI_CONDENSED: semi condensed width
 * @GDK_TEXT_WIDTH_NORMAL: the normal width
 * @GDK_TEXT_WIDTH_SEMI_EXPANDED: semi expanded width
 * @GDK_TEXT_WIDTH_EXPANDED: expanded width
 * @GDK_TEXT_WIDTH_EXTRA_EXPANDED: extra expanded width
 * @GDK_TEXT_WIDTH_ULTRA_EXPANDED: ultra expanded width
 *
 * An enumeration specifying the width of the font relative to other designs
 * within a family.
 *
 * The enumeration values match [enum@GdkTextStretch], but
 * the numeric values are expanded to allow intermediate
 * values.
 *
 * Since: 1.58
 */
typedef enum {
  GDK_TEXT_WIDTH_ULTRA_CONDENSED = 500,
  GDK_TEXT_WIDTH_EXTRA_CONDENSED = 625,
  GDK_TEXT_WIDTH_CONDENSED = 750,
  GDK_TEXT_WIDTH_SEMI_CONDENSED = 875,
  GDK_TEXT_WIDTH_NORMAL = 1000,
  GDK_TEXT_WIDTH_SEMI_EXPANDED = 1125,
  GDK_TEXT_WIDTH_EXPANDED = 1250,
  GDK_TEXT_WIDTH_EXTRA_EXPANDED = 1500,
  GDK_TEXT_WIDTH_ULTRA_EXPANDED = 2000,
} GdkTextWidth;

/**
 * GdkTextFontMask:
 * @GDK_TEXT_FONT_MASK_FAMILY: the font family is specified.
 * @GDK_TEXT_FONT_MASK_STYLE: the font style is specified.
 * @GDK_TEXT_FONT_MASK_VARIANT: the font variant is specified.
 * @GDK_TEXT_FONT_MASK_WEIGHT: the font weight is specified.
 * @GDK_TEXT_FONT_MASK_STRETCH: the font stretch/width is specified.
 * @GDK_TEXT_FONT_MASK_SIZE: the font size is specified.
 *
 * The bits in a `GdkTextFontMask` correspond to the set fields in a
 * `GdkTextFontDescription`.
 */
/**
 * GDK_TEXT_FONT_MASK_GRAVITY:
 *
 * The font gravity is specified.
 *
 * Since: 1.16
 */
/**
 * GDK_TEXT_FONT_MASK_VARIATIONS:
 *
 * OpenType font variations are specified.
 *
 * Since: 1.42
 */
/**
 * GDK_TEXT_FONT_MASK_FEATURES:
 *
 * OpenType font features are specified.
 *
 * Since: 1.56
 */
/**
 * GDK_TEXT_FONT_MASK_COLOR:
 *
 * Font color is specified.
 *
 * Since: 1.57
 */
/**
 * GDK_TEXT_FONT_MASK_WIDTH:
 *
 * Font width is specified.
 *
 * This is an alias for [flags@GdkText.FontMask.STRETCH].
 *
 * 1.58
 */
typedef enum {
  GDK_TEXT_FONT_MASK_FAMILY  = 1 << 0,
  GDK_TEXT_FONT_MASK_STYLE   = 1 << 1,
  GDK_TEXT_FONT_MASK_VARIANT = 1 << 2,
  GDK_TEXT_FONT_MASK_WEIGHT  = 1 << 3,
  GDK_TEXT_FONT_MASK_WIDTH   = 1 << 4,
  GDK_TEXT_FONT_MASK_STRETCH = 1 << 4,
  GDK_TEXT_FONT_MASK_SIZE    = 1 << 5,
  GDK_TEXT_FONT_MASK_GRAVITY = 1 << 6,
  GDK_TEXT_FONT_MASK_VARIATIONS = 1 << 7,
  GDK_TEXT_FONT_MASK_FEATURES = 1 << 8,
  GDK_TEXT_FONT_MASK_COLOR    = 1 << 9,
} GdkTextFontMask;

/* CSS scale factors (1.2 factor between each size) */
/**
 * GDK_TEXT_SCALE_XX_SMALL:
 *
 * The scale factor for three shrinking steps (1 / (1.2 * 1.2 * 1.2)).
 */
/**
 * GDK_TEXT_SCALE_X_SMALL:
 *
 * The scale factor for two shrinking steps (1 / (1.2 * 1.2)).
 */
/**
 * GDK_TEXT_SCALE_SMALL:
 *
 * The scale factor for one shrinking step (1 / 1.2).
 */
/**
 * GDK_TEXT_SCALE_MEDIUM:
 *
 * The scale factor for normal size (1.0).
 */
/**
 * GDK_TEXT_SCALE_LARGE:
 *
 * The scale factor for one magnification step (1.2).
 */
/**
 * GDK_TEXT_SCALE_X_LARGE:
 *
 * The scale factor for two magnification steps (1.2 * 1.2).
 */
/**
 * GDK_TEXT_SCALE_XX_LARGE:
 *
 * The scale factor for three magnification steps (1.2 * 1.2 * 1.2).
 */
#define GDK_TEXT_SCALE_XX_SMALL ((double)0.5787037037037)
#define GDK_TEXT_SCALE_X_SMALL  ((double)0.6944444444444)
#define GDK_TEXT_SCALE_SMALL    ((double)0.8333333333333)
#define GDK_TEXT_SCALE_MEDIUM   ((double)1.0)
#define GDK_TEXT_SCALE_LARGE    ((double)1.2)
#define GDK_TEXT_SCALE_X_LARGE  ((double)1.44)
#define GDK_TEXT_SCALE_XX_LARGE ((double)1.728)

/**
 * GdkTextFontColor:
 * @GDK_TEXT_FONT_COLOR_FORBIDDEN: The font should not have color glyphs
 * @GDK_TEXT_FONT_COLOR_REQUIRED: The font should have color glyphs
 * @GDK_TEXT_FONT_COLOR_DONT_CARE: The font may or may not use color
 *
 * Specifies whether a font should or should not have color glyphs.
 *
 * Since: 1.57
 */
typedef enum {
  GDK_TEXT_FONT_COLOR_FORBIDDEN,
  GDK_TEXT_FONT_COLOR_REQUIRED,
  GDK_TEXT_FONT_COLOR_DONT_CARE,
} GdkTextFontColor;

/*
 * GdkTextFontDescription
 */

#define GDK_TEXT_TYPE_FONT_DESCRIPTION (gdk_text_font_description_get_type ())

GDK_TEXT_AVAILABLE_IN_ALL
GType                 gdk_text_font_description_get_type    (void) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontDescription *gdk_text_font_description_new         (void);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontDescription *gdk_text_font_description_copy        (const GdkTextFontDescription  *desc);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontDescription *gdk_text_font_description_copy_static (const GdkTextFontDescription  *desc);
GDK_TEXT_AVAILABLE_IN_ALL
guint                 gdk_text_font_description_hash        (const GdkTextFontDescription  *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
gboolean              gdk_text_font_description_equal       (const GdkTextFontDescription  *desc1,
                                                          const GdkTextFontDescription  *desc2) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
void                  gdk_text_font_description_free        (GdkTextFontDescription        *desc);
GDK_TEXT_DEPRECATED_IN_1_56
void                  gdk_text_font_descriptions_free       (GdkTextFontDescription       **descs,
                                                          int                          n_descs);

GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_description_set_family        (GdkTextFontDescription *desc,
                                                               const char           *family);
GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_description_set_family_static (GdkTextFontDescription *desc,
                                                               const char           *family);
GDK_TEXT_AVAILABLE_IN_ALL
const char          *gdk_text_font_description_get_family        (const GdkTextFontDescription *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_description_set_style         (GdkTextFontDescription *desc,
                                                               GdkTextStyle            style);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextStyle           gdk_text_font_description_get_style         (const GdkTextFontDescription *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_description_set_variant       (GdkTextFontDescription *desc,
                                                               GdkTextVariant          variant);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextVariant         gdk_text_font_description_get_variant       (const GdkTextFontDescription *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_description_set_weight        (GdkTextFontDescription *desc,
                                                               GdkTextWeight           weight);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextWeight          gdk_text_font_description_get_weight        (const GdkTextFontDescription *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_description_set_stretch       (GdkTextFontDescription *desc,
                                                               GdkTextStretch          stretch);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextStretch         gdk_text_font_description_get_stretch       (const GdkTextFontDescription *desc) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_1_58
void                 gdk_text_font_description_set_width         (GdkTextFontDescription *desc,
                                                               GdkTextWidth            width);
GDK_TEXT_AVAILABLE_IN_1_58
GdkTextWidth           gdk_text_font_description_get_width         (const GdkTextFontDescription *desc);

GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_description_set_size          (GdkTextFontDescription *desc,
                                                               gint                  size);
GDK_TEXT_AVAILABLE_IN_ALL
gint                 gdk_text_font_description_get_size          (const GdkTextFontDescription *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_8
void                 gdk_text_font_description_set_absolute_size (GdkTextFontDescription *desc,
                                                               double                size);
GDK_TEXT_AVAILABLE_IN_1_8
gboolean             gdk_text_font_description_get_size_is_absolute (const GdkTextFontDescription *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_16
void                 gdk_text_font_description_set_gravity       (GdkTextFontDescription *desc,
                                                               GdkTextGravity          gravity);
GDK_TEXT_AVAILABLE_IN_1_16
GdkTextGravity         gdk_text_font_description_get_gravity       (const GdkTextFontDescription *desc) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_1_42
void                 gdk_text_font_description_set_variations_static (GdkTextFontDescription       *desc,
                                                                   const char                 *variations);
GDK_TEXT_AVAILABLE_IN_1_42
void                 gdk_text_font_description_set_variations    (GdkTextFontDescription       *desc,
                                                               const char                 *variations);
GDK_TEXT_AVAILABLE_IN_1_42
const char          *gdk_text_font_description_get_variations    (const GdkTextFontDescription *desc) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_1_56
void                 gdk_text_font_description_set_features_static (GdkTextFontDescription       *desc,
                                                                 const char                 *features);
GDK_TEXT_AVAILABLE_IN_1_56
void                 gdk_text_font_description_set_features        (GdkTextFontDescription       *desc,
                                                                 const char                 *features);
GDK_TEXT_AVAILABLE_IN_1_42
const char          *gdk_text_font_description_get_features        (const GdkTextFontDescription *desc) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_1_57
void                 gdk_text_font_description_set_color           (GdkTextFontDescription       *desc,
                                                                 GdkTextFontColor              color);
GDK_TEXT_AVAILABLE_IN_1_57
GdkTextFontColor       gdk_text_font_description_get_color           (const GdkTextFontDescription *desc);

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontMask gdk_text_font_description_get_set_fields (const GdkTextFontDescription *desc) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
void          gdk_text_font_description_unset_fields   (GdkTextFontDescription       *desc,
                                                     GdkTextFontMask               to_unset);

GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_font_description_merge        (GdkTextFontDescription       *desc,
                                          const GdkTextFontDescription *desc_to_merge,
                                          gboolean                    replace_existing);
GDK_TEXT_AVAILABLE_IN_ALL
void gdk_text_font_description_merge_static (GdkTextFontDescription       *desc,
                                          const GdkTextFontDescription *desc_to_merge,
                                          gboolean                    replace_existing);

GDK_TEXT_AVAILABLE_IN_ALL
gboolean gdk_text_font_description_better_match (const GdkTextFontDescription *desc,
                                              const GdkTextFontDescription *old_match,
                                              const GdkTextFontDescription *new_match) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontDescription *gdk_text_font_description_from_string (const char                  *str);
GDK_TEXT_AVAILABLE_IN_ALL
char *                gdk_text_font_description_to_string   (const GdkTextFontDescription  *desc);
GDK_TEXT_AVAILABLE_IN_ALL
char *                gdk_text_font_description_to_filename (const GdkTextFontDescription  *desc);

/*
 * GdkTextFontMetrics
 */

#define GDK_TEXT_TYPE_FONT_METRICS  (gdk_text_font_metrics_get_type ())

struct _PangoFontMetrics
{
  /* <private> */
  guint ref_count;

  int ascent;
  int descent;
  int height;
  int approximate_char_width;
  int approximate_digit_width;
  int underline_position;
  int underline_thickness;
  int strikethrough_position;
  int strikethrough_thickness;
};

GDK_TEXT_AVAILABLE_IN_ALL
GType             gdk_text_font_metrics_get_type                    (void) G_GNUC_CONST;
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontMetrics *gdk_text_font_metrics_ref                         (GdkTextFontMetrics *metrics);
GDK_TEXT_AVAILABLE_IN_ALL
void              gdk_text_font_metrics_unref                       (GdkTextFontMetrics *metrics);
GDK_TEXT_AVAILABLE_IN_ALL
int               gdk_text_font_metrics_get_ascent                  (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
int               gdk_text_font_metrics_get_descent                 (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_44
int               gdk_text_font_metrics_get_height                  (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
int               gdk_text_font_metrics_get_approximate_char_width  (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_ALL
int               gdk_text_font_metrics_get_approximate_digit_width (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_6
int               gdk_text_font_metrics_get_underline_position      (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_6
int               gdk_text_font_metrics_get_underline_thickness     (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_6
int               gdk_text_font_metrics_get_strikethrough_position  (GdkTextFontMetrics *metrics) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_6
int               gdk_text_font_metrics_get_strikethrough_thickness (GdkTextFontMetrics *metrics) G_GNUC_PURE;


/*
 * GdkTextFontFamily
 */

#define GDK_TEXT_TYPE_FONT_FAMILY              (gdk_text_font_family_get_type ())
#define GDK_TEXT_FONT_FAMILY(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FONT_FAMILY, GdkTextFontFamily))
#define GDK_TEXT_IS_FONT_FAMILY(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FONT_FAMILY))
#define GDK_TEXT_FONT_FAMILY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_FONT_FAMILY, GdkTextFontFamilyClass))
#define GDK_TEXT_IS_FONT_FAMILY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_FONT_FAMILY))
#define GDK_TEXT_FONT_FAMILY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_FONT_FAMILY, GdkTextFontFamilyClass))

typedef struct _PangoFontFace        GdkTextFontFace;
typedef struct _PangoFontFamily      GdkTextFontFamily;
typedef struct _PangoFontFamilyClass GdkTextFontFamilyClass;

#ifndef GDK_TEXT_DISABLE_DEPRECATED

/**
 * GdkTextFontFamily:
 *
 * A `GdkTextFontFamily` is used to represent a family of related
 * font faces.
 *
 * The font faces in a family share a common design, but differ in
 * slant, weight, width or other aspects.
 */
struct _PangoFontFamily
{
  GObject parent_instance;
};

struct _PangoFontFamilyClass
{
  GObjectClass parent_class;

  /*< public >*/

  void  (*list_faces)      (GdkTextFontFamily  *family,
                            GdkTextFontFace  ***faces,
                            int              *n_faces);
  const char * (*get_name) (GdkTextFontFamily  *family);
  gboolean (*is_monospace) (GdkTextFontFamily *family);
  gboolean (*is_variable)  (GdkTextFontFamily *family);

  GdkTextFontFace * (*get_face) (GdkTextFontFamily *family,
                               const char      *name);


  /*< private >*/

  /* Padding for future expansion */
  void (*_pango_reserved2) (void);
};

#endif /* GDK_TEXT_DISABLE_DEPRECATED */

GDK_TEXT_AVAILABLE_IN_ALL
GType      gdk_text_font_family_get_type       (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
void                 gdk_text_font_family_list_faces (GdkTextFontFamily  *family,
                                                   GdkTextFontFace  ***faces,
                                                   int              *n_faces);
GDK_TEXT_AVAILABLE_IN_ALL
const char *gdk_text_font_family_get_name   (GdkTextFontFamily  *family) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_4
gboolean   gdk_text_font_family_is_monospace         (GdkTextFontFamily  *family) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_44
gboolean   gdk_text_font_family_is_variable          (GdkTextFontFamily  *family) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_1_46
GdkTextFontFace *gdk_text_font_family_get_face (GdkTextFontFamily *family,
                                           const char      *name);


/*
 * GdkTextFontFace
 */

#define GDK_TEXT_TYPE_FONT_FACE              (gdk_text_font_face_get_type ())
#define GDK_TEXT_FONT_FACE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FONT_FACE, GdkTextFontFace))
#define GDK_TEXT_IS_FONT_FACE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FONT_FACE))
#define GDK_TEXT_FONT_FACE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_FONT_FACE, GdkTextFontFaceClass))
#define GDK_TEXT_IS_FONT_FACE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_FONT_FACE))
#define GDK_TEXT_FONT_FACE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_FONT_FACE, GdkTextFontFaceClass))

typedef struct _PangoFontFaceClass   GdkTextFontFaceClass;

#ifndef GDK_TEXT_DISABLE_DEPRECATED

/**
 * GdkTextFontFace:
 *
 * A `GdkTextFontFace` is used to represent a group of fonts with
 * the same family, slant, weight, and width, but varying sizes.
 */
struct _PangoFontFace
{
  GObject parent_instance;
};

struct _PangoFontFaceClass
{
  GObjectClass parent_class;

  /*< public >*/

  const char           * (*get_face_name)  (GdkTextFontFace *face);
  GdkTextFontDescription * (*describe)       (GdkTextFontFace *face);
  void                   (*list_sizes)     (GdkTextFontFace  *face,
                                            int           **sizes,
                                            int            *n_sizes);
  gboolean               (*is_synthesized) (GdkTextFontFace *face);
  GdkTextFontFamily *      (*get_family)     (GdkTextFontFace *face);

  /*< private >*/

  /* Padding for future expansion */
  void (*_pango_reserved3) (void);
  void (*_pango_reserved4) (void);
};

#endif /* GDK_TEXT_DISABLE_DEPRECATED */

GDK_TEXT_AVAILABLE_IN_ALL
GType      gdk_text_font_face_get_type       (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontDescription *gdk_text_font_face_describe       (GdkTextFontFace  *face);
GDK_TEXT_AVAILABLE_IN_ALL
const char           *gdk_text_font_face_get_face_name  (GdkTextFontFace  *face) G_GNUC_PURE;
GDK_TEXT_AVAILABLE_IN_1_4
void                  gdk_text_font_face_list_sizes     (GdkTextFontFace  *face,
                                                      int           **sizes,
                                                      int            *n_sizes);
GDK_TEXT_AVAILABLE_IN_1_18
gboolean              gdk_text_font_face_is_synthesized (GdkTextFontFace  *face) G_GNUC_PURE;

GDK_TEXT_AVAILABLE_IN_1_46
GdkTextFontFamily *     gdk_text_font_face_get_family     (GdkTextFontFace  *face);


/*
 * GdkTextFont
 */

#define GDK_TEXT_TYPE_FONT              (gdk_text_font_get_type ())
#define GDK_TEXT_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FONT, GdkTextFont))
#define GDK_TEXT_IS_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FONT))
#define GDK_TEXT_FONT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_FONT, GdkTextFontClass))
#define GDK_TEXT_IS_FONT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_FONT))
#define GDK_TEXT_FONT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_FONT, GdkTextFontClass))


#ifndef GDK_TEXT_DISABLE_DEPRECATED

/**
 * GdkTextFont:
 *
 * A `GdkTextFont` is used to represent a font in a
 * rendering-system-independent manner.
 */
struct _PangoFont
{
  GObject parent_instance;
};

typedef struct _PangoFontClass       GdkTextFontClass;
struct _PangoFontClass
{
  GObjectClass parent_class;

  /*< public >*/

  GdkTextFontDescription *(*describe)           (GdkTextFont      *font);
  GdkTextCoverage *       (*get_coverage)       (GdkTextFont      *font,
                                               GdkTextLanguage  *language);
  void                  (*get_glyph_extents)  (GdkTextFont      *font,
                                               GdkTextGlyph      glyph,
                                               GdkTextRectangle *ink_rect,
                                               GdkTextRectangle *logical_rect);
  GdkTextFontMetrics *    (*get_metrics)        (GdkTextFont      *font,
                                               GdkTextLanguage  *language);
  GdkTextFontMap *        (*get_font_map)       (GdkTextFont      *font);
  GdkTextFontDescription *(*describe_absolute)  (GdkTextFont      *font);
  void                  (*get_features)       (GdkTextFont      *font,
                                               hb_feature_t   *features,
                                               guint           len,
                                               guint          *num_features);
  hb_font_t *           (*create_hb_font)     (GdkTextFont      *font);
};

#endif /* GDK_TEXT_DISABLE_DEPRECATED */

GDK_TEXT_AVAILABLE_IN_ALL
GType                 gdk_text_font_get_type          (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontDescription *gdk_text_font_describe          (GdkTextFont        *font);
GDK_TEXT_AVAILABLE_IN_1_14
GdkTextFontDescription *gdk_text_font_describe_with_absolute_size (GdkTextFont        *font);
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextCoverage *       gdk_text_font_get_coverage      (GdkTextFont        *font,
                                                    GdkTextLanguage    *language);
#ifndef __GI_SCANNER__
GDK_TEXT_DEPRECATED_IN_1_44
GdkTextEngineShape *    gdk_text_font_find_shaper       (GdkTextFont        *font,
                                                    GdkTextLanguage    *language,
                                                    guint32           ch);
#endif
GDK_TEXT_AVAILABLE_IN_ALL
GdkTextFontMetrics *    gdk_text_font_get_metrics       (GdkTextFont        *font,
                                                    GdkTextLanguage    *language);
GDK_TEXT_AVAILABLE_IN_ALL
void                  gdk_text_font_get_glyph_extents (GdkTextFont        *font,
                                                    GdkTextGlyph        glyph,
                                                    GdkTextRectangle   *ink_rect,
                                                    GdkTextRectangle   *logical_rect);
GDK_TEXT_AVAILABLE_IN_1_10
GdkTextFontMap         *gdk_text_font_get_font_map      (GdkTextFont        *font);

GDK_TEXT_AVAILABLE_IN_1_46
GdkTextFontFace *       gdk_text_font_get_face          (GdkTextFont        *font);

GDK_TEXT_AVAILABLE_IN_1_44
gboolean              gdk_text_font_has_char          (GdkTextFont        *font,
                                                    gunichar          wc);
GDK_TEXT_AVAILABLE_IN_1_44
void                  gdk_text_font_get_features      (GdkTextFont        *font,
                                                    hb_feature_t     *features,
                                                    guint             len,
                                                    guint            *num_features);
GDK_TEXT_AVAILABLE_IN_1_44
hb_font_t *           gdk_text_font_get_hb_font       (GdkTextFont        *font);

GDK_TEXT_AVAILABLE_IN_1_50
GdkTextLanguage **      gdk_text_font_get_languages     (GdkTextFont        *font);

GDK_TEXT_AVAILABLE_IN_1_50
GBytes *              gdk_text_font_serialize         (GdkTextFont        *font);

GDK_TEXT_AVAILABLE_IN_1_50
GdkTextFont *           gdk_text_font_deserialize       (GdkTextContext     *context,
                                                    GBytes           *bytes,
                                                    GError          **error);

/**
 * GDK_TEXT_GLYPH_EMPTY:
 *
 * A `GdkTextGlyph` value that indicates a zero-width empty glpyh.
 *
 * This is useful for example in shaper modules, to use as the glyph for
 * various zero-width Unicode characters (those passing [func@is_zero_width]).
 */

/**
 * GDK_TEXT_GLYPH_INVALID_INPUT:
 *
 * A `GdkTextGlyph` value for invalid input.
 *
 * `GdkTextLayout` produces one such glyph per invalid input UTF-8 byte and such
 * a glyph is rendered as a crossed box.
 *
 * Note that this value is defined such that it has the %GDK_TEXT_GLYPH_UNKNOWN_FLAG
 * set.
 *
 * Since: 1.20
 */
/**
 * GDK_TEXT_GLYPH_UNKNOWN_FLAG:
 *
 * Flag used in `GdkTextGlyph` to turn a `gunichar` value of a valid Unicode
 * character into an unknown-character glyph for that `gunichar`.
 *
 * Such unknown-character glyphs may be rendered as a 'hex box'.
 */
/**
 * GDK_TEXT_GET_UNKNOWN_GLYPH:
 * @wc: a Unicode character
 *
 * The way this unknown glyphs are rendered is backend specific. For example,
 * a box with the hexadecimal Unicode code-point of the character written in it
 * is what is done in the most common backends.
 *
 * Returns: a `GdkTextGlyph` value that means no glyph was found for @wc.
 */
#define GDK_TEXT_GLYPH_EMPTY           ((GdkTextGlyph)0x0FFFFFFF)
#define GDK_TEXT_GLYPH_INVALID_INPUT   ((GdkTextGlyph)0xFFFFFFFF)
#define GDK_TEXT_GLYPH_UNKNOWN_FLAG    ((GdkTextGlyph)0x10000000)
#define GDK_TEXT_GET_UNKNOWN_GLYPH(wc) ((GdkTextGlyph)(wc)|GDK_TEXT_GLYPH_UNKNOWN_FLAG)

#ifndef __GI_SCANNER__
#ifndef GDK_TEXT_DISABLE_DEPRECATED
#define GDK_TEXT_UNKNOWN_GLYPH_WIDTH  10
#define GDK_TEXT_UNKNOWN_GLYPH_HEIGHT 14
#endif
#endif

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextFontFamily, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextFontFace, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextFont, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextFontDescription, gdk_text_font_description_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextFontMetrics, gdk_text_font_metrics_unref)

G_END_DECLS

#endif /* __PANGO_FONT_H__ */
