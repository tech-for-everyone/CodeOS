/* GdkText
 * pangofc-fontmap.h: Base fontmap type for fontconfig-based backends
 *
 * Copyright (C) 2003 Red Hat Software
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

#ifndef __PANGO_FC_FONT_MAP_H__
#define __PANGO_FC_FONT_MAP_H__

#include <pango/pango.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangofc-decoder.h>
#include <pango/pangofc-font.h>
#include <hb.h>

G_BEGIN_DECLS


/*
 * GdkTextFcFontMap
 */

#ifdef __GI_SCANNER__
#define GDK_TEXT_FC_TYPE_FONT_MAP              (gdk_text_fc_font_map_get_type ())
#define GDK_TEXT_FC_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_FC_TYPE_FONT_MAP, GdkTextFcFontMap))
#define GDK_TEXT_FC_IS_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_FC_TYPE_FONT_MAP))
#else
#define GDK_TEXT_TYPE_FC_FONT_MAP              (gdk_text_fc_font_map_get_type ())
#define GDK_TEXT_FC_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_FC_FONT_MAP, GdkTextFcFontMap))
#define GDK_TEXT_IS_FC_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_FC_FONT_MAP))
#endif

typedef struct _PangoFcFontMap        GdkTextFcFontMap;
typedef struct _PangoFcFontMapClass   GdkTextFcFontMapClass;
typedef struct _PangoFcFontMapPrivate GdkTextFcFontMapPrivate;

GDK_TEXT_AVAILABLE_IN_ALL
GType gdk_text_fc_font_map_get_type (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_4
void           gdk_text_fc_font_map_cache_clear    (GdkTextFcFontMap *fcfontmap);

GDK_TEXT_AVAILABLE_IN_1_38
void
gdk_text_fc_font_map_config_changed (GdkTextFcFontMap *fcfontmap);

GDK_TEXT_AVAILABLE_IN_1_38
void
gdk_text_fc_font_map_set_config (GdkTextFcFontMap *fcfontmap,
			      FcConfig       *fcconfig);
GDK_TEXT_AVAILABLE_IN_1_38
FcConfig *
gdk_text_fc_font_map_get_config (GdkTextFcFontMap *fcfontmap);

/**
 * GdkTextFcDecoderFindFunc:
 * @pattern: a fully resolved `FcPattern` specifying the font on the system
 * @user_data: user data passed to
 *   [method@GdkTextFc.FontMap.add_decoder_find_func]
 *
 * Callback function passed to [method@GdkTextFc.FontMap.add_decoder_find_func].
 *
 * Return value: a new reference to a custom decoder for this pattern,
 *  or %NULL if the default decoder handling should be used.
 **/
typedef GdkTextFcDecoder * (*GdkTextFcDecoderFindFunc) (FcPattern *pattern,
						    gpointer   user_data);

GDK_TEXT_AVAILABLE_IN_1_6
void gdk_text_fc_font_map_add_decoder_find_func (GdkTextFcFontMap        *fcfontmap,
					      GdkTextFcDecoderFindFunc findfunc,
					      gpointer               user_data,
					      GDestroyNotify         dnotify);
GDK_TEXT_AVAILABLE_IN_1_26
GdkTextFcDecoder *gdk_text_fc_font_map_find_decoder (GdkTextFcFontMap *fcfontmap,
					        FcPattern      *pattern);

GDK_TEXT_AVAILABLE_IN_1_4
GdkTextFontDescription *gdk_text_fc_font_description_from_pattern (FcPattern *pattern,
							      gboolean   include_size);

#ifndef GDK_TEXT_DISABLE_DEPRECATED
GDK_TEXT_DEPRECATED_IN_1_22_FOR(gdk_text_font_map_create_context)
GdkTextContext * gdk_text_fc_font_map_create_context (GdkTextFcFontMap *fcfontmap);
#endif
GDK_TEXT_AVAILABLE_IN_1_4
void           gdk_text_fc_font_map_shutdown       (GdkTextFcFontMap *fcfontmap);


GDK_TEXT_AVAILABLE_IN_1_44
hb_face_t * gdk_text_fc_font_map_get_hb_face (GdkTextFcFontMap *fcfontmap,
                                           GdkTextFcFont    *fcfont);

/**
 * GdkTextFcSubstituteFunc:
 * @pattern: the FcPattern to tweak.
 * @data: user data.
 *
 * Function type for doing final config tweaking on prepared `FcPattern`s.
 */
typedef void (*GdkTextFcSubstituteFunc) (FcPattern *pattern,
				       gpointer   data);

/**
 * gdk_text_fc_font_map_set_default_substitute:
 * @fontmap: a `GdkTextFcFontMap`
 * @func: function to call to to do final config tweaking on `FcPattern` objects
 * @data: data to pass to @func
 * @notify: function to call when @data is no longer used
 *
 * Sets a function that will be called to do final configuration
 * substitution on a `FcPattern` before it is used to load
 * the font.
 *
 * This function can be used to do things like set
 * hinting and antialiasing options.
 *
 * Since: 1.48
 */
GDK_TEXT_AVAILABLE_IN_1_48
void gdk_text_fc_font_map_set_default_substitute (GdkTextFcFontMap        *fontmap,
					       GdkTextFcSubstituteFunc func,
					       gpointer              data,
					       GDestroyNotify        notify);

/**
 * gdk_text_fc_font_map_substitute_changed:
 * @fontmap: a `GdkTextFcFontMap`
 *
 * Call this function any time the results of the default
 * substitution function set with
 * [method@GdkTextFc.FontMap.set_default_substitute] change.
 *
 * That is, if your substitution function will return different
 * results for the same input pattern, you must call this function.
 *
 * Since: 1.48
 */
GDK_TEXT_AVAILABLE_IN_1_48
void gdk_text_fc_font_map_substitute_changed (GdkTextFcFontMap *fontmap);

/**
 * GDK_TEXT_FC_GRAVITY:
 *
 * Fontconfig property that GdkText sets on any
 * fontconfig pattern it passes to fontconfig
 * if a `GdkTextGravity` other than %GDK_TEXT_GRAVITY_SOUTH
 * is desired.
 *
 * The property will have a `GdkTextGravity` value as a string,
 * like "east". This can be used to write fontconfig configuration
 * rules to choose different fonts for horizontal and vertical
 * writing directions.
 *
 * Since: 1.20
 */
#define GDK_TEXT_FC_GRAVITY "pangogravity"

/**
 * GDK_TEXT_FC_VERSION:
 *
 * Fontconfig property that GdkText sets on any
 * fontconfig pattern it passes to fontconfig.
 *
 * The property will have an integer value equal to what
 * [func@GdkText.version] returns. This can be used to write
 * fontconfig configuration rules that only affect certain
 * pango versions (or only pango-using applications, or only
 * non-pango-using applications).
 *
 * Since: 1.20
 */
#define GDK_TEXT_FC_VERSION "pangoversion"

/**
 * GDK_TEXT_FC_PRGNAME:
 *
 * Fontconfig property that GdkText sets on any
 * fontconfig pattern it passes to fontconfig.
 *
 * The property will have a string equal to what
 * g_get_prgname() returns. This can be used to write
 * fontconfig configuration rules that only affect
 * certain applications.
 *
 * This is equivalent to FC_PRGNAME in versions of
 * fontconfig that have that.
 *
 * Since: 1.24
 *
 * Deprecated: 1.56: Use FC_PRGNAME
 */
#define GDK_TEXT_FC_PRGNAME "prgname"

/**
 * GDK_TEXT_FC_FONT_FEATURES:
 *
 * Fontconfig property that GdkText reads from font
 * patterns to populate list of OpenType features
 * to be enabled for the font by default.
 *
 * The property will have a number of string elements,
 * each of which is the OpenType feature tag of one feature
 * to enable.
 *
 * This is equivalent to FC_FONT_FEATURES in versions of
 * fontconfig that have that.
 *
 * Since: 1.34
 *
 * Deprecated: 1.56: Use FC_FONT_FEATURES
 */
#define GDK_TEXT_FC_FONT_FEATURES "fontfeatures"

/**
 * GDK_TEXT_FC_FONT_VARIATIONS:
 *
 * Fontconfig property that GdkText reads from font
 * patterns to populate list of OpenType font variations
 * to be used for a font.
 *
 * The property will have a string elements, each of which
 * a comma-separated list of OpenType axis setting of the
 * form AXIS=VALUE.
 *
 * This is equivalent to FC_FONT_VARIATIONS in versions of
 * fontconfig that have that.
 *
 * Deprecated: 1.56: Use FC_FONT_VARIATIONS
 */
#define GDK_TEXT_FC_FONT_VARIATIONS "fontvariations"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdkTextFcFontMap, g_object_unref)

G_END_DECLS

#endif /* __PANGO_FC_FONT_MAP_H__ */
