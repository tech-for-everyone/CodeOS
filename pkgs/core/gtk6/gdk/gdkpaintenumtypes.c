/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTypes for GDK's drawing enums — see gdk/gdkpaint.h.
 *
 * Upstream these live in cairo-gobject: CAIRO_GOBJECT_TYPE_ANTIALIAS and
 * friends.  GTK6 owns the drawing API outright, so it owns the boxed/enum
 * GTypes too, and the sources name them exactly where they named the cairo
 * ones (GDK_PAINT_TYPE_ANTIALIAS, ...).
 *
 * This is a C file on purpose.  gdkpaint.h only declares the GType accessors
 * when GDK_PAINT_WANT_TYPES is set, which its own logic sets for every C
 * consumer and which the C++ backend (gdk/qt/gdkqtpaint.cpp) deliberately
 * leaves off so that it needs no glib.
 */

#include "config.h"

#include <gdk/gdkpaint.h>

/* GdkPaintAntialias */
GType
gdk_paint_antialias_get_type (void)
{
  static gsize id = 0;

  if (g_once_init_enter (&id))
    {
      static const GEnumValue values[] = {
        { GDK_PAINT_ANTIALIAS_DEFAULT,  "GDK_PAINT_ANTIALIAS_DEFAULT",  "default"  },
        { GDK_PAINT_ANTIALIAS_NONE,     "GDK_PAINT_ANTIALIAS_NONE",     "none"     },
        { GDK_PAINT_ANTIALIAS_GRAY,     "GDK_PAINT_ANTIALIAS_GRAY",     "gray"     },
        { GDK_PAINT_ANTIALIAS_SUBPIXEL, "GDK_PAINT_ANTIALIAS_SUBPIXEL", "subpixel" },
        { GDK_PAINT_ANTIALIAS_FAST,     "GDK_PAINT_ANTIALIAS_FAST",     "fast"     },
        { GDK_PAINT_ANTIALIAS_GOOD,     "GDK_PAINT_ANTIALIAS_GOOD",     "good"     },
        { GDK_PAINT_ANTIALIAS_BEST,     "GDK_PAINT_ANTIALIAS_BEST",     "best"     },
        { 0, NULL, NULL }
      };

      g_once_init_leave (&id, g_enum_register_static ("GdkPaintAntialias", values));
    }

  return id;
}

/* GdkPaintHintStyle */
GType
gdk_paint_hint_style_get_type (void)
{
  static gsize id = 0;

  if (g_once_init_enter (&id))
    {
      static const GEnumValue values[] = {
        { GDK_PAINT_HINT_STYLE_DEFAULT, "GDK_PAINT_HINT_STYLE_DEFAULT", "default" },
        { GDK_PAINT_HINT_STYLE_NONE,    "GDK_PAINT_HINT_STYLE_NONE",    "none"    },
        { GDK_PAINT_HINT_STYLE_SLIGHT,  "GDK_PAINT_HINT_STYLE_SLIGHT",  "slight"  },
        { GDK_PAINT_HINT_STYLE_MEDIUM,  "GDK_PAINT_HINT_STYLE_MEDIUM",  "medium"  },
        { GDK_PAINT_HINT_STYLE_FULL,    "GDK_PAINT_HINT_STYLE_FULL",    "full"    },
        { 0, NULL, NULL }
      };

      g_once_init_leave (&id, g_enum_register_static ("GdkPaintHintStyle", values));
    }

  return id;
}

/* GdkPaintHintMetrics */
GType
gdk_paint_hint_metrics_get_type (void)
{
  static gsize id = 0;

  if (g_once_init_enter (&id))
    {
      static const GEnumValue values[] = {
        { GDK_PAINT_HINT_METRICS_DEFAULT, "GDK_PAINT_HINT_METRICS_DEFAULT", "default" },
        { GDK_PAINT_HINT_METRICS_OFF,     "GDK_PAINT_HINT_METRICS_OFF",     "off"     },
        { GDK_PAINT_HINT_METRICS_ON,      "GDK_PAINT_HINT_METRICS_ON",      "on"      },
        { 0, NULL, NULL }
      };

      g_once_init_leave (&id, g_enum_register_static ("GdkPaintHintMetrics", values));
    }

  return id;
}

/* GdkPaintSubpixelOrder */
GType
gdk_paint_subpixel_order_get_type (void)
{
  static gsize id = 0;

  if (g_once_init_enter (&id))
    {
      static const GEnumValue values[] = {
        { GDK_PAINT_SUBPIXEL_ORDER_DEFAULT, "GDK_PAINT_SUBPIXEL_ORDER_DEFAULT", "default" },
        { GDK_PAINT_SUBPIXEL_ORDER_RGB,     "GDK_PAINT_SUBPIXEL_ORDER_RGB",     "rgb"     },
        { GDK_PAINT_SUBPIXEL_ORDER_BGR,     "GDK_PAINT_SUBPIXEL_ORDER_BGR",     "bgr"     },
        { GDK_PAINT_SUBPIXEL_ORDER_VRGB,    "GDK_PAINT_SUBPIXEL_ORDER_VRGB",    "vrgb"    },
        { GDK_PAINT_SUBPIXEL_ORDER_VBGR,    "GDK_PAINT_SUBPIXEL_ORDER_VBGR",    "vbgr"    },
        { 0, NULL, NULL }
      };

      g_once_init_leave (&id, g_enum_register_static ("GdkPaintSubpixelOrder", values));
    }

  return id;
}
