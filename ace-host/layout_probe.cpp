// Probe for ace_engine's *layout* layer (components_ng), as opposed to the
// foundation value types the sibling probe covers. Separate binary because the
// two fail for different reasons.
//
// WHY THESE PARTICULAR ASSERTIONS
// -------------------------------
// A layout engine is a pure function from a constraint to a rectangle, which
// makes it exactly assertable with no tolerance and no framebuffer. Each case
// below is chosen so that a *specific* wrong implementation fails it:
//
//   aspect ratio, width-driven   -> catches a solver that ignores the ratio or
//                                   divides where it should multiply
//   aspect ratio, height-driven  -> catches a solver that only handles the
//                                   width branch
//   aspect ratio, ratio <= 0     -> catches a missing guard (division by zero,
//                                   or a negative size propagating)
//   Constrain, below/above/inside-> catches a clamp that is one-sided
//   UpdateMax/Min size           -> catches a "check" that mutates unconditionally
//
// The aspect-ratio cases are deliberately a *pair*: with only the width-driven
// one, an implementation that always divides would pass, and that is precisely
// the bug the second case exists to catch.

#include <cstdio>

#include "base/geometry/ng/size_t.h"
#include "core/components_ng/property/layout_constraint.h"

using namespace OHOS::Ace;
using namespace OHOS::Ace::NG;

static int failures = 0;

static void check(const char *what, double got, double want)
{
    const bool ok = (got == want);
    if (!ok) {
        failures++;
    }
    std::printf("  [%s] %-46s got %-10g want %g\n",
                ok ? "ok  " : "FAIL", what, got, want);
}

int main()
{
    std::printf("ace layout-engine probe\n");

    // --- aspect ratio, width-driven ----------------------------------------
    // ratio is width/height, so a 200-wide parent at ratio 2.0 implies height
    // 200/2 = 100.
    {
        LayoutConstraintF c;
        c.parentIdealSize.SetWidth(200.0f);
        c.ApplyAspectRatioToParentIdealSize(true, 2.0f);
        check("aspect (width-driven): height = 200 / 2",
              c.parentIdealSize.Height().value_or(-1.0f), 100.0f);
        check("aspect (width-driven): width untouched",
              c.parentIdealSize.Width().value_or(-1.0f), 200.0f);
    }

    // --- aspect ratio, height-driven ---------------------------------------
    // The other branch: a 50-high parent at ratio 2.0 implies width 50*2 = 100.
    // An implementation that always divided would give 25 here and fail.
    {
        LayoutConstraintF c;
        c.parentIdealSize.SetHeight(50.0f);
        c.ApplyAspectRatioToParentIdealSize(false, 2.0f);
        check("aspect (height-driven): width = 50 * 2",
              c.parentIdealSize.Width().value_or(-1.0f), 100.0f);
    }

    // --- aspect ratio, ratio <= 0 ------------------------------------------
    // Positive(ratio) must reject it; nothing should be written. Without this,
    // a ratio of 0 would divide and yield inf.
    {
        LayoutConstraintF c;
        c.parentIdealSize.SetWidth(200.0f);
        c.ApplyAspectRatioToParentIdealSize(true, 0.0f);
        check("aspect ratio 0: height stays unset",
              c.parentIdealSize.Height().has_value() ? 1.0 : 0.0, 0.0);
    }

    // --- Constrain: clamp from both sides ---------------------------------
    {
        LayoutConstraintF c;
        c.minSize = SizeF(20.0f, 30.0f);
        c.maxSize = SizeF(100.0f, 200.0f);

        const SizeF below = c.Constrain(SizeF(10.0f, 10.0f));
        check("Constrain below min: width -> 20", below.Width(), 20.0f);
        check("Constrain below min: height -> 30", below.Height(), 30.0f);

        const SizeF above = c.Constrain(SizeF(500.0f, 500.0f));
        check("Constrain above max: width -> 100", above.Width(), 100.0f);
        check("Constrain above max: height -> 200", above.Height(), 200.0f);

        const SizeF inside = c.Constrain(SizeF(50.0f, 60.0f));
        check("Constrain inside: unchanged width", inside.Width(), 50.0f);
        check("Constrain inside: unchanged height", inside.Height(), 60.0f);
    }

    // --- the "with check" mutators only move one way ----------------------
    // These return whether they changed anything, and callers use that to decide
    // whether to re-measure. A version that always returned true would cause
    // infinite measure loops; one that always mutated would widen a max that is
    // meant only to shrink.
    {
        LayoutConstraintF c;
        c.maxSize = SizeF(100.0f, 100.0f);
        check("UpdateMaxSizeWithCheck shrinking -> true",
              c.UpdateMaxSizeWithCheck(SizeF(80.0f, 80.0f)) ? 1.0 : 0.0, 1.0);
        check("UpdateMaxSizeWithCheck applied", c.maxSize.Width(), 80.0f);
        check("UpdateMaxSizeWithCheck growing -> false",
              c.UpdateMaxSizeWithCheck(SizeF(120.0f, 120.0f)) ? 1.0 : 0.0, 0.0);
        check("UpdateMaxSizeWithCheck did not grow", c.maxSize.Width(), 80.0f);
    }
    {
        LayoutConstraintF c;
        c.minSize = SizeF(10.0f, 10.0f);
        check("UpdateMinSizeWithCheck growing -> true",
              c.UpdateMinSizeWithCheck(SizeF(40.0f, 40.0f)) ? 1.0 : 0.0, 1.0);
        check("UpdateMinSizeWithCheck applied", c.minSize.Width(), 40.0f);
        check("UpdateMinSizeWithCheck shrinking -> false",
              c.UpdateMinSizeWithCheck(SizeF(5.0f, 5.0f)) ? 1.0 : 0.0, 0.0);
        check("UpdateMinSizeWithCheck did not shrink", c.minSize.Width(), 40.0f);
    }

    // --- percent reference reports whether it changed -----------------------
    {
        LayoutConstraintF c;
        check("UpdatePercentReference first time -> true",
              c.UpdatePercentReference(SizeF(300.0f, 400.0f)) ? 1.0 : 0.0, 1.0);
        check("UpdatePercentReference applied",
              c.percentReference.Width(), 300.0f);
        check("UpdatePercentReference same again -> false",
              c.UpdatePercentReference(SizeF(300.0f, 400.0f)) ? 1.0 : 0.0, 0.0);
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
