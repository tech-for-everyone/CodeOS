/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * gdktext.h: GTK6's text API.
 *
 * This is the entry point for the text types and functions the GTK4 sources
 * used to get from Pango.  The bodies are Pango's public headers, vendored
 * under gdk/text/pango/ and mechanically renamed (Pango -> GdkText,
 * PANGO_ -> GDK_TEXT_, pango_ -> gdk_text_; see scripts/gtk4-rename-pango.py).
 * They are reached through the include path, so <pango/pango.h> and
 * <pango/pangocairo.h> in the sources resolve here.
 *
 * This header is pulled in from the end of gdktypes.h, the same way
 * gdk/gdkpaint.h replaced cairo.h.
 */
#pragma once

#include <glib-object.h>
#include <gdk/gdkpaint.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
