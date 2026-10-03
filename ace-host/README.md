# ace-host — building OpenHarmony's ArkUI off OpenHarmony

A host build of `arkui_ace_engine`, with the goal of combining it with GNUstep
into one app runtime that can eventually run on CodeOS. This directory is the
first step and it is deliberately small: it answers *whether the port is
possible at all* with numbers rather than opinion.

Run it:

```sh
cd ace-host
cmake -S . -B build && cmake --build build -j"$(nproc)"   # GREEN
./build/ace_layout_probe                                  # foundation: 20 assertions, exit 0
./build/ace_ng_layout_probe                               # layout: 20 assertions, exit 0
./build/ace_tree_probe                                    # layout a tree + rasterise -> tree_layout.ppm
./build/ace_paint_probe                                   # engine paint code -> its own draw command + raster
./build/ace_window_demo --size 800x600                    # live X11 window; resize re-runs the engine layout
python3 controls.py                                       # 23 negative controls, exit 0
python3 classify.py <dir>                                 # compile-and-bucket any subtree
```

`ace_window_demo` needs an X display; under a headless host use
`xvfb-run -a ./build/ace_window_demo --frames 1 --screenshot out.ppm` to present
once and exit. It is only built when X11 is found, so the rest of the build is
unaffected on a host without it.

## Why a host build first, and why this is not the OpenHarmony build

`arkui_ace_engine` ships only a GN build. `BUILD.gn` imports `//build/ohos.gni` —
the OpenHarmony build system — which is not in the ace_engine repo, and the engine
also wants `//third_party/skia`. So **as it stands there is no way to build this
tree on any machine** without first standing up the whole OpenHarmony build.

Before doing that, it is worth knowing how much of the engine actually depends on
OpenHarmony. That question is answerable cheaply, and this directory answers it.

## What compiles today

| layer | compiles | links+runs | notes |
|---|---|---|---|
| `frameworks/base` (foundation) | **57 / 63** | **yes** | the 6 excluded need real third-party code |
| `components_ng` layout core (`base`, `property`, `linear_layout`) | **51 / 51** | **yes** | renderer-free via `ACE_UNITTEST` |

**Nothing under `arkui/` is modified** — a fresh clone reproduces this build.

The 6 excluded foundation files are excluded by name in `CMakeLists.txt`, each
because it needs genuine third-party behaviour rather than a compiler flag:
`cJSON` (a JSON library), `chnsecal` (a lunar calendar), an OHOS HTTP client, and
Rosen/Skia (the rasteriser).

## The finding that matters most: the renderer is already a swap

`frameworks/core/components_ng/render/drawing.h` is **not** an abstraction — it
hard-includes `drawing/engine_adapter/skia_adapter/skia_canvas.h` and Rosen. But it
is wrapped in `#ifndef ACE_UNITTEST`, and the else-branch includes
`drawing_mock.h`, which pulls in `test/mock/frameworks/core/rosen/testing_*.h`:

- **46 mock-renderer headers, 3.8 MB, shipped inside the ace_engine repo.**

So the engine already builds with no rasteriser at all for its own unit tests, and
`-DACE_UNITTEST` selects that path. This is the seam a port needs, and it changes
the plan: **Skia is not required to run the layout engine or to compile the
engine's renderer interface** — only a real rasteriser needs it, and that becomes
a swappable backend choice rather than a prerequisite.

`controls.py` C5 proves the seam is load-bearing: `view_abstract.cpp` compiles with
`-DACE_UNITTEST` and fails on `draw/canvas.h` without it.

## The second finding: the OSAL is the platform seam

Linking *any* non-trivial engine code needs three files, each the sole definition
of a symbol the tree calls, each a platform seam rather than engine logic:

| file | why |
|---|---|
| `adapter/preview/osal/system_properties.cpp` | the host `SystemProperties`; the `ohos` one hard-codes OHOS flags |
| `adapter/ohos/osal/log_wrapper.cpp` | the **only** log backend in the tree — there is no preview equivalent |
| `frameworks/core/pipeline/pipeline_base.cpp` | `PipelineBase::GetCurrentContextSafely`, called by `Dimension::NormalizeToPx` |

Deliberately *not* globbing all of `adapter/ohos` or `adapter/preview`: those are
full platform ports that want Skia and the OHOS IPC stack.

## Compiling ≠ linking, and the seam that closed it

The layout core **compiles** 51/51, but its probe did not **link**, for a specific
reason worth recording:

```cpp
// frameworks/core/components_ng/property/layout_constraint.h:54
ScaleProperty scaleProperty = ScaleProperty::CreateScaleProperty();      // default member init

// frameworks/core/components_ng/property/calc_length.cpp:26
auto pipeline = context ? AceType::Claim(context) : PipelineBase::GetCurrentContextSafely();
CHECK_NULL_RETURN(pipeline, scaleProperty);                             // default 1.0 scale
```

On a host with no container that call is **null-safe** and yields the default
scale — but the reference is still emitted, so the linker required `PipelineBase`,
which required `Container`, `AceEngine`, `Kit::UIContext`, `JsonUtil` (→ cJSON) and
the trace machinery. A naive closure grew 517 → 1016 → 1289 undefined symbols and
kept discovering Skia and the OHOS IPC stack. **Constructing a layout constraint
is not purely functional even though measuring with one is.**

The closure is not needed, because the real function's *only* use of
`PipelineBase` is to read the current density, and with no container present it
returns the **default** scale. ace_engine already ships that exact seam for its
own unit tests:

```cpp
// test/mock/frameworks/core/components_ng/property/mock_calc_length.cpp
ScaleProperty ScaleProperty::CreateScaleProperty(PipelineBase* context) {
    (void)context;
    return ScaleProperty();          // the same value the real path returns
}
```

So the probe compiles **upstream's mock in place of the real `calc_length.cpp`**,
selecting it the same way the engine's tests do. This is not a homemade stub: it
is a real definition of the exact symbol, and it is the value the real code
produces when there is no container. The result is that the whole closure stays
inside the layout layer — no PipelineBase, no Skia, no OHOS IPC, no cJSON — and
`ace_ng_layout_probe` **links and passes** (Stage 1b). Control C6 proves the swap
is load-bearing: the real TU references `PipelineBase` (so it could not link
standalone), the mock does not.

The service core still has to be faced to run a real `FrameNode` tree, but it is
no longer in the way of measuring layout geometry. Stage 2 splits in two:
**2a, lay a tree out** (done) and **2b, let the engine paint** — the engine's own
paint code now runs off-pipeline and emits a draw command we capture and raster
(done, below); painting a whole `FrameNode` tree still needs a render backend
(open).

## Stage 2a: the real algorithm over a tree

`ace_tree_probe` builds a `Column`-rooted tree

```
root(Column) -> a, b, row(Row) -> {c, d}, e
```

out of a `LayoutWrapper` **test double**, runs the engine's
`LinearLayoutUtils::Measure` / `Layout` on it, asserts the resulting
`GeometryNode` rectangles, and rasterises them with a trivial software renderer
to `tree_layout.ppm`. All geometry assertions pass, e.g. `b` sits at `y = 50`
(the height of `a`), `row` at `y = 100` and measures `160x60` from its children,
`c`/`d` sit at `x = 0`/`80` inside it, `e` at `y = 160`.

* **Real:** the algorithm (`linear_layout_utils.cpp`), `LayoutProperty`
  (`layout/layout_property.cpp`), `GeometryNode`, `CalcLength::NormalizeToPx`.
* **Double:** the `LayoutWrapper`. Upstream backs it with a `FrameNode`, and a
  `FrameNode` drags in `Pattern` + `RenderContext` + `PipelineContext` —
  measured, one node is **1226 undefined references**. A tree probe does not need
  a `FrameNode`, so it implements the abstract interface directly and delegates
  straight back into `LinearLayoutUtils`. No layout arithmetic is reimplemented;
  control C7a checks the engine's `Measure`/`Layout` are the ones linked.
* **Ours:** the rasteriser. The engine's paint path is the Skia/Rosen backend,
  absent here, so the probe draws the geometry itself. The claim is about
  *geometry*; the pixels are a rendering of the engine's layout output, not the
  engine's paint output. Control C7c samples the PPM at six points and checks
  each against the computed rectangle.

The closure that run needs is **5 engine TUs** (`inspector_constants` for the
`V2::*_ETS_TAG` strings the property vtable references, `alignment`,
`layout_property`, `layout_algorithm`, the real `calc_length`) plus upstream's
mocks for the services that are not here (JSON, the application-info singleton,
the log sink, the `LayoutWrapper` out-of-line methods), plus `tree_seams.cpp`.
That last file is the *"no container / no pipeline exists"* answer for the
unreachable paths the property vtable and safe-area code reference; it defines no
layout arithmetic, and the geometry assertions would fail if it did.

`-ffunction-sections` + `--gc-sections` are what keep this from becoming the
1226-symbol service core: they drop the unreachable functions instead of linking
them. Before that, the same tree run failed to link with 758 references; after,
it closes. (A `FrameNode` path cannot use the same trick as cheaply — its
constructor is one function that itself references the render/task core.)

## Stage 2b: the engine's own paint code, driven off-pipeline

Stage 2a rasterised geometry the engine's *layout* produced. Stage 2b asks the
other half: what does the engine's *paint* code draw, and can that be observed
without Skia?

`ace_paint_probe` builds a real `DividerModifier`, hands it a `DrawingContext`
around a `RecordingCanvas`, and calls `ContentModifier::Draw`
(`components_ng/base/modifier.cpp`), which calls `DividerModifier::onDraw`, which
builds a `DividerPainter` and calls `DividerPainter::DrawLine`
(`components_ng/render/divider_painter.cpp`). That function computes the line's
endpoints from the stroke width, line-cap style, orientation and length — engine
arithmetic — and emits `canvas.DrawLine`. The probe records those endpoints and
asserts them: a SQUARE cap insets by `width/2`, a BUTT cap does not, vertical swaps
the axes, and `DividerModifier` coerces BUTT to SQUARE while
`strokeWidthLimitation_` is on (its default). It then rasterises the captured
segment.

* **Real:** the paint logic — `DividerModifier`, `DividerPainter`, and every
  endpoint. Control C9a checks that the binary links
  `DividerPainter::DrawLine`, so the arithmetic under test is the engine's.
* **Ours:** the rasteriser, and the colour. The engine's pen is a stateless mock
  (`TestingPen::SetColor`/`SetWidth` are no-ops), so the canvas observes *geometry*
  but not *style*; the probe supplies the colour it configured.
* **Recovered:** the draw command. That is the seam a host backend plugs into:
  override the canvas, take the engine's draw list, rasterise it yourself.

This is one painter, not the whole tree. Background/border/foreground are Rosen
`RSNode` property setters rather than canvas draw calls, so painting a real
`FrameNode` tree still needs a render context or a software backend — the closure
measured next. The same capture is wired into `ace_window_demo`, where one node is
painted by a real `DividerModifier` from its engine-computed rectangle (Stage 3a,
control C8c).

### Why painting the whole tree is still open

Constructing one real `FrameNode` (`FrameNode::CreateFrameNode` + a `Pattern`)
and linking it against `libace_layout.a` produces **1226 undefined references**,
and `--gc-sections` only brings that to 758. The first ones are already the
service core, not layout:

```
VTT / vtable for OHOS::Ace::NG::Pattern        -> pattern.cpp (the whole pattern layer)
OHOS::Ace::NG::RenderContext::Create()         -> render/adapter (Skia/Rosen)
OHOS::Ace::PreMakeScope::IsPreMake()           -> base/premake (build-time globals)
OHOS::Ace::NG::LayoutProperty::SetHost(...)    -> the property layer's C++ out-of-line members
OHOS::Ace::MultiThreadBuildManager::CheckTag() -> base/multi_thread (task manager)
```

So letting the *engine* paint is the engine's real tree + render + task/service
core, which is where Skia and the OHOS IPC/resource stack enter. The environment
this was measured in has **no `gn`, no Skia, and no OHOS SDK** (checked:
`which gn` empty, no `libskia*` on disk). ace_engine's own standalone rendering
path, the DevEco previewer
(`adapter/preview/entrance/samples/ace_phone_test.cpp`), additionally needs
`//ide/tools/previewer` and `//foundation/window/window_manager`, a JS/ETS bundle
and Skia — none of which are in this repo. So "run the engine's own paint" is a
fetch of major third-party trees, not a configuration change; Stage 2a gets the
*geometry* into a window without it.

## Stage 3, host step: a live window that relayouts

`ace_window_demo` is the same real algorithm and the same `LayoutWrapper` double as
the tree probe, but the output is a running X11 window instead of a file:

```
window size change (ConfigureNotify)
      -> LayoutScene(root, w, h)          # the engine's LinearLayoutUtils
      -> Flatten(root)                    # absolute rectangles
      -> Surface + XPutImage              # ours
```

Its scene is built to make the loop visible: the column centres its children, and
one node is measured at **half the width the engine hands down**. So resizing the
window runs a genuinely new layout, and the pixels change with it. Control C8b
checks that by counting the half-width node's pixels at two window widths: the
count is `(W/2 - 2) * 28` at both, which a fixed picture cannot satisfy, and the
demo links `LinearLayoutUtils::Measure`/`Layout` (C8a), so the geometry is the
engine's.

The window also shows Stage 2b live. One node (`div`) is not filled by the raster:
`Present` drives a real `DividerModifier` from that node's engine-computed
rectangle, captures the `DrawLine` it emits, and rasterises that. So the divider's
pixels are the engine's own draw command following the engine's own layout. Control
C8c counts them: the length is `0.8*W`, stroke 4, so the count is `4 * 0.8*W` at
both sizes, and C8a checks `DividerPainter::DrawLine` is linked.

This is the host visual runtime: **layout is ArkUI's, the divider's paint is
ArkUI's, pixels are ours, and the size comes from the window**. It is not yet the
GNUstep shell (Stage 3 proper), and not yet a whole `FrameNode` tree painted by the
engine (Stage 2c) -- those stay the next steps.

## Two measurements that changed the shape of the work

**1. Use the compiler the project uses.** `frameworks/base` gets 32/63 files clean
under g++ 16 and **47/63 under clang 22** *before any workaround*. The gap is not
code — GCC 16 rejects an anonymous-union member with a constructor at
`ng/rect_t.h:543` that clang accepts. Measuring with g++ would have booked a
compiler choice as porting cost.

**2. Count root causes, not files.** 17 apparent failures in `frameworks/base`
under g++ collapse to **2 header bugs**; 7 "portability" failures collapse to
**5 missing standard includes** (`<algorithm>` in `matrix3.h`, `<cstdint>` ×4).
One header fix clears sixteen files — counting by file overstated the work by an
order of magnitude.

## How the missing pieces are handled, and why not by patching

The five missing-standard-include bugs are real upstream bugs and the right fix is
to add the include at each site. But patching a vendored clone makes the build
unreproducible from a fresh checkout, so they are covered instead by
`-include compat/ace_compat.hpp`, a forced include listing every site in a comment.
`controls.py` C1 proves that header is load-bearing.

**The test for whether a shim may be a no-op is whether any caller observes the
side effect.** That gives a deliberate asymmetry:

| shim | kind | why |
|---|---|---|
| `hilog/log.h` | no-op | a log line is unobserved; no caller's state depends on it |
| `securec.h` | **implemented** | the checked `memcpy_s`/`sprintf_s` variants ARE a safety property; a stub that ignored its size arguments would compile, run, and destroy it |
| `refbase.h` | stub with a stated limit | only `sptr<IRemoteObject>` in a callback signature; backed by `shared_ptr`, which does **not** share a refcount with OHOS `RefBase`, so it must not cross the OHOS boundary |

`securec.h` covers the three functions `frameworks/base` calls; anything else is a
**link error, not a silent no-op**.

## Negative controls (23/23)

An assertion never seen to fail is not an assertion, and a shim the compiler
optimises away is not a shim. `controls.py` removes one ingredient at a time:

| control | removes | requires |
|---|---|---|
| C1a / C1b | the forced-include compat header | `matrix3.cpp` fails on `std::for_each` |
| C2a / C2b | the securec shim | `ace_trace.cpp` fails on `securec.h` |
| C3a / C3b | `-Wno-c++11-narrowing` | `dimension.cpp` fails on narrowing |
| C4a / C4b | the probe's pass/fail machinery | the mutant probe exits non-zero |
| C5a / C5b | `-DACE_UNITTEST` | `view_abstract.cpp` fails on `draw/canvas.h` |
| C6a / C6b | the mock `calc_length` link seam | the real TU references `PipelineBase`; the mock defines the symbol and does not |
| C7a / C7b | the tree probe's dependence on the real algorithm | the binary links `LinearLayoutUtils::Measure`/`Layout`; the probe exits 0 |
| C7c / C7d | the raster's tie to the layout | six sampled pixels match the computed rectangles; the tree mutant exits non-zero |
| C8a / C8b | the live window's dependence on the real layout | the binary links `LinearLayoutUtils` and `DividerPainter::DrawLine`; a 0.5-width node's pixel count is `W/2-2` wide at two different window widths |
| C8c | the live engine paint's dependence on the engine's layout | the engine-painted divider's pixel count is `4 * 0.8*W` at two different window widths |
| C9a / C9b | the paint probe's dependence on the engine's painter | the binary links `DividerPainter::DrawLine`; the probe exits 0 |
| C9c / C9d | the raster's tie to the engine's draw command | eight sampled pixels match the engine-emitted segment; the paint mutant exits non-zero |

C4, C7d and C9d are the controls that protect the assertions:
`ace_layout_probe_mutant`, `ace_tree_probe_mutant` and `ace_paint_probe_mutant` are
the same sources with one deliberately wrong expectation compiled in, and they must
FAIL. If any ever passes, every PASS from its real probe is worthless.

C2b caught a real bug in an earlier version of itself, which had failed to actually
remove the shims directory — the control failed, correctly, and the control was
fixed rather than the expectation.

**What these controls do not prove:** that ace_engine computes correct numbers in
an absolute sense. They prove the expectations in `main.cpp` are compared and that
the scaffolding does work. Absolute correctness needs the upstream unit tests,
which need the full OpenHarmony tree.

## Target architecture: the combination

ArkUI and GNUstep are alternative UI toolkits with no natural interop layer, so
"combining" has to be given a concrete shape:

```
  ┌─────────────────────────────────────────────────────────┐
  │  the app (Objective-C / GNUstep)                        │
  │    NSApplication, NSWindow, NSMenu, app lifecycle,      │
  │    preferences, file dialogs  ── the native shell       │
  ├─────────────────────────────────────────────────────────┤
  │  ArkUIView : NSView                                     │
  │    owns an ace_engine PipelineContext                   │
  │    forwards resize / mouse / key into ace_engine        │
  │    blits ace_engine's surface                           │
  ├─────────────────────────────────────────────────────────┤
  │  ace_engine (C++)  ── the declarative content           │
  │    components_ng: FrameNode + Pattern + properties      │
  │    Flex / Grid / RelativeContainer layout, animation,   │
  │    gestures, text                                        │
  └─────────────────────────────────────────────────────────┘
```

GNUstep is the *shell*; ArkUI is the *content*. That is the same split ArkUI uses
on iOS and macOS upstream, so there is precedent rather than invention, and neither
project has to be reimplemented in the other's image.

The render backend becomes a **swap** rather than a dependency, thanks to the
`ACE_UNITTEST` finding: options are Skia, a software rasteriser, or CodeOS's
framebuffer.

## Staged plan

| stage | goal | verified by | state |
|---|---|---|---|
| 0 | foundation compiles and runs off OpenHarmony | `ace_layout_probe` + `controls.py` | **done** |
| 1a | layout core compiles renderer-free | `classify.py` 51/51 + C5 | **done** |
| 1b | layout core **links** and measures geometry | `ace_ng_layout_probe` + controls C4/C6 | **done** |
| 2a | the real algorithm lays out a tree, software-rastered | `ace_tree_probe` + controls C7 | **done** |
| 2b | the engine's paint code emits a draw command, captured, rastered, and shown live | `ace_paint_probe` + controls C9; window C8c | **done** |
| 2c | paint a whole `FrameNode` tree (background/border/foreground) | engine-raster pixel assertions | not started |
| 3a | a live host window shows the layout and relayouts on resize | `ace_window_demo` + control C8 | **done** |
| 3b | a GNUstep app wraps that output in a VM window | screenshot | not started |
| 4 | mouse/keyboard/resize forwarded into ace_engine | interaction drives a change | not started |
| 5 | bring it to CodeOS | boot-verified | not started |

## Environment (host)

Arch Linux. `gnustep-base` and `gnustep-make` are in `extra`; `gnustep-gui` and
`libobjc2` need AUR/source and `yay` is present. QEMU 11.1.1 is available for the VM.
GitHub is reachable. No Skia is installed — which, per the `ACE_UNITTEST` finding,
is not automatically a blocker.

## Files

| file | purpose |
|---|---|
| `aceroots.py` | the include roots the real GN build uses, in one place |
| `classify.py` | compiles a subtree and buckets each file by first error, by *distinct root cause* |
| `closure.py` | walks a header `#include` closure and reports unresolvable targets |
| `linkclosure.py` | maps undefined symbols in a link failure to the `.cpp` files that define them |
| `closure_sources.txt` | the source set `linkclosure.py` searches |
| `CMakeLists.txt` | the host build; explicit exclusion list for third-party deps |
| `main.cpp` | foundation probe: 20 assertions over Dimension and Matrix3 |
| `layout_probe.cpp` | layout probe: assertions over LayoutConstraint geometry (links via the upstream mock) |
| `ace_scene.hpp` | shared scene: `ProbeWrapper` (LayoutWrapper double), `LayoutScene`, `Flatten`, `Surface` software raster |
| `tree_probe.cpp` | tree probe: real `LinearLayoutUtils` over the double + software raster |
| `paint_capture.hpp` | `RecordingCanvas`: records the engine's `DrawLine` geometry |
| `paint_raster.hpp` | `StampLine`: draws a captured engine draw command into the `Surface` |
| `paint_probe.cpp` | paint probe: real `DividerModifier`/`DividerPainter` draw command + software raster |
| `tree_seams.cpp` | host seams for unreachable service paths of the tree probe (no layout arithmetic) |
| `window_demo.cpp` | live X11 window: presents the engine's layout, re-runs it on resize |
| `demo.cpp` | minimal `ace_demo`: exercises the mock renderer seam |
| `controls.py` | 23 negative controls; must exit 0 |
| `compat/ace_compat.hpp` | forced include covering five upstream missing-include bugs |
| `compat/shims/` | `securec.h` (implemented), `hilog/log.h` (no-op), `refbase.h` (limited) |
| `ohos-root/` | symlink tree so 7 files' `foundation/arkui/ace_engine/...` includes resolve |

## Known gaps

* **Layout links by an explicit seam, not by closing the service core.** The probe
  compiles upstream's `mock_calc_length.cpp` in place of the real TU; the real
  one is excluded by name and C6 proves the difference. Everything the probe
  measures is therefore the real layout arithmetic, with `ScaleProperty` fixed at
  the no-container default. Stage 2a's tree probe stays on the same side of that
  seam: it feeds the real algorithm a `LayoutWrapper` double and answers the
  unreachable service paths in `tree_seams.cpp`. The engine's **paint** code runs
  off-pipeline too (Stage 2b): `ace_paint_probe` drives a real `DividerModifier`
  and captures its draw command with a recording canvas, but that is one painter,
  not a tree. Painting a whole `FrameNode` (background/border/foreground) wants a
  real `FrameNode` and a render context, which is the 1226-ref service/render core;
  that closure is still open (Stage 2c).
* **Link coverage is measured as a tool.** `linkclosure.py` maps undefined symbols
  in a linker failure to the `.cpp` files that appear to define them (tracking
  namespace scope and trimming Itanium ABI tags), with explicit policy (prefer
  `adapter/preview/` over `ohos/`, prefer `fake_*` over `rosen_*` under
  `ACE_UNITTEST`). It is what showed the naive closure growing 517 → 1016 → 1289
  and reaching Skia/OHOS IPC — i.e. that closing it wholesale is the wrong move.
  `classify.py` and `closure.py` remain for compile/header reachability.
* `securec.h` covers three functions. `refbase.h` does not share OHOS refcounting.
* Nothing here has been run on CodeOS. That is stage 5.
