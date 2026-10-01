# ace-host — building OpenHarmony's ArkUI off OpenHarmony

A host build of `arkui_ace_engine`, with the goal of combining it with GNUstep
into one app runtime that can eventually run on CodeOS. This directory is the
first step and it is deliberately small: it answers *whether the port is
possible at all* with numbers rather than opinion.

Run it:

```sh
cd ace-host
cmake -S . -B build && cmake --build build -j"$(nproc)"   # GREEN
./build/ace_layout_probe                                  # 20 assertions, exit 0
python3 controls.py                                       # 10 negative controls, exit 0
python3 classify.py <dir>                                 # compile-and-bucket any subtree
```

## Why a host build first, and why this is not the OpenHarmony build

`arkui_ace_engine` ships only a GN build. `BUILD.gn` imports `//build/ohos.gni` —
the OpenHarmony build system — which is not in the ace_engine repo, and the engine
also wants `//third_party/skia`. So **as it stands there is no way to build this
tree on any machine** without first standing up the whole OpenHarmony build.

Before doing that, it is worth knowing how much of the engine actually depends on
OpenHarmony. That question is answerable cheaply, and this directory answers it.

## What compiles today

| layer | compiles | notes |
|---|---|---|
| `frameworks/base` (foundation) | **57 / 63** | the 57 **link and run**; the 6 need real third-party code |
| `components_ng` layout core (`base`, `property`, `linear_layout`) | **51 / 51** | renderer-free via `ACE_UNITTEST` |

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

## The open frontier: compiling ≠ linking

The layout core **compiles** 51/51, but its probe does not yet **link**, and the
reason is specific and worth recording:

```cpp
// frameworks/core/components_ng/property/layout_constraint.h:54
ScaleProperty scaleProperty = ScaleProperty::CreateScaleProperty();      // default member init

// frameworks/core/components_ng/property/calc_length.cpp:26
auto pipeline = context ? AceType::Claim(context) : PipelineBase::GetCurrentContextSafely();
CHECK_NULL_RETURN(pipeline, scaleProperty);                             // default 1.0 scale
```

On a host with no container that call is **null-safe** and yields the default
scale — but the reference is still emitted, so the linker requires `PipelineBase`,
which requires `Container`, `AceEngine`, `Kit::UIContext`, `JsonUtil` (→ cJSON) and
the trace machinery. **Constructing a layout constraint is not purely functional
even though measuring with one is.**

So `ace_ng_layout_probe` is `EXCLUDE_FROM_ALL` by design and the build stays
green rather than shipping a red target. Closing that closure — the engine's
service core — is the next milestone, and it is a *link* problem, not a *compile*
problem. `classify.py` measures compile reachability only; link coverage is not
yet measured for the whole tree.

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

## Negative controls (10/10)

An assertion never seen to fail is not an assertion, and a shim the compiler
optimises away is not a shim. `controls.py` removes one ingredient at a time:

| control | removes | requires |
|---|---|---|
| C1a / C1b | the forced-include compat header | `matrix3.cpp` fails on `std::for_each` |
| C2a / C2b | the securec shim | `ace_trace.cpp` fails on `securec.h` |
| C3a / C3b | `-Wno-c++11-narrowing` | `dimension.cpp` fails on narrowing |
| C5a / C5b | `-DACE_UNITTEST` | `view_abstract.cpp` fails on `draw/canvas.h` |
| C4a / C4b | the probe's pass/fail machinery | the mutant probe exits non-zero |

C4 is the one that protects the assertions: `ace_layout_probe_mutant` is the same
source with one deliberately wrong expectation compiled in, and it must FAIL. If it
ever passes, every PASS from `ace_layout_probe` is worthless.

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
| 1b | layout core **links** and measures a tree | assert measured geometry | blocked on the service-core closure above |
| 2 | ace_engine rasterises a component tree | pixel assertions | not started |
| 3 | a GNUstep app shows that output in a VM window | screenshot | not started |
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
| `CMakeLists.txt` | the host build; explicit exclusion list for third-party deps |
| `main.cpp` | foundation probe: 20 assertions over Dimension and Matrix3 |
| `layout_probe.cpp` | layout probe: assertions over LayoutConstraint geometry (not yet linked) |
| `controls.py` | 10 negative controls; must exit 0 |
| `compat/ace_compat.hpp` | forced include covering five upstream missing-include bugs |
| `compat/shims/` | `securec.h` (implemented), `hilog/log.h` (no-op), `refbase.h` (limited) |
| `ohos-root/` | symlink tree so 7 files' `foundation/arkui/ace_engine/...` includes resolve |

## Known gaps

* **Link coverage is not measured.** `classify.py` and `closure.py` report header
  and compile reachability only. A file that compiles may still fail to link; this
  is exactly what stage 1b is blocked on.
* `securec.h` covers three functions. `refbase.h` does not share OHOS refcounting.
* `layout_probe.cpp` compiles but its binary does not link yet, by design.
* Nothing here has been run on CodeOS. That is stage 5.
