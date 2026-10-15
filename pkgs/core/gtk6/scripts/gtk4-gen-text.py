#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Vendor Pango's public headers into gdk/text/pango/ so the GTK4 sources keep
# including <pango/pango.h> / <pango/pangocairo.h> but resolve them to GTK6's
# own copies instead of the system ones.
#
# Why copy the real headers (with their real names and their real intra-pango
# #includes) instead of concatenating them into one gdk/gdktext.h:
#
#   An earlier attempt concatenated the headers and dropped every
#   #include <pango/...>.  That destroys the dependency graph: pango-attributes.h
#   is then read before the types and version macros it relies on, and the
#   whole tree fails with "expected ';' before 'int'" at every
#   GDK_TEXT_AVAILABLE_IN_* marker.  Keeping the files and their includes
#   intact keeps Pango's own ordering; -I$(HERE)/gdk/text makes the
#   <pango/...> prefix resolve to gdk/text/pango/.
#
# The generator only rewrites the two includes that cannot resolve any more:
#
#   <cairo.h>       -> <gdk/gdkpaint.h>      (cairo is gone; see gdkpaint.h)
#
# <hb.h> and <hb-ot.h> are provided as minimal opaque stubs in gdk/text/, so
# the pango headers' harfbuzz references keep a type to talk about.
#
# The symbol rename (Pango -> GdkText, PANGO_ -> GDK_TEXT_, pango_ -> gdk_text_,
# and the cairo types in pangocairo.h) is NOT done here.  It is done by
# scripts/gtk4-rename-pango.py and scripts/gtk4-rename-cairo.py, the same two
# scripts that rewrite the GTK4 sources, so the headers and their users can
# never drift apart.  File names are deliberately left unchanged, matching the
# cairo precedent (gdk/gdkcairo.c still has that name too).
#
# The generated headers are committed; this script is the reproducible spec.
#
# Usage: scripts/gtk4-gen-text.py [--check]

import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = "/usr/include/pango-1.0/pango"
OUT = os.path.join(ROOT, "gdk", "text")
OUT_PANGO = os.path.join(OUT, "pango")

# hb.h: Pango's public API only ever mentions these by pointer, so opaque
# forward declarations are enough to parse it.  The real shaping lives in Qt.
HB_H = """\
/* SPDX-License-Identifier: MIT */
/* GTK6: minimal stand-in for harfbuzz's hb.h.  Pango's public headers only
 * mention these types by pointer (see pango-font.h, pango-coverage.h); the
 * real shaping is done by Qt6's bundled harfbuzz. */
#ifndef HB_H
#define HB_H
typedef struct hb_font_t hb_font_t;
typedef struct hb_face_t hb_face_t;
typedef struct hb_feature_t hb_feature_t;
typedef struct hb_glyph_info_t hb_glyph_info_t;
#endif
"""

HB_OT_H = """\
/* SPDX-License-Identifier: MIT */
/* GTK6: minimal stand-in for harfbuzz's hb-ot.h; see hb.h. */
#ifndef HB_OT_H
#define HB_OT_H
#include <hb.h>
#endif
"""

# gdk/gdktext.h: the single public entry point.  It simply pulls in the two
# Pango umbrellas, which resolve to the vendored copies on the gdk/text
# include path.  Kept as a real header (rather than rewriting every
# #include <pango/pango.h>) so the sources and the docs have one name to
# point at, the way gdk/gdkpaint.h replaced cairo.h.
GDKTEXT_H = """\
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
"""


def vendored_text(src):
    """Pango header body, adjusted for CodeOS' include set.

    The only structural change is cairo.h -> gdkpaint.h; every
    #include <pango/...> is kept so the copies resolve to each other.
    """
    return src.replace("#include <cairo.h>", "#include <gdk/gdkpaint.h>")


def emit(path, text, check):
    """Write text to path (unless --check), return True if it changed."""
    old = None
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
            old = f.read()
    changed = old != text
    if changed and not check:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report files that would change, do not write")
    args = ap.parse_args()

    if not os.path.isdir(PKG):
        sys.exit(f"gtk4-gen-text: {PKG} not found")

    os.makedirs(OUT_PANGO, exist_ok=True)

    changed = []
    for path, text in (("hb.h", HB_H), ("hb-ot.h", HB_OT_H),
                       (os.path.join(ROOT, "gdk", "gdktext.h"), GDKTEXT_H)):
        target = os.path.join(OUT, path) if not os.path.isabs(path) else path
        if emit(target, text, args.check):
            changed.append(target)

    # Vendor every public Pango header, byte for byte, except for the one
    # include that no longer exists on this build's include path.
    for name in sorted(os.listdir(PKG)):
        if not name.endswith(".h"):
            continue
        with open(os.path.join(PKG, name), "r",
                  encoding="utf-8", errors="surrogateescape") as f:
            body = f.read()
        target = os.path.join(OUT_PANGO, name)
        if emit(target, vendored_text(body), args.check):
            changed.append(target)

    verb = "would change" if args.check else "wrote"
    print(f"gtk4-gen-text: {len(changed)} files {verb}")
    for path in changed[:20]:
        print("  " + os.path.relpath(path, ROOT))
    if len(changed) > 20:
        print(f"  ... and {len(changed) - 20} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
