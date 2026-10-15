/* GdkText
 * pango-engine.h: Engines for script and language specific processing
 *
 * Copyright (C) 2000,2003 Red Hat Software
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

#ifndef __PANGO_ENGINE_H__
#define __PANGO_ENGINE_H__

#include <pango/pango-types.h>
#include <pango/pango-item.h>
#include <pango/pango-font.h>
#include <pango/pango-glyph.h>
#include <pango/pango-script.h>

G_BEGIN_DECLS

/* All of this is deprecated and entirely useless for bindings.
 * Leave it out of the gir file.
 */
#ifndef __GI_SCANNER__

#ifndef GDK_TEXT_DISABLE_DEPRECATED

/**
 * GDK_TEXT_RENDER_TYPE_NONE:
 *
 * A string constant defining the render type
 * for engines that are not rendering-system specific.
 *
 * Deprecated: 1.38
 */
#define GDK_TEXT_RENDER_TYPE_NONE "GdkTextRenderNone"

#define GDK_TEXT_TYPE_ENGINE              (gdk_text_engine_get_type ())
#define GDK_TEXT_ENGINE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_ENGINE, GdkTextEngine))
#define GDK_TEXT_IS_ENGINE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_ENGINE))
#define GDK_TEXT_ENGINE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_ENGINE, GdkTextEngineClass))
#define GDK_TEXT_IS_ENGINE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_ENGINE))
#define GDK_TEXT_ENGINE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_ENGINE, GdkTextEngineClass))

typedef struct _PangoEngine GdkTextEngine;
typedef struct _PangoEngineClass GdkTextEngineClass;

/**
 * GdkTextEngine:
 *
 * `GdkTextEngine` is the base class for all types of language and
 * script specific engines. It has no functionality by itself.
 *
 * Deprecated: 1.38
 **/
struct _PangoEngine
{
  /*< private >*/
  GObject parent_instance;
};

/**
 * GdkTextEngineClass:
 *
 * Class structure for `GdkTextEngine`
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineClass
{
  /*< private >*/
  GObjectClass parent_class;
};

GDK_TEXT_DEPRECATED_IN_1_38
GType gdk_text_engine_get_type (void) G_GNUC_CONST;

/**
 * GDK_TEXT_ENGINE_TYPE_LANG:
 *
 * A string constant defining the engine type for language engines.
 * These engines derive from `GdkTextEngineLang`.
 *
 * Deprecated: 1.38
 */
#define GDK_TEXT_ENGINE_TYPE_LANG "GdkTextEngineLang"

#define GDK_TEXT_TYPE_ENGINE_LANG              (gdk_text_engine_lang_get_type ())
#define GDK_TEXT_ENGINE_LANG(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_ENGINE_LANG, GdkTextEngineLang))
#define GDK_TEXT_IS_ENGINE_LANG(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_ENGINE_LANG))
#define GDK_TEXT_ENGINE_LANG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_ENGINE_LANG, GdkTextEngineLangClass))
#define GDK_TEXT_IS_ENGINE_LANG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_ENGINE_LANG))
#define GDK_TEXT_ENGINE_LANG_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_ENGINE_LANG, GdkTextEngineLangClass))

typedef struct _PangoEngineLangClass GdkTextEngineLangClass;

/**
 * GdkTextEngineLang:
 *
 * The `GdkTextEngineLang` class is implemented by engines that
 * customize the rendering-system independent part of the
 * GdkText pipeline for a particular script or language. For
 * instance, a custom `GdkTextEngineLang` could be provided for
 * Thai to implement the dictionary-based word boundary
 * lookups needed for that language.
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineLang
{
  /*< private >*/
  GdkTextEngine parent_instance;
};

/**
 * GdkTextEngineLangClass:
 * @script_break: (nullable): Provides a custom implementation of
 * gdk_text_break().  If %NULL, gdk_text_default_break() is used instead. If
 * not %NULL, for GdkText versions before 1.16 (module interface version
 * before 1.6.0), this was called instead of gdk_text_default_break(),
 * but in newer versions, gdk_text_default_break() is always called and
 * this is called after that to allow tailoring the breaking results.
 *
 * Class structure for `GdkTextEngineLang`
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineLangClass
{
  /*< private >*/
  GdkTextEngineClass parent_class;

  /*< public >*/
  void (*script_break) (GdkTextEngineLang *engine,
			const char    *text,
			int            len,
			GdkTextAnalysis *analysis,
			GdkTextLogAttr  *attrs,
			int            attrs_len);
};

GDK_TEXT_DEPRECATED_IN_1_38
GType gdk_text_engine_lang_get_type (void) G_GNUC_CONST;

/**
 * GDK_TEXT_ENGINE_TYPE_SHAPE:
 *
 * A string constant defining the engine type for shaping engines.
 * These engines derive from `GdkTextEngineShape`.
 *
 * Deprecated: 1.38
 */
#define GDK_TEXT_ENGINE_TYPE_SHAPE "GdkTextEngineShape"

#define GDK_TEXT_TYPE_ENGINE_SHAPE              (gdk_text_engine_shape_get_type ())
#define GDK_TEXT_ENGINE_SHAPE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GDK_TEXT_TYPE_ENGINE_SHAPE, GdkTextEngineShape))
#define GDK_TEXT_IS_ENGINE_SHAPE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GDK_TEXT_TYPE_ENGINE_SHAPE))
#define GDK_TEXT_ENGINE_SHAPE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GDK_TEXT_TYPE_ENGINE_SHAPE, GdkTextEngineShapeClass))
#define GDK_TEXT_IS_ENGINE_SHAPE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GDK_TEXT_TYPE_ENGINE_SHAPE))
#define GDK_TEXT_ENGINE_SHAPE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GDK_TEXT_TYPE_ENGINE_SHAPE, GdkTextEngineShapeClass))

typedef struct _PangoEngineShapeClass GdkTextEngineShapeClass;

/**
 * GdkTextEngineShape:
 *
 * The `GdkTextEngineShape` class is implemented by engines that
 * customize the rendering-system dependent part of the
 * GdkText pipeline for a particular script or language.
 * A `GdkTextEngineShape` implementation is then specific to both
 * a particular rendering system or group of rendering systems
 * and to a particular script. For instance, there is one
 * `GdkTextEngineShape` implementation to handle shaping Arabic
 * for Fontconfig-based backends.
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineShape
{
  GdkTextEngine parent_instance;
};

/**
 * GdkTextEngineShapeClass:
 * @script_shape: Given a font, a piece of text, and a `GdkTextAnalysis`
 *   structure, converts characters to glyphs and positions the
 *   resulting glyphs. The results are stored in the `GdkTextGlyphString`
 *   that is passed in. (The implementation should resize it
 *   appropriately using gdk_text_glyph_string_set_size()). All fields
 *   of the @log_clusters and @glyphs array must be filled in, with
 *   the exception that GdkText will automatically generate
 *   `glyphs->glyphs[i].attr.is_cluster_start`
 *   using the @log_clusters array. Each input character must occur in one
 *   of the output logical clusters;
 *   if no rendering is desired for a character, this may involve
 *   inserting glyphs with the `GdkTextGlyph` ID %GDK_TEXT_GLYPH_EMPTY, which
 *   is guaranteed never to render. If the shaping fails for any reason,
 *   the shaper should return with an empty (zero-size) glyph string.
 *   If the shaper has not set the size on the glyph string yet, simply
 *   returning signals the failure too.
 * @covers: Returns the characters that this engine can cover
 *   with a given font for a given language. If not overridden, the default
 *   implementation simply returns the coverage information for the
 *   font itself unmodified.
 *
 * Class structure for `GdkTextEngineShape`
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineShapeClass
{
  /*< private >*/
  GdkTextEngineClass parent_class;

  /*< public >*/
  void (*script_shape) (GdkTextEngineShape    *engine,
			GdkTextFont           *font,
			const char          *item_text,
			unsigned int         item_length,
			const GdkTextAnalysis *analysis,
			GdkTextGlyphString    *glyphs,
			const char          *paragraph_text,
			unsigned int         paragraph_length);
  GdkTextCoverageLevel (*covers)   (GdkTextEngineShape *engine,
				  GdkTextFont        *font,
				  GdkTextLanguage    *language,
				  gunichar          wc);
};

GDK_TEXT_DEPRECATED_IN_1_38
GType gdk_text_engine_shape_get_type (void) G_GNUC_CONST;

typedef struct _PangoEngineInfo GdkTextEngineInfo;
typedef struct _PangoEngineScriptInfo GdkTextEngineScriptInfo;

/**
 * GdkTextEngineScriptInfo:
 * @script: a `GdkTextScript`. The value %GDK_TEXT_SCRIPT_COMMON has
 * the special meaning here of "all scripts"
 * @langs: a semicolon separated list of languages that this
 * engine handles for this script. This may be empty,
 * in which case the engine is saying that it is a
 * fallback choice for all languages for this range,
 * but should not be used if another engine
 * indicates that it is specific for the language for
 * a given code point. An entry in this list of "*"
 * indicates that this engine is specific to all
 * languages for this range.
 *
 * The `GdkTextEngineScriptInfo` structure contains
 * information about how the shaper covers a particular script.
 *
 * Deprecated: 1.38
 */
struct _PangoEngineScriptInfo
{
  GdkTextScript script;
  const gchar *langs;
};

/**
 * GdkTextEngineInfo:
 * @id: a unique string ID for the engine.
 * @engine_type: a string identifying the engine type.
 * @render_type: a string identifying the render type.
 * @scripts: array of scripts this engine supports.
 * @n_scripts: number of items in @scripts.
 *
 * The `GdkTextEngineInfo` structure contains information about a particular
 * engine. It contains the following fields:
 *
 * Deprecated: 1.38
 */
struct _PangoEngineInfo
{
  const gchar *id;
  const gchar *engine_type;
  const gchar *render_type;
  GdkTextEngineScriptInfo *scripts;
  gint n_scripts;
};

/**
 * script_engine_list: (skip)
 * @engines: location to store a pointer to an array of engines.
 * @n_engines: location to store the number of elements in @engines.
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
GDK_TEXT_DEPRECATED_IN_1_38
void script_engine_list (GdkTextEngineInfo **engines,
			 int              *n_engines);

/**
 * script_engine_init: (skip)
 * @module: a `GTypeModule` structure used to associate any
 *  GObject types created in this module with the module.
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
GDK_TEXT_DEPRECATED_IN_1_38
void script_engine_init (GTypeModule *module);


/**
 * script_engine_exit: (skip)
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
GDK_TEXT_DEPRECATED_IN_1_38
void script_engine_exit (void);

/**
 * script_engine_create: (skip)
 * @id: the ID of an engine as reported by script_engine_list.
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
GDK_TEXT_DEPRECATED_IN_1_38
GdkTextEngine *script_engine_create (const char *id);

/* Utility macro used by GDK_TEXT_ENGINE_LANG_DEFINE_TYPE and
 * GDK_TEXT_ENGINE_LANG_DEFINE_TYPE
 */
#define GDK_TEXT_ENGINE_DEFINE_TYPE(name, prefix, class_init, instance_init, parent_type) \
static GType prefix ## _type;						  \
static void								  \
prefix ## _register_type (GTypeModule *module)				  \
{									  \
  const GTypeInfo object_info =						  \
    {									  \
      sizeof (name ## Class),						  \
      (GBaseInitFunc) NULL,						  \
      (GBaseFinalizeFunc) NULL,						  \
      (GClassInitFunc) class_init,					  \
      (GClassFinalizeFunc) NULL,					  \
      NULL,          /* class_data */					  \
      sizeof (name),							  \
      0,             /* n_prelocs */					  \
      (GInstanceInitFunc) instance_init,				  \
      NULL           /* value_table */					  \
    };									  \
									  \
  prefix ## _type =  g_type_module_register_type (module, parent_type,	  \
						  # name,		  \
						  &object_info, 0);	  \
}

/**
 * GDK_TEXT_ENGINE_LANG_DEFINE_TYPE:
 * @name: Name of the the type to register (for example:, ArabicEngineFc)
 * @prefix: Prefix for symbols that will be defined (for example:, arabic_engine_fc)
 * @class_init: (nullable): Class initialization function for the new type
 * @instance_init: (nullable): Instance initialization function for the new type
 *
 * Outputs the necessary code for GObject type registration for a
 * `GdkTextEngineLang` class defined in a module. Two static symbols
 * are defined.
 *
 * <programlisting>
 *  static GType *prefix*_type;
 *  static void *prefix*_register_type (GTypeModule module);
 *
 * The *prefix*_register_type()
 * function should be called in your script_engine_init() function for
 * each type that your module implements, and then your script_engine_create()
 * function can create instances of the object as follows:
 *
 * ```
 * GdkTextEngine *engine = g_object_new (prefix_type, NULL);
 * ```
 *
 * Deprecated: 1.38
 **/
#define GDK_TEXT_ENGINE_LANG_DEFINE_TYPE(name, prefix, class_init, instance_init)	\
  GDK_TEXT_ENGINE_DEFINE_TYPE (name, prefix,				\
			    class_init, instance_init,			\
			    GDK_TEXT_TYPE_ENGINE_LANG)

/**
 * GDK_TEXT_ENGINE_SHAPE_DEFINE_TYPE:
 * @name: Name of the the type to register (for example:, ArabicEngineFc)
 * @prefix: Prefix for symbols that will be defined (for example:, arabic_engine_fc)
 * @class_init: (nullable): Class initialization function for the new type
 * @instance_init: (nullable): Instance initialization function for the new type
 *
 * Outputs the necessary code for GObject type registration for a
 * `GdkTextEngineShape` class defined in a module. Two static symbols
 * are defined.
 *
 * <programlisting>
 *  static GType *prefix*_type;
 *  static void *prefix*_register_type (GTypeModule module);
 * </programlisting>
 *
 * The *prefix*_register_type()
 * function should be called in your script_engine_init() function for
 * each type that your module implements, and then your script_engine_create()
 * function can create instances of the object as follows:
 *
 * ```
 * GdkTextEngine *engine = g_object_new (prefix_type, NULL);
 * ```
 *
 * Deprecated: 1.38
 **/
#define GDK_TEXT_ENGINE_SHAPE_DEFINE_TYPE(name, prefix, class_init, instance_init)	\
  GDK_TEXT_ENGINE_DEFINE_TYPE (name, prefix,				\
			    class_init, instance_init,			\
			    GDK_TEXT_TYPE_ENGINE_SHAPE)

/* Macro used for possibly builtin GdkText modules. Not useful
 * for externally build modules. If we are compiling a module standalone,
 * then we name the entry points script_engine_list, etc. But if we
 * are compiling it for inclusion directly in GdkText, then we need them to
 * to have distinct names for this module, so we prepend a prefix.
 *
 * The two intermediate macros are to deal with details of the C
 * preprocessor; token pasting tokens must be function arguments,
 * and macro substitution isn't used on function arguments that
 * are used for token pasting.
 */
#ifdef GDK_TEXT_MODULE_PREFIX
#define GDK_TEXT_MODULE_ENTRY(func) _PANGO_MODULE_ENTRY2(GDK_TEXT_MODULE_PREFIX,func)
#define _PANGO_MODULE_ENTRY2(prefix,func) _PANGO_MODULE_ENTRY3(prefix,func)
#define _PANGO_MODULE_ENTRY3(prefix,func) prefix##_script_engine_##func
#else
#define GDK_TEXT_MODULE_ENTRY(func) script_engine_##func
#endif

#endif /* GDK_TEXT_DISABLE_DEPRECATED */

#endif /* __GI_SCANNER__ */

G_END_DECLS

#endif /* __PANGO_ENGINE_H__ */
