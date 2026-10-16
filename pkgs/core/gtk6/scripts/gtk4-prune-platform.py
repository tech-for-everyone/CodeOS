#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Remove the platform backends GTK6 replaces with Qt6 (PLAN.md "Dependency
# surgery"): the macOS/Quartz, Windows, X11, Wayland, Broadway and Android
# widget files.  Their directory-level counterparts (gdk/x11, gtk/a11y, ...)
# are already excluded by scripts/import-gtk4.sh; this is the file-level half
# of the same prune.
#
# The list is the per-OS branches of gtk/meson.build `gtk_sources`:
#   os_win32        -> gtkapplication-win32.c, gtkcolorpickerwin32.c,
#                      gtkimcontextime.c, gtkfilechoosernativewin32.c,
#                      gtkwin32.c, gtkshowwin32.c
#   x11_enabled     -> gtkapplication-x11.c, gtkmountoperation-x11.c
#   wayland_enabled -> gtkimcontextwayland.c, gtkapplication-wayland.c
#   macos_enabled   -> gtksearchenginequartz.c, gtkapplication-quartz.c,
#                      gtkapplication-quartz-menu.c, gtkcolorpickerquartz.c,
#                      gtkfilechoosernativequartz.c, gtkimcontextquartz.c,
#                      gtkquartz.c, gtkwindowbuttonsquartz.c
#   android_enabled -> gtkapplication-android.c, gtkfilechoosernativeandroid.c,
#                      gtkimcontextandroid.c
#   broadway        -> gtkimcontextbroadway.c
#
# Idempotent: files that are already gone are skipped.
#
# Usage: scripts/gtk4-prune-platform.py [--check]

import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PLATFORM_C = [
    "gtk/gtkapplication-win32.c",
    "gtk/gtkcolorpickerwin32.c",
    "gtk/gtkimcontextime.c",
    "gtk/gtkfilechoosernativewin32.c",
    "gtk/gtkwin32.c",
    "gtk/gtkshowwin32.c",
    "gtk/gtkapplication-x11.c",
    "gtk/gtkmountoperation-x11.c",
    "gtk/gtkimcontextwayland.c",
    "gtk/gtkapplication-wayland.c",
    "gtk/gtksearchenginequartz.c",
    "gtk/gtkapplication-quartz.c",
    "gtk/gtkapplication-quartz-menu.c",
    "gtk/gtkcolorpickerquartz.c",
    "gtk/gtkfilechoosernativequartz.c",
    "gtk/gtkimcontextquartz.c",
    "gtk/gtkquartz.c",
    "gtk/gtkwindowbuttonsquartz.c",
    "gtk/gtkapplication-android.c",
    "gtk/gtkfilechoosernativeandroid.c",
    "gtk/gtkimcontextandroid.c",
    "gtk/gtkimcontextbroadway.c",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report files that would be removed")
    args = ap.parse_args()

    changed = []
    for rel in PLATFORM_C:
        path = os.path.join(ROOT, rel)
        if os.path.exists(path):
            changed.append(rel)
            if not args.check:
                os.remove(path)

    verb = "would remove" if args.check else "removed"
    print(f"gtk4-prune-platform: {len(changed)} files {verb}")
    for rel in changed:
        print("  " + rel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
