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

- `gtk6/` — the combined toolkit (this directory).
  - `gtk/` — GTK4 widget layer, pruned.
  - `gsk/` — GSK core (render nodes) + new `qt` renderer.
  - `gdk/` — GDK core + new `qt` backend.
  - `qt/` — the Qt6-backed shims: `qgdk*.cpp`, `qgsk*.cpp`, `qgtext*.cpp`,
    `qgimage*.cpp`.
  - `subprojects/` — `glib`, `graphene` sources (or wraps).
  - `Makefile` — freestanding build mirroring `qt6/panels/Makefile`.
- Upstream GTK4 stays pristine at `../gtk4/` as the reference / patch base.

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

`./scripts/build-glib.sh` configures glib 2.90.0 with
`codeos-glib.cross` and produces, cleanly and reproducibly:

    build/glib/glib/libglib-2.0.a        (~12 MB,   1000+ T symbols)
    build/glib/gobject/libgobject-2.0.a  (~6 MB,     GObject type system)
    build/glib/gio/libgio-2.0.a          (~30 MB)
    build/glib/gmodule/libgmodule-2.0.a
    build/glib/gthread/libgthread-2.0.a

What had to be bridged to get there (all in `deps/`, all weak so CodeOS can
override later):

| Gap | Resolution |
|---|---|
| meson needs `dependency('iconv')` | `deps/src/iconv_stub.c` (UTF-8 passthrough) + `libiconv.a`, linked in |
| gnulib hard-requires linking `frexp`/`frexpl` | `deps/src/math_shim.c` (IEEE-754 bit-twiddling, no libm recursion) |
| gio hard-requires `res_query` + `socket()` family | `deps/src/net_stub.c` |
| glib `gmem.c` hard-errors without an aligned allocator | `deps/src/alloc_stub.c` (`posix_memalign`/`aligned_alloc`/`memalign` → malloc; matches `pkgs/core/openssl/src/ossl_shim.c`) |
| gio picks `USE_STATFS`/`USE_STATVFS` from `statfs()` | `deps/src/fs_stub.c` |
| `x86_64-elf-gcc` rejects `-pthread` (glib's `threads` dep) | `deps/bin/cc`, `deps/bin/c++` filter it out |
| Compiler doesn't define `__linux__` but meson host is linux | `-D__linux__` in the cross file |

Deliberately **not** built: glib's installed CLI tools (`gtester`,
`gobject-query`, `gdbus`, `gsettings`, …). They are host programs and link the
**host** `libpcre2-8.so`/`libffi.so`, which is meaningless for the freestanding
target. Code generation uses the host's `glib-compile-resources`/
`glib-compile-schemas`, and the Python `glib-genmarshal`/`glib-mkenums` meson
generates into `build/glib/gobject/`.

### Remaining substrate work

1. **Target pcre2 + libffi + zlib.** meson currently resolves these against the
   *host* system (`libpcre2-8.so`, `libffi.so`, `zlib`). gio/gobject reference
   their symbols, so the final link needs target builds: reuse Qt6's
   `libQt6BundledPcre2.a`/`libQt6BundledZLIB.a` and build libffi from glib's
   `libffi.wrap`, or `--wrap-mode=forcefallback`.
2. **Install glib into a `gtk6/sysroot`.** GTK's build needs
   `glib-2.0.pc`/`gobject-2.0.pc`/`gio-2.0.pc` + headers. Add an install step that
   stages headers/`.pc` files so meson can find glib as a target dependency.
3. **graphene** — small, portable; build it against the staged glib and add
   `graphene-gobject-1.0.pc`.

Then Phases 2–6 (`gdk/qt`, `gsk/qt`, text bridge, widgets, prune) can start.
