/* GdkText
 * pango-ot.h:
 *
 * Copyright (C) 2000,2007 Red Hat Software
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

#ifndef __PANGO_OT_H__
#define __PANGO_OT_H__

/* Deprecated.  Use HarfBuzz directly! */

#include <pango/pangofc-font.h>
#include <pango/pango-glyph.h>
#include <pango/pango-font.h>
#include <pango/pango-script.h>
#include <pango/pango-language.h>

#include <ft2build.h>
#include FT_FREETYPE_H

G_BEGIN_DECLS

#ifndef GDK_TEXT_DISABLE_DEPRECATED

/**
 * GdkTextOTTag:
 *
 * The `GdkTextOTTag` typedef is used to represent TrueType and OpenType
 * four letter tags inside GdkText. Use GDK_TEXT_OT_TAG_MAKE()
 * or GDK_TEXT_OT_TAG_MAKE_FROM_STRING() macros to create GdkTextOTTags manually.
 */
typedef guint32 GdkTextOTTag;

/**
 * GDK_TEXT_OT_TAG_MAKE:
 * @c1: First character.
 * @c2: Second character.
 * @c3: Third character.
 * @c4: Fourth character.
 *
 * Creates a `GdkTextOTTag` from four characters.  This is similar and
 * compatible with the FT_MAKE_TAG() macro from FreeType.
 */
/**
 * GDK_TEXT_OT_TAG_MAKE_FROM_STRING:
 * @s: The string representation of the tag.
 *
 * Creates a `GdkTextOTTag` from a string. The string should be at least
 * four characters long (pad with space characters if needed), and need
 * not be nul-terminated.  This is a convenience wrapper around
 * GDK_TEXT_OT_TAG_MAKE(), but cannot be used in certain situations, for
 * example, as a switch expression, as it dereferences pointers.
 */
#define GDK_TEXT_OT_TAG_MAKE(c1,c2,c3,c4)		((GdkTextOTTag) FT_MAKE_TAG (c1, c2, c3, c4))
#define GDK_TEXT_OT_TAG_MAKE_FROM_STRING(s)	(GDK_TEXT_OT_TAG_MAKE(((const char *) s)[0], \
								   ((const char *) s)[1], \
								   ((const char *) s)[2], \
								   ((const char *) s)[3]))

typedef struct _PangoOTInfo       GdkTextOTInfo;
typedef struct _PangoOTBuffer     GdkTextOTBuffer;
typedef struct _PangoOTGlyph      GdkTextOTGlyph;
typedef struct _PangoOTRuleset    GdkTextOTRuleset;
typedef struct _PangoOTFeatureMap GdkTextOTFeatureMap;
typedef struct _PangoOTRulesetDescription GdkTextOTRulesetDescription;

/**
 * GdkTextOTTableType:
 * @GDK_TEXT_OT_TABLE_GSUB: The GSUB table.
 * @GDK_TEXT_OT_TABLE_GPOS: The GPOS table.
 *
 * The GdkTextOTTableType enumeration values are used to
 * identify the various OpenType tables in the
 * gdk_text_ot_info_… functions.
 */
typedef enum
{
  GDK_TEXT_OT_TABLE_GSUB,
  GDK_TEXT_OT_TABLE_GPOS
} GdkTextOTTableType;

/**
 * GDK_TEXT_OT_ALL_GLYPHS:
 *
 * This is used as the property bit in gdk_text_ot_ruleset_add_feature() when a
 * feature should be applied to all glyphs.
 *
 * Since: 1.16
 */
/**
 * GDK_TEXT_OT_NO_FEATURE:
 *
 * This is used as a feature index that represent no feature, that is, should be
 * skipped.  It may be returned as feature index by gdk_text_ot_info_find_feature()
 * if the feature is not found, and gdk_text_ot_ruleset_add_feature() function
 * automatically skips this value, so no special handling is required by the user.
 *
 * Since: 1.18
 */
/**
 * GDK_TEXT_OT_NO_SCRIPT:
 *
 * This is used as a script index that represent no script, that is, when the
 * requested script was not found, and a default ('DFLT') script was not found
 * either.  It may be returned as script index by gdk_text_ot_info_find_script()
 * if the script or a default script are not found, all other functions
 * taking a script index essentially return if the input script index is
 * this value, so no special handling is required by the user.
 *
 * Since: 1.18
 */
/**
 * GDK_TEXT_OT_DEFAULT_LANGUAGE:
 *
 * This is used as the language index in gdk_text_ot_info_find_feature() when
 * the default language system of the script is desired.
 *
 * It is also returned by gdk_text_ot_info_find_language() if the requested language
 * is not found, or the requested language tag was GDK_TEXT_OT_TAG_DEFAULT_LANGUAGE.
 * The end result is that one can always call gdk_text_ot_tag_from_language()
 * followed by gdk_text_ot_info_find_language() and pass the result to
 * gdk_text_ot_info_find_feature() without having to worry about falling back to
 * default language system explicitly.
 *
 * Since: 1.16
 */
#define GDK_TEXT_OT_ALL_GLYPHS			((guint) 0xFFFF)
#define GDK_TEXT_OT_NO_FEATURE			((guint) 0xFFFF)
#define GDK_TEXT_OT_NO_SCRIPT			((guint) 0xFFFF)
#define GDK_TEXT_OT_DEFAULT_LANGUAGE		((guint) 0xFFFF)

/**
 * GDK_TEXT_OT_TAG_DEFAULT_SCRIPT:
 *
 * This is a `GdkTextOTTag` representing the special script tag 'DFLT'.  It is
 * returned as script tag by gdk_text_ot_tag_from_script() if the requested script
 * is not found.
 *
 * Since: 1.18
 */
/**
 * GDK_TEXT_OT_TAG_DEFAULT_LANGUAGE:
 *
 * This is a `GdkTextOTTag` representing a special language tag 'dflt'.  It is
 * returned as language tag by gdk_text_ot_tag_from_language() if the requested
 * language is not found.  It is safe to pass this value to
 * gdk_text_ot_info_find_language() as that function falls back to returning default
 * language-system if the requested language tag is not found.
 *
 * Since: 1.18
 */
#define GDK_TEXT_OT_TAG_DEFAULT_SCRIPT		GDK_TEXT_OT_TAG_MAKE ('D', 'F', 'L', 'T')
#define GDK_TEXT_OT_TAG_DEFAULT_LANGUAGE		GDK_TEXT_OT_TAG_MAKE ('d', 'f', 'l', 't')

/* Note that this must match hb_glyph_info_t */
/**
 * GdkTextOTGlyph:
 * @glyph: the glyph itself.
 * @properties: the properties value, identifying which features should be
 * applied on this glyph.  See gdk_text_ot_ruleset_add_feature().
 * @cluster: the cluster that this glyph belongs to.
 * @component: a component value, set by the OpenType layout engine.
 * @ligID: a ligature index value, set by the OpenType layout engine.
 * @internal: for GdkText internal use
 *
 * The `GdkTextOTGlyph` structure represents a single glyph together with
 * information used for OpenType layout processing of the glyph.
 * It contains the following fields.
 */
struct _PangoOTGlyph
{
  guint32  glyph;
  guint    properties;
  guint    cluster;
  gushort  component;
  gushort  ligID;

  guint    internal;
};

/**
 * GdkTextOTFeatureMap:
 * @feature_name: feature tag in represented as four-letter ASCII string.
 * @property_bit: the property bit to use for this feature.  See
 * gdk_text_ot_ruleset_add_feature() for details.
 *
 * The `GdkTextOTFeatureMap` typedef is used to represent an OpenType
 * feature with the property bit associated with it.  The feature tag is
 * represented as a char array instead of a `GdkTextOTTag` for convenience.
 *
 * Since: 1.18
 */
struct _PangoOTFeatureMap
{
  char     feature_name[5];
  gulong   property_bit;
};

/**
 * GdkTextOTRulesetDescription:
 * @script: a `GdkTextScript`
 * @language: a `GdkTextLanguage`
 * @static_gsub_features: (nullable): static map of GSUB features
 * @n_static_gsub_features: length of @static_gsub_features, or 0.
 * @static_gpos_features: (nullable): static map of GPOS features
 * @n_static_gpos_features: length of @static_gpos_features, or 0.
 * @other_features: (nullable): map of extra features to add to both
 *   GSUB and GPOS. Unlike the static maps, this pointer need not
 *   live beyond the life of function calls taking this struct.
 * @n_other_features: length of @other_features, or 0.
 *
 * The `GdkTextOTRuleset` structure holds all the information needed
 * to build a complete `GdkTextOTRuleset` from an OpenType font.
 * The main use of this struct is to act as the key for a per-font
 * hash of rulesets.  The user populates a ruleset description and
 * gets the ruleset using gdk_text_ot_ruleset_get_for_description()
 * or create a new one using gdk_text_ot_ruleset_new_from_description().
 *
 * Since: 1.18
 */
struct _PangoOTRulesetDescription {
  GdkTextScript               script;
  GdkTextLanguage            *language;
  const GdkTextOTFeatureMap  *static_gsub_features;
  guint                   n_static_gsub_features;
  const GdkTextOTFeatureMap  *static_gpos_features;
  guint                   n_static_gpos_features;
  const GdkTextOTFeatureMap  *other_features;
  guint                   n_other_features;
};

#ifdef __GI_SCANNER__
#define GDK_TEXT_OT_TYPE_INFO              (gdk_text_ot_info_get_type ())
#define GDK_TEXT_OT_INFO(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_OT_TYPE_INFO, GdkTextOTInfo))
#define GDK_TEXT_OT_IS_INFO(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_OT_TYPE_INFO))
#else
#define GDK_TEXT_TYPE_OT_INFO              (gdk_text_ot_info_get_type ())
#define GDK_TEXT_OT_INFO(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_OT_INFO, GdkTextOTInfo))
#define GDK_TEXT_IS_OT_INFO(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_OT_INFO))
#endif

GDK_TEXT_DEPRECATED
GType gdk_text_ot_info_get_type (void) G_GNUC_CONST;

#ifdef __GI_SCANNER__
#define GDK_TEXT_OT_TYPE_RULESET           (gdk_text_ot_ruleset_get_type ())
#define GDK_TEXT_OT_RULESET(object)        (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_OT_TYPE_RULESET, GdkTextOTRuleset))
#define GDK_TEXT_OT_IS_RULESET(object)     (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_OT_TYPE_RULESET))
#else
#define GDK_TEXT_TYPE_OT_RULESET           (gdk_text_ot_ruleset_get_type ())
#define GDK_TEXT_OT_RULESET(object)        (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_OT_RULESET, GdkTextOTRuleset))
#define GDK_TEXT_IS_OT_RULESET(object)     (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_OT_RULESET))
#endif

GDK_TEXT_DEPRECATED
GType gdk_text_ot_ruleset_get_type (void) G_GNUC_CONST;


GDK_TEXT_DEPRECATED
GdkTextOTInfo *gdk_text_ot_info_get (FT_Face face);

GDK_TEXT_DEPRECATED
gboolean gdk_text_ot_info_find_script   (GdkTextOTInfo      *info,
				      GdkTextOTTableType  table_type,
				      GdkTextOTTag        script_tag,
				      guint            *script_index);

GDK_TEXT_DEPRECATED
gboolean gdk_text_ot_info_find_language (GdkTextOTInfo      *info,
				      GdkTextOTTableType  table_type,
				      guint             script_index,
				      GdkTextOTTag        language_tag,
				      guint            *language_index,
				      guint            *required_feature_index);
GDK_TEXT_DEPRECATED
gboolean gdk_text_ot_info_find_feature  (GdkTextOTInfo      *info,
				      GdkTextOTTableType  table_type,
				      GdkTextOTTag        feature_tag,
				      guint             script_index,
				      guint             language_index,
				      guint            *feature_index);

GDK_TEXT_DEPRECATED
GdkTextOTTag *gdk_text_ot_info_list_scripts   (GdkTextOTInfo      *info,
					  GdkTextOTTableType  table_type);
GDK_TEXT_DEPRECATED
GdkTextOTTag *gdk_text_ot_info_list_languages (GdkTextOTInfo      *info,
					  GdkTextOTTableType  table_type,
					  guint             script_index,
					  GdkTextOTTag        language_tag);
GDK_TEXT_DEPRECATED
GdkTextOTTag *gdk_text_ot_info_list_features  (GdkTextOTInfo      *info,
					  GdkTextOTTableType  table_type,
					  GdkTextOTTag        tag,
					  guint             script_index,
					  guint             language_index);

#ifdef __GI_SCANNER__
#define GDK_TEXT_OT_TYPE_BUFFER (gdk_text_ot_buffer_get_type())
#else
#define GDK_TEXT_TYPE_OT_BUFFER (gdk_text_ot_buffer_get_type())
#endif

GDK_TEXT_DEPRECATED
GType          gdk_text_ot_buffer_get_type   (void) G_GNUC_CONST;

GDK_TEXT_DEPRECATED
GdkTextOTBuffer *gdk_text_ot_buffer_new        (GdkTextFcFont       *font);
GDK_TEXT_DEPRECATED
void           gdk_text_ot_buffer_destroy    (GdkTextOTBuffer     *buffer);
GDK_TEXT_DEPRECATED
void           gdk_text_ot_buffer_clear      (GdkTextOTBuffer     *buffer);
GDK_TEXT_DEPRECATED
void           gdk_text_ot_buffer_set_rtl    (GdkTextOTBuffer     *buffer,
					   gboolean           rtl);
GDK_TEXT_DEPRECATED
void           gdk_text_ot_buffer_add_glyph  (GdkTextOTBuffer     *buffer,
					   guint              glyph,
					   guint              properties,
					   guint              cluster);
GDK_TEXT_DEPRECATED
void           gdk_text_ot_buffer_get_glyphs (const GdkTextOTBuffer  *buffer,
					   GdkTextOTGlyph        **glyphs,
					   int                  *n_glyphs);
GDK_TEXT_DEPRECATED
void           gdk_text_ot_buffer_output     (const GdkTextOTBuffer  *buffer,
					   GdkTextGlyphString     *glyphs);

GDK_TEXT_DEPRECATED
void           gdk_text_ot_buffer_set_zero_width_marks (GdkTextOTBuffer     *buffer,
						     gboolean           zero_width_marks);

GDK_TEXT_DEPRECATED
const GdkTextOTRuleset *gdk_text_ot_ruleset_get_for_description (GdkTextOTInfo                     *info,
							    const GdkTextOTRulesetDescription *desc);
GDK_TEXT_DEPRECATED
GdkTextOTRuleset *gdk_text_ot_ruleset_new (GdkTextOTInfo       *info);
GDK_TEXT_DEPRECATED
GdkTextOTRuleset *gdk_text_ot_ruleset_new_for (GdkTextOTInfo       *info,
					  GdkTextScript        script,
					  GdkTextLanguage     *language);
GDK_TEXT_DEPRECATED
GdkTextOTRuleset *gdk_text_ot_ruleset_new_from_description (GdkTextOTInfo                     *info,
						       const GdkTextOTRulesetDescription *desc);
GDK_TEXT_DEPRECATED
void            gdk_text_ot_ruleset_add_feature (GdkTextOTRuleset   *ruleset,
					      GdkTextOTTableType  table_type,
					      guint             feature_index,
					      gulong            property_bit);
GDK_TEXT_DEPRECATED
gboolean        gdk_text_ot_ruleset_maybe_add_feature (GdkTextOTRuleset   *ruleset,
						    GdkTextOTTableType  table_type,
						    GdkTextOTTag        feature_tag,
						    gulong            property_bit);
GDK_TEXT_DEPRECATED
guint           gdk_text_ot_ruleset_maybe_add_features (GdkTextOTRuleset          *ruleset,
						     GdkTextOTTableType         table_type,
						     const GdkTextOTFeatureMap *features,
						     guint                    n_features);
GDK_TEXT_DEPRECATED
guint           gdk_text_ot_ruleset_get_feature_count (const GdkTextOTRuleset   *ruleset,
						    guint                  *n_gsub_features,
						    guint                  *n_gpos_features);

GDK_TEXT_DEPRECATED
void            gdk_text_ot_ruleset_substitute  (const GdkTextOTRuleset   *ruleset,
					      GdkTextOTBuffer          *buffer);

GDK_TEXT_DEPRECATED
void            gdk_text_ot_ruleset_position    (const GdkTextOTRuleset   *ruleset,
					      GdkTextOTBuffer          *buffer);

GDK_TEXT_DEPRECATED
GdkTextScript     gdk_text_ot_tag_to_script     (GdkTextOTTag     script_tag) G_GNUC_CONST;

GDK_TEXT_DEPRECATED
GdkTextOTTag      gdk_text_ot_tag_from_script   (GdkTextScript    script) G_GNUC_CONST;

GDK_TEXT_DEPRECATED
GdkTextLanguage  *gdk_text_ot_tag_to_language   (GdkTextOTTag     language_tag) G_GNUC_CONST;

GDK_TEXT_DEPRECATED
GdkTextOTTag      gdk_text_ot_tag_from_language (GdkTextLanguage *language) G_GNUC_CONST;

#ifdef __GI_SCANNER__
#define GDK_TEXT_OT_TYPE_RULESET_DESCRIPTION (gdk_text_ot_ruleset_description_get_type())
#else
#define GDK_TEXT_TYPE_OT_RULESET_DESCRIPTION (gdk_text_ot_ruleset_description_get_type())
#endif

GDK_TEXT_DEPRECATED
GType           gdk_text_ot_ruleset_description_get_type (void) G_GNUC_CONST;

GDK_TEXT_DEPRECATED
guint           gdk_text_ot_ruleset_description_hash  (const GdkTextOTRulesetDescription *desc) G_GNUC_PURE;

GDK_TEXT_DEPRECATED
gboolean        gdk_text_ot_ruleset_description_equal (const GdkTextOTRulesetDescription *desc1,
						    const GdkTextOTRulesetDescription *desc2) G_GNUC_PURE;

GDK_TEXT_DEPRECATED
GdkTextOTRulesetDescription *gdk_text_ot_ruleset_description_copy  (const GdkTextOTRulesetDescription *desc);

GDK_TEXT_DEPRECATED
void            gdk_text_ot_ruleset_description_free  (GdkTextOTRulesetDescription       *desc);


#endif /* GDK_TEXT_DISABLE_DEPRECATED */

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextOTRulesetDescription, gdk_text_ot_ruleset_description_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextOTBuffer, gdk_text_ot_buffer_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextOTRuleset, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextOTInfo, g_object_unref)

G_END_DECLS

#endif /* __PANGO_OT_H__ */
