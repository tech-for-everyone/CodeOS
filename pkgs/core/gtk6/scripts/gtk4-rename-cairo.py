#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The cairo -> GdkPaint rename, performed mechanically.
#
# GTK6 owns the drawing API that GTK4 got from cairo (gdk/gdkpaint.h and
# gdk/qt/gdkqtpaint.cpp).  The imported GTK4 sources are ported by renaming the
# symbols they name, in place and in a fixed order, rather than by editing
# call sites: the order below is the whole specification, so it can be
# re-run and reviewed.  The names on the left are cairo's, the names on the
# right are the ones gdkpaint.h (and gdkregion.h) declare.
#
# It also swaps the cairo headers for gdk/gdkpaint.h, because <cairo.h> is not
# on this build's include path any more (see the negative controls: naming a
# cairo type is a compile error).
#
# Deliberately NOT done here:
#   * renaming files (gdk/gdkcairo.c, gsk/gskcairo*.c).  They keep their names
#     for now even though they no longer name a cairo symbol; see PLAN.md.
#   * pango / gdk-pixbuf.  They are the next dependency to remove and are
#     still named as-is.
#
# Usage: scripts/gtk4-rename-cairo.py [--check] [ROOT ...]

import argparse
import os
import re
import sys

# Files this script must not touch: they are GTK6's own and already spell the
# new API, and their comments document the rename (a mechanical pass would turn
# "cairo_region_* -> gdk_region_*" into "gdk_region_* -> gdk_region_*").
SKIP = {
    "gdk/gdkpaint.h",
    "gdk/gdkregion.h",
    "gdk/gdkregion.c",
    "gdk/gdkpaintenumtypes.c",
}

# (pattern, replacement).  Applied in order, first match wins per position.
RULES = [
    # ── cairo headers -> gdkpaint.h ─────────────────────────────────────
    (r"#\s*include\s*<cairo[^>]*>", "#include <gdk/gdkpaint.h>"),

    # ── GDK's own cairo wrappers and the GdkCairoContext class ──────────
    # The context has to be lifted out before gdk_cairo_ below would eat its
    # prefix.
    (r"\bgdk_cairo_context_cairo_create\b", "gdk_paint_context_get_painter"),
    (r"\bgdk_cairo_context_",               "gdk_paint_context_"),
    (r"\bgdk_cairo_context\b",              "gdk_paint_context"),
    (r"\bGdkCairoContext",                  "GdkPaintContext"),
    (r"\b_GdkCairoContext",                 "_GdkPaintContext"),
    (r"gdk_surface_create_cairo_context\b", "gdk_surface_create_paint_context"),
    (r"\bGDK_TYPE_CAIRO_CONTEXT\b",         "GDK_TYPE_PAINT_CONTEXT"),
    (r"\bGDK_CAIRO_CONTEXT\b",              "GDK_PAINT_CONTEXT"),
    (r"\bCAIRO_CONTEXT\b",                  "PAINT_CONTEXT"),
    (r"\bgdk_cairo_",                       "gdk_painter_"),

    # ── cairo-gobject GTypes ────────────────────────────────────────────
    (r"\bCAIRO_GOBJECT_TYPE_ANTIALIAS\b",       "GDK_PAINT_TYPE_ANTIALIAS"),
    (r"\bCAIRO_GOBJECT_TYPE_HINT_STYLE\b",      "GDK_PAINT_TYPE_HINT_STYLE"),
    (r"\bCAIRO_GOBJECT_TYPE_HINT_METRICS\b",    "GDK_PAINT_TYPE_HINT_METRICS"),
    (r"\bCAIRO_GOBJECT_TYPE_SUBPIXEL_ORDER\b",  "GDK_PAINT_TYPE_SUBPIXEL_ORDER"),
    (r"\bCAIRO_GOBJECT_TYPE_RECTANGLE_INT\b",   "GDK_TYPE_RECTANGLE"),
    (r"\bCAIRO_GOBJECT_TYPE_RECTANGLE\b",       "GDK_TYPE_RECTANGLE"),
    (r"\bCAIRO_GOBJECT_TYPE_REGION\b",          "GDK_TYPE_REGION"),

    # ── the region type and its constants ───────────────────────────────
    (r"\bCAIRO_REGION_OVERLAP_", "GDK_REGION_OVERLAP_"),
    (r"\bcairo_region_t\b",      "GdkRegion"),
    (r"\bcairo_region_",         "gdk_region_"),

    # ── cairo types, longest name first ─────────────────────────────────
    (r"\bcairo_script_interpreter_hooks_t\b", "GdkPaintScriptInterpreterHooks"),
    (r"\bcairo_script_interpreter_t\b",       "GdkPaintScriptInterpreter"),
    (r"\bcairo_rectangle_int_t\b",            "GdkRectangle"),
    (r"\bcairo_rectangle_t\b",                "GdkPaintRectangle"),
    (r"\bcairo_user_data_key_t\b",            "GdkPaintUserDataKey"),
    (r"\bcairo_destroy_func_t\b",             "GdkPaintDestroyFunc"),
    (r"\bcairo_write_func_t\b",               "GdkPaintWriteFunc"),
    (r"\bcairo_path_data_t\b",                "GdkPaintPathData"),
    (r"\bcairo_path_t\b",                     "GdkPaintPath"),
    (r"\bcairo_surface_t\b",                  "GdkPaintSurface"),
    (r"\bcairo_pattern_t\b",                  "GdkPaintPattern"),
    (r"\bcairo_scaled_font_t\b",              "GdkPaintScaledFont"),
    (r"\bcairo_font_options_t\b",             "GdkPaintFontOptions"),
    (r"\bcairo_matrix_t\b",                   "GdkPaintMatrix"),
    (r"\bcairo_device_t\b",                   "GdkPaintDevice"),
    (r"\bcairo_status_t\b",                   "GdkPaintStatus"),
    (r"\bcairo_format_t\b",                   "GdkPaintFormat"),
    (r"\bcairo_content_t\b",                  "GdkPaintContent"),
    (r"\bcairo_operator_t\b",                 "GdkPaintOperator"),
    (r"\bcairo_antialias_t\b",                "GdkPaintAntialias"),
    (r"\bcairo_hint_style_t\b",               "GdkPaintHintStyle"),
    (r"\bcairo_hint_metrics_t\b",             "GdkPaintHintMetrics"),
    (r"\bcairo_subpixel_order_t\b",           "GdkPaintSubpixelOrder"),
    (r"\bcairo_extend_t\b",                   "GdkPaintExtend"),
    (r"\bcairo_filter_t\b",                   "GdkPaintFilter"),
    (r"\bcairo_fill_rule_t\b",                "GdkPaintFillRule"),
    (r"\bcairo_line_cap_t\b",                 "GdkPaintLineCap"),
    (r"\bcairo_line_join_t\b",                "GdkPaintLineJoin"),
    (r"\bcairo_t\b",                          "GdkPainter"),

    # ── constants, then the function prefix ─────────────────────────────
    (r"\bCAIRO_", "GDK_PAINT_"),
    (r"\bcairo_", "gdk_paint_"),
]

COMPILED = [(re.compile(p), r) for p, r in RULES]


def rename_text(text):
    for rx, repl in COMPILED:
        text = rx.sub(repl, text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report files that would change, do not write")
    ap.add_argument("roots", nargs="*", default=["gdk", "gsk", "gtk"])
    args = ap.parse_args()

    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(here)

    changed = []
    for root in (args.roots or ["gdk", "gsk", "gtk"]):
        for dirpath, _dirs, files in os.walk(root):
            for name in sorted(files):
                if not name.endswith((".c", ".h")):
                    continue
                path = os.path.join(dirpath, name)
                if path in SKIP:
                    continue
                with open(path, encoding="utf-8", errors="surrogateescape") as f:
                    original = f.read()
                updated = rename_text(original)
                if updated == original:
                    continue
                changed.append(path)
                if not args.check:
                    with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                        f.write(updated)

    verb = "would change" if args.check else "rewritten"
    print(f"gtk4-rename-cairo: {len(changed)} files {verb}")
    for path in changed[:20]:
        print(f"  {path}")
    if len(changed) > 20:
        print(f"  ... and {len(changed) - 20} more")

    return 0


if __name__ == "__main__":
    sys.exit(main())
