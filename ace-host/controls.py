#!/usr/bin/env python3
"""Negative controls for the ace_host build.

WHY THIS EXISTS
---------------
Every claim this build makes is of the form "X is what makes Y work". Such a
claim is only worth something if removing X breaks Y. Otherwise it is decoration,
and a build that passes for the wrong reason is more dangerous than one that
fails, because it looks like progress.

Each control REMOVES one ingredient and requires a specific failure:

  C1  the forced-include compat header  -> matrix3.cpp must fail on std::for_each
  C2  the securec shim                  -> ace_trace.cpp must fail on securec.h
  C3  -Wno-c++11-narrowing              -> dimension.cpp must fail on narrowing
  C4  the probe's pass/fail machinery   -> the mutant probe must FAIL
  C5  -DACE_UNITTEST                    -> view_abstract.cpp must fail on draw/canvas.h
  C6  the mock_calc_length link seam    -> real calc_length.cpp must reference
                                           PipelineBase, the mock must not
  C7  the tree probe                    -> links the real LinearLayoutUtils, its
                                           PASS is a comparison that can fail,
                                           and the raster it reports is real
  C8  the live window                   -> links the real LinearLayoutUtils, and
                                           resizing makes the engine re-lay the
                                           tree out (pixel count tracks W/2)
  C9  the paint probe                   -> links the real DividerPainter::DrawLine,
                                           its PASS is a comparison that can fail,
                                           and the raster contains the engine's
                                           own draw command

C1-C3, C5 and C6 are compile/link-time and prove each shim/flag/TU swap is
load-bearing rather than inert. C4, C7b, C8b and C9b are the ones that matter for
the assertions: they prove a PASS from the probes is a real comparison that can
come out false, rather than a program that ran and printed.

WHAT THESE CONTROLS DO NOT PROVE, per control, is printed with the result.
Nothing here shows ace_engine computes the *right* numbers in an absolute sense --
only that main.cpp's expectations are actually compared and that the port's
scaffolding is doing work. Absolute correctness needs the upstream unit tests,
which need the full OpenHarmony tree.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from aceroots import ROOT, INCLUDE_DIRS

INC = [f"-I{d}" for d in INCLUDE_DIRS]
SHIMS = f"{HERE}/compat/shims"
COMPAT = f"{HERE}/compat/ace_compat.hpp"

results = []


def report(name, ok, detail, proves):
    results.append(ok)
    print(f"  [{'ok  ' if ok else 'FAIL'}] {name}")
    print(f"          {detail}")
    print(f"          proves: {proves}")


def compiles(src, extra, with_shims=True):
    # The shims dir is part of INCLUDE_DIRS, so dropping it means filtering it out
    # of that list rather than simply not prepending it.
    inc = [f"-I{d}" for d in INCLUDE_DIRS if with_shims or d != SHIMS]
    return subprocess.run(
        ["clang++", "-std=gnu++17", "-fsyntax-only"] + inc + extra + [src],
        capture_output=True, text=True)


def control_compile(name, src, extra, want_ok, needle, proves, with_shims=True):
    p = compiles(src, extra, with_shims=with_shims)
    ok_now = (p.returncode == 0)
    blob = p.stdout + p.stderr
    if want_ok:
        ok = ok_now
        detail = f"rc={p.returncode} (expected a clean compile)"
    else:
        ok = (not ok_now) and (needle in blob)
        detail = (f"rc={p.returncode}, {'found' if needle in blob else 'DID NOT FIND'} "
                  f"{needle!r}")
    report(name, ok, detail, proves)


print("ace_host negative controls")
print()

# --- C1: the compat header is load-bearing ----------------------------------
# The positive case runs first: a file that never compiled would make the
# negative result prove nothing.
control_compile(
    "C1a  matrix3.cpp WITH the compat header",
    f"{ROOT}/frameworks/base/geometry/matrix3.cpp", ["-include", COMPAT], True, "",
    "baseline: this file is otherwise buildable")
control_compile(
    "C1b  matrix3.cpp WITHOUT the compat header",
    f"{ROOT}/frameworks/base/geometry/matrix3.cpp", [], False, "for_each",
    "the missing <algorithm> in matrix3.h is real, and the forced include is what "
    "covers it")

# --- C2: the securec shim is load-bearing -----------------------------------
control_compile(
    "C2a  ace_trace.cpp WITH the shims dir",
    f"{ROOT}/frameworks/base/log/ace_trace.cpp",
    ["-include", COMPAT, "-Wno-c++11-narrowing"], True, "",
    "baseline: securec.h resolves from compat/shims")
control_compile(
    "C2b  ace_trace.cpp WITHOUT the shims dir",
    f"{ROOT}/frameworks/base/log/ace_trace.cpp",
    ["-include", COMPAT, "-Wno-c++11-narrowing"], False, "securec.h",
    "the OpenHarmony <securec.h> dependency is real and unvendored, so the shim is "
    "what stands in for it",
    with_shims=False)

# --- C3: the narrowing flag is load-bearing ---------------------------------
control_compile(
    "C3a  dimension.cpp WITH -Wno-c++11-narrowing",
    f"{ROOT}/frameworks/base/geometry/dimension.cpp",
    ["-include", COMPAT, "-Wno-c++11-narrowing"], True, "",
    "baseline")
control_compile(
    "C3b  dimension.cpp WITHOUT -Wno-c++11-narrowing",
    f"{ROOT}/frameworks/base/geometry/dimension.cpp",
    ["-include", COMPAT], False, "narrowing",
    "there is exactly one narrowing site in frameworks/base and the flag is what "
    "gets past it")

# --- C5: ACE_UNITTEST is load-bearing ----------------------------------------
# The layout core's renderer dependency is behind components_ng/render/drawing.h,
# which is hard-wired to Skia unless ACE_UNITTEST selects the in-tree mock. This
# control proves that claim by compiling a layout source with and without the
# flag: without it, the renderer leaks in.
control_compile(
    "C5a  view_abstract.cpp WITH -DACE_UNITTEST",
    f"{ROOT}/frameworks/core/components_ng/base/view_abstract.cpp",
    ["-include", COMPAT, "-Wno-c++11-narrowing", "-DACE_UNITTEST=1"], True, "",
    "baseline: the layout core compiles using the in-tree mock renderer")
control_compile(
    "C5b  view_abstract.cpp WITHOUT -DACE_UNITTEST",
    f"{ROOT}/frameworks/core/components_ng/base/view_abstract.cpp",
    ["-include", COMPAT, "-Wno-c++11-narrowing"], False, "draw/canvas.h",
    "the renderer dependency is real and sits behind the ACE_UNITTEST switch, so "
    "the layout layer is renderer-free only through that seam")

# --- C4: the probe's PASS is a real comparison ------------------------------
mutant = f"{HERE}/build/ace_layout_probe_mutant"
plain = f"{HERE}/build/ace_layout_probe"

if os.path.exists(plain):
    p = subprocess.run([plain], capture_output=True, text=True)
    report("C4a  the real probe exits 0",
           p.returncode == 0,
           f"rc={p.returncode}, last line: {p.stdout.strip().splitlines()[-1]!r}",
           "the assertions hold against the real library")
else:
    report("C4a  the real probe exits 0", False, f"{plain} not built", "n/a")

if os.path.exists(mutant):
    p = subprocess.run([mutant], capture_output=True, text=True)
    report("C4b  the mutant probe exits NON-zero",
           p.returncode != 0 and "MUTANT" in p.stdout,
           f"rc={p.returncode}; mutant line present: {'MUTANT' in p.stdout}",
           "the harness can actually fail, so C4a's PASS is not vacuous -- this is "
           "the control that would catch a probe whose checks were optimised out or "
           "never reached")
else:
    report("C4b  the mutant probe exits NON-zero", False, f"{mutant} not built", "n/a")

# --- C6: the layout probe's link seam is upstream's mock_calc_length ---------
# Stage 1b. LayoutConstraintT's default member initialiser calls
# ScaleProperty::CreateScaleProperty() (layout_constraint.h:54). The real
# definition (calc_length.cpp) exists only to read the current density, and to do
# that it reaches PipelineBase::GetCurrentContextSafely() -- which drags the
# service core into the link. Upstream's own mock returns the default
# ScaleProperty and reaches nothing. The probe compiles the mock in place of the
# real TU; this control proves that swap is what keeps the closure inside layout.
#
# It compiles both translation units and inspects their symbols: the real object
# must reference PipelineBase (so it could not be linked standalone), and the
# mock object must define CreateScaleProperty without referencing PipelineBase.
import shutil
import tempfile


def _tu_symbols(src):
    d = tempfile.mkdtemp(prefix="acehost-c6-")
    obj = os.path.join(d, "tu.o")
    p = subprocess.run(
        ["clang++"] + INC
        + ["-std=gnu++17", "-include", COMPAT, "-Wno-c++11-narrowing",
           "-DACE_UNITTEST=1", "-c", src, "-o", obj],
        capture_output=True, text=True)
    if p.returncode != 0:
        shutil.rmtree(d, ignore_errors=True)
        return None, p.stdout + p.stderr
    nm = subprocess.run(["nm", "-C", obj], capture_output=True, text=True).stdout
    shutil.rmtree(d, ignore_errors=True)
    return nm, ""


real_cl = f"{ROOT}/frameworks/core/components_ng/property/calc_length.cpp"
mock_cl = (f"{ROOT}/test/mock/frameworks/core/components_ng/property/"
           f"mock_calc_length.cpp")

real_syms, real_err = _tu_symbols(real_cl)
if real_syms is None:
    report("C6a  real calc_length.cpp references the service core", False,
           f"did not compile: {real_err.strip().splitlines()[-1] if real_err else '?'}",
           "n/a")
else:
    refs = "GetCurrentContextSafely" in real_syms
    report("C6a  real calc_length.cpp references the service core", refs,
           f"references PipelineBase::GetCurrentContextSafely: {refs}",
           "the real definition cannot be linked without PipelineBase and its "
           "whole transitive closure, which is why it is excluded from the probe")

mock_syms, mock_err = _tu_symbols(mock_cl)
if mock_syms is None:
    report("C6b  mock_calc_length.cpp defines the symbol and reaches nothing",
           False,
           f"did not compile: {mock_err.strip().splitlines()[-1] if mock_err else '?'}",
           "n/a")
else:
    defines = "T OHOS::Ace::NG::ScaleProperty::CreateScaleProperty" in mock_syms
    reaches = "GetCurrentContextSafely" in mock_syms
    report("C6b  mock_calc_length.cpp defines the symbol and reaches nothing",
           defines and not reaches,
           f"defines CreateScaleProperty: {defines}; references PipelineBase: {reaches}",
           "the substitute is a real definition of the exact symbol the probe "
           "needs, and it is what lets the probe link with no service core -- C6a "
           "is the failure this swap avoids, so C6b is not vacuous")

# --- C7: the layout-TREE probe runs the real algorithm -----------------------
# Stage 2. tree_probe builds a parent/children tree over a LayoutWrapper test
# double and runs LinearLayoutUtils::Measure/Layout from the engine. Three
# things have to hold for the PASS to mean anything: the binary must contain the
# engine's algorithm (not a local reimplementation), the PASS must be able to
# fail, and the raster it prints must actually contain the geometry it asserts.
tree_probe = f"{HERE}/build/ace_tree_probe"
tree_mutant = f"{HERE}/build/ace_tree_probe_mutant"

if os.path.exists(tree_probe):
    nm = subprocess.run(["nm", "-C", tree_probe], capture_output=True, text=True).stdout
    has_measure = "OHOS::Ace::NG::LinearLayoutUtils::Measure" in nm
    has_layout = "OHOS::Ace::NG::LinearLayoutUtils::Layout" in nm
    report("C7a  tree probe links the engine's linear-layout algorithm",
           has_measure and has_layout,
           f"LinearLayoutUtils::Measure={has_measure}, Layout={has_layout} "
           f"(present = linked, not garbage-collected, so actually called)",
           "the tree geometry comes from LinearLayoutUtils in the engine, not "
           "from a layout routine reimplemented in the probe")

    d = tempfile.mkdtemp(prefix="acehost-c7-")
    ppm = os.path.join(d, "tree.ppm")
    p = subprocess.run([tree_probe, ppm], capture_output=True, text=True)
    report("C7b  the real tree probe exits 0",
           p.returncode == 0,
           f"rc={p.returncode}, last line: {p.stdout.strip().splitlines()[-1]!r}",
           "the engine lays out the tree to the asserted geometry")

    ok_file = os.path.exists(ppm)
    header_ok = False
    pixels_ok = False
    detail = "no PPM written"
    if ok_file:
        with open(ppm, "rb") as f:
            data = f.read()
        header = data[:15].split(b"\n")
        header_ok = header[:3] == [b"P6", b"360 640", b"255"]
        pix = data[15:]
        # interior pixels, one per node, in the order the probe draws them
        want = {
            (50, 25): (0xE0, 0x5C, 0x5C),    # child a
            (50, 75): (0x5C, 0xE0, 0xA0),    # child b
            (40, 120): (0x60, 0x90, 0xF0),   # nested-row child c
            (120, 120): (0xB0, 0x60, 0xE0),  # nested-row child d
            (50, 180): (0xF0, 0x80, 0x50),   # child e
            (330, 600): (0xE8, 0xE8, 0xF0),  # root background
        }
        pixels_ok = all(
            tuple(pix[(y * 360 + x) * 3:(y * 360 + x) * 3 + 3]) == rgb
            for (x, y), rgb in want.items())
        detail = (f"header ok={header_ok}; the {len(want)} sampled pixels "
                  f"match the computed layout={pixels_ok}")
    report("C7c  the reported raster actually contains the layout",
           ok_file and header_ok and pixels_ok,
           detail,
           "the software rasteriser draws the geometry the engine produced, so "
           "the 'visual proof' is the layout output and not a hard-coded picture")
    shutil.rmtree(d, ignore_errors=True)
else:
    report("C7a  tree probe links the engine's linear-layout algorithm", False,
           f"{tree_probe} not built", "n/a")
    report("C7b  the real tree probe exits 0", False, f"{tree_probe} not built", "n/a")
    report("C7c  the reported raster actually contains the layout", False,
           f"{tree_probe} not built", "n/a")

if os.path.exists(tree_mutant):
    p = subprocess.run([tree_mutant], capture_output=True, text=True)
    report("C7d  the tree mutant probe exits NON-zero",
           p.returncode != 0 and "MUTANT" in p.stdout,
           f"rc={p.returncode}; mutant line present: {'MUTANT' in p.stdout}",
           "the tree probe's comparisons can come out false, so C7b's PASS is not "
           "vacuous")
else:
    report("C7d  the tree mutant probe exits NON-zero", False,
           f"{tree_mutant} not built", "n/a")

# --- C8: the live X11 window re-runs the engine's layout on resize -----------
# window_demo lays the tree out at the window's current size and shows it. The
# claim is that the *engine's* layout is live: resizing must change the geometry
# in a way the engine computes. The scene gives the proportional node a unique
# colour, so counting its pixels measures the width the engine gave it. At width
# W the node is 0.5*W wide; FillRect insets one pixel on each side, so the count
# is (W/2 - 2) * (30 - 2). Two sizes are run under Xvfb; a hard-coded buffer, or
# a layout that ignored the resize, could not match both.
window_demo = f"{HERE}/build/ace_window_demo"
if os.path.exists(window_demo) and shutil.which("xvfb-run"):
    nm = subprocess.run(["nm", "-C", window_demo], capture_output=True, text=True).stdout
    has_measure = "OHOS::Ace::NG::LinearLayoutUtils::Measure" in nm
    has_layout = "OHOS::Ace::NG::LinearLayoutUtils::Layout" in nm
    report("C8a  window demo links the engine's linear-layout algorithm",
           has_measure and has_layout,
           f"LinearLayoutUtils::Measure={has_measure}, Layout={has_layout}",
           "the window's pixels come from the engine's layout, not from a "
           "layout routine reimplemented in the demo")

    def _pct_count(width, height, tag):
        d = tempfile.mkdtemp(prefix="acehost-c8-")
        ppm = os.path.join(d, f"window_{tag}.ppm")
        p = subprocess.run(["xvfb-run", "-a", window_demo, "--size", f"{width}x{height}",
                            "--frames", "1", "--screenshot", ppm],
                           capture_output=True, text=True)
        if p.returncode != 0 or not os.path.exists(ppm):
            shutil.rmtree(d, ignore_errors=True)
            return None, f"rc={p.returncode}: {p.stderr.strip()[:120]}"
        data = open(ppm, "rb").read()
        header = data[:15].split(b"\n")
        if header[:3] != [b"P6", f"{width} {height}".encode(), b"255"]:
            shutil.rmtree(d, ignore_errors=True)
            return None, f"bad header {header[:3]}"
        pix = data[15:]
        target = bytes((0xff, 0x2d, 0x95))  # unique colour of the 0.5-width node
        n = sum(1 for i in range(0, len(pix), 3) if pix[i:i + 3] == target)
        shutil.rmtree(d, ignore_errors=True)
        return n, ""

    c1, e1 = _pct_count(360, 640, "360")
    c2, e2 = _pct_count(640, 480, "640")
    want1 = (360 // 2 - 2) * (30 - 2)
    want2 = (640 // 2 - 2) * (30 - 2)
    ok = (c1 == want1) and (c2 == want2) and c1 != c2
    report("C8b  resizing re-runs the engine's layout proportionally",
           ok,
           f"0.5-width node pixels: at 360 wide {c1} (want {want1}), at 640 wide {c2} "
           f"(want {want2})" + (f"; {e1}{e2}" if (e1 or e2) else ""),
           "the window's own size drives a fresh engine layout whose result is "
           "visible in the pixels; matching W/2-2 at two different widths cannot "
           "come from one hard-coded picture")
else:
    print("  [skip] C8 window demo control (no ace_window_demo and/or no xvfb-run)")

# --- C9: the PAINT probe runs the engine's own draw code ---------------------
# Stage 2b. paint_probe drives a real DividerModifier off-pipeline and captures the
# DrawLine the engine emits. Three things must hold for its PASS to mean anything:
# the binary must contain the engine's DividerPainter::DrawLine (so the endpoint
# arithmetic under test is the engine's, not the probe's), the PASS must be able to
# fail, and the raster it reports must contain the engine-computed segment.
paint_probe = f"{HERE}/build/ace_paint_probe"
paint_mutant = f"{HERE}/build/ace_paint_probe_mutant"

if os.path.exists(paint_probe):
    nm = subprocess.run(["nm", "-C", paint_probe], capture_output=True, text=True).stdout
    has_drawline = "OHOS::Ace::NG::DividerPainter::DrawLine" in nm
    report("C9a  paint probe links the engine's DividerPainter::DrawLine",
           has_drawline,
           f"DividerPainter::DrawLine present={has_drawline} "
           f"(present = linked, not garbage-collected, so actually called)",
           "the captured endpoints were computed by the engine's painter, not by "
           "arithmetic reimplemented in the probe")

    d = tempfile.mkdtemp(prefix="acehost-c9-")
    ppm = os.path.join(d, "paint.ppm")
    p = subprocess.run([paint_probe, ppm], capture_output=True, text=True)
    report("C9b  the real paint probe exits 0",
           p.returncode == 0,
           f"rc={p.returncode}, last line: {p.stdout.strip().splitlines()[-1]!r}",
           "the engine's paint path runs to completion and its computed endpoints "
           "match the asserted values")

    ok_file = os.path.exists(ppm)
    header_ok = False
    pixels_ok = False
    detail = "no PPM written"
    if ok_file:
        data = open(ppm, "rb").read()
        header = data[:15].split(b"\n")
        header_ok = header[:3] == [b"P6", b"240 240", b"255"]
        pix = data[15:]
        red = (0xFF, 0x00, 0x00)
        bg = (0x10, 0x10, 0x18)
        want = {
            (10, 22): red, (209, 22): red,    # painted span [10, 210)
            (9, 22): bg, (210, 22): bg,       # just outside it
            (100, 20): red, (100, 23): red,   # 4px stroke rows [20, 24)
            (100, 19): bg, (100, 24): bg,
        }
        pixels_ok = all(
            tuple(pix[(y * 240 + x) * 3:(y * 240 + x) * 3 + 3]) == rgb
            for (x, y), rgb in want.items())
        detail = (f"header ok={header_ok}; the {len(want)} sampled pixels match the "
                  f"engine-emitted segment={pixels_ok}")
    report("C9c  the reported raster contains the engine's draw command",
           ok_file and header_ok and pixels_ok,
           detail,
           "the software rasteriser draws the segment the engine emitted, so the "
           "'paint proof' is the engine's draw output, not a hard-coded picture")
    shutil.rmtree(d, ignore_errors=True)
else:
    report("C9a  paint probe links the engine's DividerPainter::DrawLine", False,
           f"{paint_probe} not built", "n/a")
    report("C9b  the real paint probe exits 0", False, f"{paint_probe} not built", "n/a")
    report("C9c  the reported raster contains the engine's draw command", False,
           f"{paint_probe} not built", "n/a")

if os.path.exists(paint_mutant):
    p = subprocess.run([paint_mutant], capture_output=True, text=True)
    report("C9d  the paint mutant probe exits NON-zero",
           p.returncode != 0 and "MUTANT" in p.stdout,
           f"rc={p.returncode}; mutant line present: {'MUTANT' in p.stdout}",
           "the paint probe's comparisons can come out false, so C9b's PASS is not "
           "vacuous")
else:
    report("C9d  the paint mutant probe exits NON-zero", False,
           f"{paint_mutant} not built", "n/a")

print()
failed = results.count(False)
print(f"{'ALL CONTROLS OK' if not failed else 'CONTROLS FAILED'}: "
      f"{len(results) - failed}/{len(results)} ok")
sys.exit(1 if failed else 0)
