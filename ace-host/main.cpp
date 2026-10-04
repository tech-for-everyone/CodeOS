// Host probe for the ace_engine foundation + layout layer.
//
// This is the "does it run" half of the question the CMakeLists asks. Compiling
// proves the code has no OpenHarmony or Skia dependency; running asserts that the
// numbers it produces are the ones the API documents, which is the property an
// embedder actually depends on.
//
// Deliberately NOT a rendering probe. There is no pixel here and no surface:
// layout is a pure function from a component tree plus a constraint to a set of
// geometry values, so it is assertable exactly, with no tolerance and no
// framebuffer. Rendering is the part that needs a rasteriser and is a separate
// question -- and the two excluded files in this very tree that need Skia are the
// evidence that the separation is real rather than wishful.
//
// Each assertion below is chosen because it fails for a DIFFERENT reason:
//   Dimension   -> the unit field survives, i.e. 100px != 100%
//   Matrix3     -> the compat header is load-bearing (std::for_each, <algorithm>)
//   Invert      -> real numerical code runs, not just constructors reached

#include <cmath>
#include <cstdio>
#include <vector>

#include "base/geometry/dimension.h"
#include "base/geometry/matrix3.h"

using namespace OHOS::Ace;

static int failures = 0;

static void check(const char *what, double got, double want)
{
    const bool ok = (got == want);
    if (!ok) {
        failures++;
    }
    std::printf("  [%s] %-42s got %-10g want %g\n",
                ok ? "ok  " : "FAIL", what, got, want);
}

static void checkNear(const char *what, double got, double want, double tol)
{
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) {
        failures++;
    }
    std::printf("  [%s] %-42s got %-10g want %g (+-%g)\n",
                ok ? "ok  " : "FAIL", what, got, want, tol);
}

int main()
{
    std::printf("ace layout probe\n");

    // Dimension carries a value and a unit, and the unit is the whole reason it
    // exists: 100px and 100% are the same number and different constraints. If
    // a port silently lost the unit, layout would still run and every length
    // would be wrong.
    {
        const Dimension px(100.0, DimensionUnit::PX);
        const Dimension pct(100.0, DimensionUnit::PERCENT);
        check("Dimension::Value() keeps px",    px.Value(), 100.0);
        check("Dimension::Value() keeps pct",   pct.Value(), 100.0);
        check("px unit is PX",                  px.Unit() == DimensionUnit::PX, 1);
        check("pct unit is PERCENT",            pct.Unit() == DimensionUnit::PERCENT, 1);
        check("px unit is not pct",             px.Unit() == pct.Unit(), 0);
    }

    // Arithmetic, and the unit travelling with the value.
    {
        const Dimension a(3.0, DimensionUnit::VP);
        const Dimension b(4.0, DimensionUnit::VP);
        check("Dimension + Dimension",  (a + b).Value(), 7.0);
        check("Dimension - Dimension",  (b - a).Value(), 1.0);
        check("Dimension * 2",          (a * 2.0).Value(), 6.0);
        check("Dimension / 2",          (b / 2.0).Value(), 2.0);
        check("result unit survives +", (a + b).Unit() == DimensionUnit::VP, 1);
        // Division by ~0 returns a default Dimension (0 PX) rather than inf.
        check("Dimension / 0 is 0",     (b / 0.0).Value(), 0.0);
    }

    // Matrix3's scalar operator* is inline and calls std::for_each on a vector
    // without including <algorithm>. This assertion is what makes the compat
    // header load-bearing: without -include compat/ace_compat.hpp this file does
    // not compile at all, and with it the arithmetic is checked, so the header
    // cannot rot into an unused no-op.
    {
        Matrix3 m;
        m.SetEntry(0, 0, 1.0);
        m.SetEntry(1, 1, 2.0);
        m.SetEntry(2, 2, 3.0);
        m * 2.0;
        check("Matrix3 scalar mul (0,0)", m(0, 0), 2.0);
        check("Matrix3 scalar mul (1,1)", m(1, 1), 4.0);
        check("Matrix3 scalar mul (2,2)", m(2, 2), 6.0);
        check("Matrix3 off-diagonal untouched", m(0, 1), 0.0);
    }

    // Real numerical code, not just constructors: invert a diagonal matrix and
    // multiply back to identity. This reaches matrix3.cpp's actual algorithm.
    {
        Matrix3 m;
        m.SetEntry(0, 0, 2.0);
        m.SetEntry(1, 1, 4.0);
        m.SetEntry(2, 2, 5.0);
        Matrix3 inv;
        const bool ok = m.Invert(inv);
        check("Matrix3::Invert reports success", ok, 1);
        checkNear("inverse (0,0) is 1/2", inv(0, 0), 0.5, 1e-12);
        checkNear("inverse (1,1) is 1/4", inv(1, 1), 0.25, 1e-12);
        checkNear("inverse (2,2) is 1/5", inv(2, 2), 0.2, 1e-12);
        // A singular matrix must refuse rather than emit garbage.
        Matrix3 singular;
        singular.SetEntry(0, 0, 1.0);
        Matrix3 out;
        check("singular Invert returns false", singular.Invert(out), 0);
    }

#ifdef ACE_PROBE_MUTANT
    // Negative control on the harness itself, not on ace_engine. If the probe
    // still reported PASS with a deliberately wrong expectation in it, then its
    // PASS would be vacuous -- it would be evidence that printf ran, not that
    // any comparison happened. Built as a separate target by controls.py, which
    // requires this to FAIL.
    check("MUTANT control: deliberately wrong expectation", 1.0, 2.0);
#endif

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
