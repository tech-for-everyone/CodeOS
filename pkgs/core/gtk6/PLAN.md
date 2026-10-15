<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# GTK6 — GTK4 widgets on the Qt6 engine

Goal: keep GTK4's widget layer / public API (the *usefulness*) and replace the
platform-coupled subsystems with Qt6 (the *bendability*), so the toolkit builds
freestanding for CodeOS / Zircon next to `qt6/`.

This is not "link GTK4 and Qt6 together" (two object systems, two event loops,
two renderers). It is "GTK4's design + API, Qt6's engine".

## What Qt6 already gives us (verified in this tree)

- Qt **6.11.1**, static, freestanding (`-ffreestanding -mcmodel=large -mno-sse`,
  linked into the kernel; see `qt6/panels/Makefile`).
- `libQt6Core.a` (191M), `libQt6Gui.a` (328M), `libQt6Widgets.a` (294M).
- **Qt6Gui bundles freetype + harfbuzz** → font loading, shaping, rasterisation.
- **Qt6Core bundles pcre2** (`libQt6BundledPcre2.a`) → regex.
- Available: QtCore, QtGui, QtWidgets, QtConcurrent, QtFbSupport,
  QtDeviceDiscoverySupport, bundled pcre2/zlib. **No** QtNetwork/Sql/PrintSupport
  (`QT_NO_PRINTER`, `QT_NO_OPENGL` are already set).

So Qt6 covers everything pango + harfbuzz + freetype + fontconfig + cairo +
gdk-pixbuf do for us, plus the windowing/event/input backend via QtWidgets.

## Dependency surgery

| GTK4 subsystem | size | action | replaced by |
|---|---|---|---|
| `gdk/{x11,wayland,broadway,macos,win32,android}` | 167k LOC | **delete** | new `gdk/qt` (GdkDisplay/GdkSurface/GdkDevice on `QWidget`) |
| `gsk/{gl,gpu,broadway}` | 72k LOC | **delete** | new `gsk/qt` (`GskRenderer` drawing `GskRenderNode`s with `QPainter`) |
| `pango`, `harfbuzz`, `fribidi`, `fontconfig`, `freetype` | — | drop dep | Qt6 `QFont`/`QTextLayout`/`QFontMetrics` |
| `cairo`, `cairo-gobject` | 31 files in `gtk/` | drop (`QT_NO_PRINTER`) | `QPainter` |
| `gdk-pixbuf` | 3 files in `gtk/` | drop | `QImage` |
| `graphene` | small | **keep** | portable pure-math C |
| `glib` / `gobject` / `gio` | 206 + 86 files in `gtk/` | **CRUX — see below** | port, or shim on Qt |
| `gstreamer`, `tracker`, `colord`, `accesskit`, `cups`/`cpdb`, `vulkan`, `libdrm`, `cloudproviders`, `iso-codes` | — | delete | n/a |
| `gtk/{print,deprecated,a11y}`, `gtk/svg` | — | drop in phase 1 | n/a |

The widget layer itself barely touches the dropped libs, so the removals are
mostly deleting whole directories and adding two new backends (`gdk/qt`,
`gsk/qt`) plus one text bridge.

## Where the code lives

`pkgs/core/gtk6/` is a real CodeOS package (`manifest.json`, per-package
`Makefile`, GPL-3.0-or-later).  glib lives **inside** the package.

- `pkgs/core/gtk6/` — the combined toolkit (this directory).
  - `gtk/` — GTK4 widget layer, pruned.
  - `gsk/` — GSK core (render nodes) + new `qt` renderer.
  - `gdk/` — GDK core + new `qt` backend.
  - `qt/` — the Qt6-backed shims: `qgdk*.cpp`, `qgsk*.cpp`, `qgtext*.cpp`,
    `qgimage*.cpp`.
  - `subprojects/glib/` — glib 2.90 source (built by `scripts/build-glib.sh`).
  - `subprojects/graphene/` — graphene source.
  - `toolchain/` — `cc`/`c++` driver wrappers (they drop the `-pthread` flag
    meson's `threads` dependency injects, which bare-metal GCC rejects).
  - `codeos-glib.cross.in` — glib cross-file template; `build-glib.sh`
    substitutes the two checkout paths into `build/codeos-glib.cross`.
  - `Makefile` — freestanding build mirroring `qt6/panels/Makefile`.
- Top-level `make gtk6` / `gtk6-glib` / `gtk6-all` / `gtk6-check` build and
  check the package.
- Upstream GTK4 stays pristine at `gtk4/` (repo root) as the reference / patch
  base.

The CodeOS-native libc functions glib needs are part of **CodeOS**, not the
package: `qt6/posixstubs/src/codeos_compat.c` (in `libcodeos_posixstubs`) now
provides `iconv`, `posix_memalign`/`aligned_alloc`/`memalign`, `frexp`/`ldexp`,
`statfs`/`statvfs` and the resolver/socket family.  The package carries no
private libc.

## Phases

0. **Scaffold** — `gtk6/` tree + freestanding build harness + a hello-widget that
   proves Qt6 objects link into a GTK4-shaped shell.
1. **Substrate** — build `glib`/`gobject`/`gio` (minimal) and `graphene` for the
   CodeOS freestanding target.
2. **`gdk/qt`** — display, surfaces, devices, input, clipboard, cursors on
   QtWidgets.
3. **`gsk/qt`** — software renderer over `QPainter`; text nodes via `QTextLayout`.
4. **Text bridge** — `gtkpangoprivate` / `gskpangoprivate` → Qt text.
5. **Widget bring-up** — CSS, layout managers, core widgets, then the rest.
6. **Prune** — delete print/deprecated/a11y/gstreamer paths once the core links.

## glib strategy — DECIDED

**(A) Port real glib.** glib is the *portable* part; pango/cairo/gdk were the
hard part, and Qt6 already replaces those. Base revision stays GTK `main`
(4.25); glib is pinned to **2.90.0** and graphene to **`f89522c`**, exactly the
revisions GTK4's `subprojects/*.wrap` files request.

The alternative (a Qt-backed glib/gobject shim over `QString`/`QList`/`QObject`)
was rejected: it is more work than porting glib and must re-implement GObject's
property/signal semantics.

## Progress

### Phase 1 — substrate (glib): DONE (libraries build)

`./scripts/build-glib.sh` configures glib 2.90.0 with the generated
`build/codeos-glib.cross` and produces, cleanly and reproducibly:

    build/glib/glib/libglib-2.0.a        (~7 MB,    glib core + handlers)
    build/glib/gobject/libgobject-2.0.a  (~2.3 MB,  GObject type system)
    build/glib/gio/libgio-2.0.a          (~16 MB)
    build/glib/gmodule/libgmodule-2.0.a
    build/glib/gthread/libgthread-2.0.a

The functions glib needs that a minimal freestanding libc does not have are
**part of CodeOS now**, not a package-local shim.  `qt6/posixstubs/src/
codeos_compat.c` adds them to `libcodeos_posixstubs.a`, so every consumer gets
them and gtk6 links no private libc:

| Gap | Resolution (in CodeOS) |
|---|---|
| meson needs `dependency('iconv')` | real `iconv_open`/`iconv`/`iconv_close` (UTF-8/UTF-16/UTF-32/Latin-1/ASCII) |
| gnulib hard-requires linking `frexp`/`frexpl` | `frexp`/`ldexp` via IEEE-754 bit twiddling (no libm recursion) |
| gio hard-requires `res_query` + `socket()` family | weak resolver/socket stubs (no userspace network stack yet) |
| glib `gmem.c` hard-errors without an aligned allocator | `posix_memalign`/`aligned_alloc`/`memalign` → malloc |
| gio picks `USE_STATFS`/`USE_STATVFS` from `statfs()` | weak `statfs`/`statvfs` stubs |
| `x86_64-elf-gcc` rejects `-pthread` (glib's `threads` dep) | `toolchain/cc`, `toolchain/c++` filter it out |
| Compiler doesn't define `__linux__` but meson host is linux | `-D__linux__` in the cross-file template |

The network/statfs entry points are `weak` on purpose: a future real
implementation linked from the kernel or a NIC package overrides them without
touching `codeos_compat.c`.

Deliberately **not** built: glib's installed CLI tools (`gtester`,
`gobject-query`, `gdbus`, `gsettings`, …). They are host programs and link the
**host** `libpcre2-8.so`/`libffi.so`, which is meaningless for the freestanding
target. Code generation uses the host's `glib-compile-resources`/
`glib-compile-schemas`, and the Python `glib-genmarshal`/`glib-mkenums` meson
generates into `build/glib/gobject/`.

### Remaining substrate work

1. ~~**Target pcre2 + libffi + zlib.**~~ **DONE.** `build-glib.sh` passes
   `--wrap-mode=forcefallback` and builds the three bundled subprojects for the
   CodeOS target alongside glib:
   `subprojects/pcre2-10.46/libpcre2-8.a`,
   `subprojects/libffi-3.5.2/src/libffi.a`, `subprojects/zlib-1.3.1/libz.a`.
   Nothing is taken from the host.
2. **Stage glib into `gtk6/sysroot/`.** DONE for headers and libs:
   `sysroot/usr/include/glib/` (128 headers) and
   `sysroot/usr/lib/lib{glib,gobject,gio,gmodule,gthread}-2.0.a` are staged, and
   `build/glib/glib/glibconfig.h` is on the include path. Still wanted:
   `glib-2.0.pc`/`gobject-2.0.pc`/`gio-2.0.pc` so a consumer can ask for glib as
   a *target* dependency, and a script that reproduces the staging (today the
   copy is manual).
3. **graphene** — DONE: `sysroot/usr/lib/libgraphene-1.0.a`,
   `sysroot/usr/include/graphene-1.0/` and
   `sysroot/usr/lib/pkgconfig/graphene-1.0.pc` are staged.
   `graphene-gobject-1.0.pc` is still wanted.

Then Phases 2–6 (`gdk/qt`, `gsk/qt`, text bridge, widgets, prune) can start.

### Phases 2–4 — the Qt backends exist and build

`make -C pkgs/core/gtk6 check` builds `build/libgtk6qt.a` from three real (not integer)
modules and compiles `tests/linkcheck.cpp` against their headers:

    gdk/qt/gdkqt.cpp   Phase 2  GdkQtDisplay / GdkQtSurface on QRasterWindow
    gsk/qt/gskqt.cpp   Phase 3  GskQtRenderer: QPainter software renderer
    qt/qttext.cpp      Phase 4  GtkQtFont / GtkQtText on QTextLayout

* **Phase 4 (text).** `qt/qttext.h` is the Pango stand-in: create a font, set
  UTF-8, set a wrap width, ask for size/ascent/line count, extract shaped glyphs
  (`QTextLine::glyphRuns`), and draw. Shaping, bidi and line breaking are
  Qt's — the module names no Pango/HarfBuzz/FreeType header, which is the
  surgery the plan calls for.
* **Phase 3 (render).** `gsk/qt` executes the three GSK jobs (clip, transform,
  paint) as `QPainter` calls. `fill_rect`/`stroke_rect`/`draw_text` cover the
  node set the widget bring-up starts with; gradients and image nodes come
  next.
* **Phase 2 (display/surface).** A toplevel is a `QRasterWindow`; the paint
  callback is where the caller runs the GSK renderer, so raster-on-framebuffer
  is the whole pipeline. No X11/Wayland/Broadway/Win32/macOS.
* The modules deliberately do **not** include `<glib.h>`: the library is built
  with `-nostdinc` against the CodeOS `stubs`, and glib's own headers need libc
  headers the freestanding target does not have. Plain
  `<stdint.h>`/`int`/`double` is the ABI.

Not yet wired up:

* The GTK4 sources are imported and pruned (see Phases 5–6 below) but not on
  the build: `gtk/*.c`, `gdk/*.c` and `gsk/*.c` still want the Qt-backed
  `gdk/qt` / `gsk/qt` / `qt` modules behind `G_DEFINE_TYPE`d `GdkQt*`/`GskQt*`
  backends, plus the removed libs' replacements.
* glib 2.90's pcre2/libffi/zlib now build for the CodeOS target from the forced
  subprojects (see "Remaining substrate work"); they are not yet linked into
  the kernel image.
* `sysroot/` is staged but not tracked; see ".gitignore".

### Phases 5–6 — GTK4 imported and pruned, not yet built

`scripts/import-gtk4.sh` vendors upstream GTK4 into the package, from the CodeOS
tree's `gtk4/` by default:

    gtk/   28M   the widget layer (641 .c)
    gdk/  3.2M   GDK core + gtk6's own gdk/qt
    gdk/gsk/qt survives because it is not in upstream
    gsk/  2.2M   GSK core + gtk6's own gsk/qt

Pruned at import (the "Dependency surgery" table):

    gdk/{x11,wayland,broadway,macos,win32,android,deprecated,loaders}
    gsk/{gl,gpu,broadway}
    gtk/{a11y,inspector,print,media,deprecated}

The import is additive — upstream files land next to gtk6's own `gdk/qt`,
`gsk/qt` and `qt/`, which have no upstream counterpart.

The port itself is not done. What it needs:

* **Generated headers do not exist yet.** Upstream produces `gdkconfig.h`,
  `gdk/gdkenumtypes.[ch]`, `gtk/gtktypebuiltins.[ch]`,
  `gtk/gtkprivatetypebuiltins.[ch]`, `gsk/gskenumtypes.[ch]`, the
  `*.c`/`*.h` from `*.template` (the `.template` files are imported), and the
  gleaned enums/marshalers/resources with the host's `glib-mkenums`/
  `glib-genmarshal`/`glib-compile-resources`. Nothing in the import compiles
  without them, so this is the first build task.
* **The dropped libraries are woven through the kept files**, so removing them
  is per-file surgery rather than a directory delete: every `gdk/*.c` includes
  `<cairo-gobject.h>`, and `gtk/*.c` uses pango (text), cairo (drawing) and
  gdk-pixbuf (images). The replacements are already prototyped
  (`qt/qttext`, `gsk/qt`, `gdk/qt`) and get wired in during the bring-up.
* **Upstream's `meson.build` files are imported as reference** but not used;
  gtk6's freestanding `Makefile` owns the build.
* **Re-licensing is deferred.** The imported headers still carry upstream's
  `LGPL-2.1-or-later`; the glib tree was swept to `GPL-3.0-or-later` and the
  GTK4 trees should be swept the same way once the file list settles.

`make -C pkgs/core/gtk6 check` still builds only the three Qt modules.

