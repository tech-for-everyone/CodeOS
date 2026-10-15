#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Mechanical include rename for the gdk-pixbuf -> GTK6 gdkpixbuf.h step.
#
# Unlike the cairo and pango passes, nothing here is renamed: gdk-pixbuf's
# public types and functions are already spelled in the Gdk namespace
# (GdkPixbuf, gdk_pixbuf_*(), GDK_PIXBUF_*, GDK_COLORSPACE_*), so GTK6 just
# owns them (gdk/gdkpixbuf.h, gdk/gdkpixdata.h).  The only thing to change in
# the imported sources is which header they include, and the include guards
# are what say "the system gdk-pixbuf is gone":
#
#   <gdk-pixbuf/gdk-pixbuf.h>        -> <gdk/gdkpixbuf.h>
#   <gdk-pixbuf/gdk-pixdata.h>       -> <gdk/gdkpixdata.h>
#   <gdk/deprecated/gdkpixbuf.h>     -> <gdk/gdkpixbuf.h>
#
# The two gdk headers are hand-written to the surface the tree names (like
# gdk/gdkpaint.h), so there is no generator here.  This script is the
# reproducible spec and is idempotent.
#
# Usage: scripts/gtk4-rename-pixbuf.py [--check]

import argparse
import os
import re
import sys

RULES = [
    (re.compile(r'#\s*include\s*<gdk-pixbuf/gdk-pixbuf\.h>'),
     '#include <gdk/gdkpixbuf.h>'),
    (re.compile(r'#\s*include\s*<gdk-pixbuf/gdk-pixdata\.h>'),
     '#include <gdk/gdkpixdata.h>'),
    (re.compile(r'#\s*include\s*<gdk/deprecated/gdkpixbuf\.h>'),
     '#include <gdk/gdkpixbuf.h>'),
]

ROOTS = ("gdk", "gsk", "gtk")


def rewrite(text):
    for rx, repl in RULES:
        text = rx.sub(repl, text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report files that would change, do not write")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)

    changed = []
    for top in ("gdk", "gsk", "gtk"):
        for dirpath, _dirs, files in os.walk(top):
            for name in sorted(files):
                if not name.endswith((".c", ".h")):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
                    text = f.read()
                new = rewrite(text)
                if new != text:
                    changed.append(path)
                    if not args.check:
                        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                            f.write(new)

    verb = "would change" if args.check else "rewritten"
    print(f"gtk4-rename-pixbuf: {len(changed)} files {verb}")
    for path in changed:
        print("  " + path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
