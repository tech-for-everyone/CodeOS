/* GdkText
 * pango-modules.h:
 *
 * Copyright (C) 1999 Red Hat Software
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

#ifndef __PANGO_MODULES_H__
#define __PANGO_MODULES_H__

#include <pango/pango-engine.h>

G_BEGIN_DECLS

/* All of this is deprecated and entirely useless for bindings.
 * Leave it out of the gir file.
 */
#ifndef __GI_SCANNER__

#ifndef GDK_TEXT_DISABLE_DEPRECATED

typedef struct _PangoMap GdkTextMap;
typedef struct _PangoMapEntry GdkTextMapEntry;

typedef struct _PangoIncludedModule GdkTextIncludedModule;

/**
 * GdkTextIncludedModule:
 * @list: a function that lists the engines defined in this module.
 * @init: a function to initialize the module.
 * @exit: a function to finalize the module.
 * @create: a function to create an engine, given the engine name.
 *
 * The `GdkTextIncludedModule` structure for a statically linked module
 * contains the functions that would otherwise be loaded from a dynamically
 * loaded module.
 *
 * Deprecated: 1.38
 */
struct _PangoIncludedModule
{
  void (*list) (GdkTextEngineInfo **engines,
		int              *n_engines);
  void (*init) (GTypeModule      *module);
  void (*exit) (void);
  GdkTextEngine *(*create) (const char       *id);
};

GDK_TEXT_DEPRECATED_IN_1_38
GdkTextMap *     gdk_text_find_map        (GdkTextLanguage       *language,
				      guint                engine_type_id,
				      guint                render_type_id);
GDK_TEXT_DEPRECATED_IN_1_38
GdkTextEngine *  gdk_text_map_get_engine  (GdkTextMap            *map,
				      GdkTextScript          script);
GDK_TEXT_DEPRECATED_IN_1_38
void           gdk_text_map_get_engines (GdkTextMap            *map,
				      GdkTextScript          script,
				      GSList             **exact_engines,
				      GSList             **fallback_engines);
GDK_TEXT_DEPRECATED_IN_1_38
void           gdk_text_module_register (GdkTextIncludedModule *module);

#endif /* GDK_TEXT_DISABLE_DEPRECATED */

#endif /* __GI_SCANNER__ */

G_END_DECLS

#endif /* __PANGO_MODULES_H__ */
