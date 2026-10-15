#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Mechanical rename of Pango -> GdkText and the GDK/GSK/GTK pango glue.
#
# The headers themselves are vendored, under their original names, in
# gdk/text/pango/ by scripts/gtk4-gen-text.py, and gdk/text is on the include
# path.  So <pango/pango.h> and <pango/pangocairo.h> still work in the sources
# and inside the vendored headers; this pass only rewrites the symbols, in the
# sources *and* in the vendored copies, so the two can never drift apart.
#
# This deliberately does not rewrite any #include: the vendored headers include
# each other by their <pango/...> names, and those must keep resolving.
#
# The cairo types that pangocairo.h mentions are renamed by
# scripts/gtk4-rename-cairo.py; run that first (it is also idempotent).
#
# See PLAN.md for the rationale.

import argparse
import os
import re
import sys

SKIP = {
    # Our own text API: no Pango symbols, only the two <pango/...> includes.
    "gdk/gdktext.h",
    # Our own harfbuzz stand-ins: comments mention Pango, no symbols to rename.
    "gdk/text/hb.h",
    "gdk/text/hb-ot.h",
}

RULES = [
    # ── GDK/GSK/GTK pango glue ───────────────────────────────────────────────
    (re.compile(r'\bgdk_pango_'), 'gdk_text_'),
    (re.compile(r'\bGdkPango'), 'GdkText'),
    (re.compile(r'\bgsk_pango_'), 'gsk_text_'),
    (re.compile(r'\bGskPango'), 'GskText'),
    (re.compile(r'\bgtk_pango_'), 'gtk_text_'),
    (re.compile(r'\bGtkPango'), 'GtkText'),
    # ── Pango API proper ─────────────────────────────────────────────────────
    (re.compile(r'\bPANGO_'), 'GDK_TEXT_'),
    (re.compile(r'\bPango'), 'GdkText'),
    (re.compile(r'\bpango_'), 'gdk_text_'),
]

COMPILED = [(rx, repl) for rx, repl in RULES]

def rename_text(text):
    for rx, repl in COMPILED:
        text = rx.sub(repl, text)
    return text

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('roots', nargs='*', default=['gdk','gsk','gtk'])
    args = ap.parse_args()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(here)
    changed = []
    for root in args.roots:
        for dirpath, _dirs, files in os.walk(root):
            for name in sorted(files):
                if not name.endswith(('.c','.h')):
                    continue
                path = os.path.normpath(os.path.join(dirpath, name))
                if path in SKIP:
                    continue
                with open(path, 'r', encoding='utf-8', errors='surrogateescape') as f:
                    orig = f.read()
                new = rename_text(orig)
                if new != orig:
                    changed.append(path)
                    if not args.check:
                        with open(path, 'w', encoding='utf-8', errors='surrogateescape') as f:
                            f.write(new)
    print(f'gtk4-rename-pango: {len(changed)} files rewritten')
    for p in changed[:20]:
        print('  '+p)
    if len(changed) > 20:
        print(f'  ... and {len(changed)-20} more')
    return 0

if __name__ == '__main__':
    sys.exit(main())
