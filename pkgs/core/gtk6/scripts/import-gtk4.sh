#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Import the upstream GTK4 sources gtk6 keeps, minus the platform and
# rendering backends that Qt6 replaces (PLAN.md "Dependency surgery").
#
#   GTK4_SRC   upstream checkout; defaults to the CodeOS tree's gtk4/
#
# The three trees land as gtk6's own gtk/, gdk/ and gsk/:
#
#   gtk/   the widget layer (gtk/*.c, gtk/**)
#   gdk/   the GDK core, plus gtk6's gdk/qt (the Qt backend)
#   gsk/   the GSK core, plus gtk6's gsk/qt (the Qt renderer)
#
# The copy is additive and never touches gdk/qt, gsk/qt or qt/ -- those are
# gtk6's own code.  Upstream is the reference; the removals below are the only
# ones the plan's surgery table calls for at import time (phase 6 prunes
# further once the core links).
#
# Alongside the three trees it imports build-aux/meson/gen-visibility-macros.py:
# GTK's own generator for gdkversionmacros.h / gdk-visibility.h, which
# scripts/gen-gtk4-headers.py re-runs when it produces build/gen/.
#
# Usage:
#   scripts/import-gtk4.sh [--dry-run] [--diff]
#
#   --dry-run  list what would be copied, change nothing
#   --diff     report files that are in upstream but absent from the import
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Repo root is three levels up from pkgs/core/gtk6.
GTK4_SRC="${GTK4_SRC:-$HERE/../../../gtk4}"

# ── What the plan's surgery table deletes ────────────────────────────────
# Backends per platform.  Qt is the only backend gtk6 keeps.
PLATFORM_EXCLUDES=(
    --exclude='gdk/x11'
    --exclude='gdk/wayland'
    --exclude='gdk/broadway'
    --exclude='gdk/macos'
    --exclude='gdk/win32'
    --exclude='gdk/android'
    --exclude='gdk/deprecated'
    --exclude='gdk/loaders'          # gdk-pixbuf loaders: QtImage instead
    --exclude='gsk/gl'
    --exclude='gsk/gpu'
    --exclude='gsk/broadway'
    --exclude='gtk/a11y'
    --exclude='gtk/deprecated'
    --exclude='gtk/inspector'
    --exclude='gtk/print'
    --exclude='gtk/media'
)

DRY_RUN=0
DIFF=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        --diff)    DIFF=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

if [[ ! -d "$GTK4_SRC/gdk" || ! -d "$GTK4_SRC/gsk" || ! -d "$GTK4_SRC/gtk" ]]; then
    echo "import-gtk4: '$GTK4_SRC' does not look like a GTK4 checkout" >&2
    echo "             set GTK4_SRC=<path> or place one at the repo root." >&2
    exit 1
fi

if [[ "$DRY_RUN" == 1 ]]; then
    echo "==> dry run: $GTK4_SRC -> $HERE"
    (cd "$GTK4_SRC" && tar cf - "${PLATFORM_EXCLUDES[@]}" gdk gsk gtk \
            build-aux/meson/gen-visibility-macros.py) \
        | tar tf - | sort
    exit 0
fi

if [[ "$DIFF" == 1 ]]; then
    (cd "$GTK4_SRC" && tar cf - "${PLATFORM_EXCLUDES[@]}" gdk gsk gtk \
            build-aux/meson/gen-visibility-macros.py) \
        | tar tf - | sed 's:/$::' | grep -v '/$' | sort -u \
        | while read -r f; do
              [ -e "$HERE/$f" ] || printf 'missing %s\n' "$f"
          done
    exit 0
fi

echo "==> importing GTK4 from $GTK4_SRC"
echo "==> pruned backends:"
printf '     %s\n' "${PLATFORM_EXCLUDES[@]}" | sed 's/--exclude=//'

# tar over the three trees; gdk/qt and gsk/qt are not in upstream, so the
# existing Qt backends survive untouched.  build-aux/meson/ is GTK's own
# generated-header tooling (gen-visibility-macros.py).
(cd "$GTK4_SRC" && tar cf - "${PLATFORM_EXCLUDES[@]}" gdk gsk gtk \
        build-aux/meson/gen-visibility-macros.py) \
    | (cd "$HERE" && tar xf -)

echo "==> imported:"
du -sh "$HERE/gdk" "$HERE/gsk" "$HERE/gtk" 2>/dev/null
