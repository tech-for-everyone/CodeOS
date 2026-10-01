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

C1-C3 are compile-time and prove each shim/flag is load-bearing rather than inert.
C4 is the one that matters for the assertions: it proves a PASS from
ace_layout_probe is a real comparison that can come out false, rather than a
program that ran and printed.

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

print()
failed = results.count(False)
print(f"{'ALL CONTROLS OK' if not failed else 'CONTROLS FAILED'}: "
      f"{len(results) - failed}/{len(results)} ok")
sys.exit(1 if failed else 0)
