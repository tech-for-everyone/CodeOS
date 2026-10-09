// Boundary probe: a second engine painter, to show the capture model is not
// special-cased to one shape. Divider emitted a single DrawLine; this one emits a
// rect and eight short corner lines, so it adds DrawRect to the recorded
// vocabulary and a genuine multi-op capture.
//
// WHY THIS EXISTS
// ---------------
// Stage 2b's first proof (paint_probe.cpp) captured DividerPainter::DrawLine. A
// single painter could be a fluke -- maybe only lines are observable, or only one
// code path is reachable off-pipeline. DebugBoundaryPainter is a second, unrelated
// member of the engine's paint code: it computes a boundary rect and corner marks
// from the content/frame sizes it was given and emits them on the canvas. If its
// geometry comes back exactly, the capture approach generalises.
//
// WHAT IS REAL AND WHAT IS OURS
// -----------------------------
// Real: DebugBoundaryPainter and every coordinate it computes (the rect edges from
// frameMarginSize_ minus the half-stroke inset, the corner marks 8px long, the four
// margin rects).
// Ours: the rasteriser, and the three colours. The mock pen/brush are stateless
// (see paint_capture.hpp), so the engine's SetColor calls never reach the canvas;
// the probe supplies colours to make the captured ops visible and samplable.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ace_scene.hpp"
#include "core/components_ng/render/debug_boundary_painter.h"
#include "paint_capture.hpp"
#include "paint_raster.hpp"

using namespace OHOS::Ace;
using namespace OHOS::Ace::NG;

static int failures = 0;

static void check(const char* what, double got, double want)
{
    const bool ok = (got == want);
    if (!ok) {
        failures++;
    }
    std::printf("  [%s] %-46s got %-8g want %g\n", ok ? "ok  " : "FAIL", what, got, want);
}

// The canvas holds the surfaces at (0,0); the painter can be positioned in it.
static constexpr float kW = 220.0f;   // frame margin width
static constexpr float kH = 120.0f;   // frame margin height
static constexpr float kContentW = 200.0f;
static constexpr float kContentH = 100.0f;
static constexpr float kContentX = 30.0f;  // content offset inside the frame
static constexpr float kContentY = 40.0f;

// Drive the real engine painter and return the ops it emitted.
static acehost::RecordingCanvas PaintBoundary()
{
    DebugBoundaryPainter painter(SizeF(kContentW, kContentH), SizeF(kW, kH));
    painter.SetFrameOffset(OffsetF(kContentX, kContentY));
    painter.SetPaddingOffset(OffsetF(0.0f, 0.0f));

    acehost::RecordingCanvas canvas;
    // Passing the content offset makes marginOffset zero, i.e. the boundaries start
    // at the canvas origin. That is the painter's own "canvas is translated" model.
    painter.DrawDebugBoundaries(canvas, OffsetF(kContentX, kContentY));
    return canvas;
}

int main(int argc, char* argv[])
{
    std::printf("ace boundary probe (second engine painter -> our raster)\n");

    // --- engine emits the draw commands; we capture geometry ---------------
    const auto canvas = PaintBoundary();
    const auto& rects = canvas.rects;
    const auto& lines = canvas.lines;

    check("one rect per boundary", rects.size() >= 1 ? 1.0 : 0.0, 1.0);
    check("rect count (boundary + 4 margins)", static_cast<double>(rects.size()), 5.0);
    check("line count (8 corner marks)", static_cast<double>(lines.size()), 8.0);

    if (rects.size() >= 5) {
        // PaintDebugBoundary: [startPoint, frame - 0.5] = [0,0 .. 219.5,119.5].
        const auto& boundary = rects[0];
        check("boundary left", boundary.left, 0.0f);
        check("boundary top", boundary.top, 0.0f);
        check("boundary right (frame - half stroke)", boundary.right, 219.5f);
        check("boundary bottom (frame - half stroke)", boundary.bottom, 119.5f);
        // PaintDebugMargin draws top, bottom, left, right (in that order).
        const auto& top = rects[1];
        const auto& bottom = rects[2];
        const auto& left = rects[3];
        const auto& right = rects[4];
        check("top margin has zero height", top.bottom - top.top, 0.0f);
        check("bottom margin top", bottom.top, 100.0f);
        check("bottom margin bottom", bottom.bottom, 120.0f);
        check("left margin has zero width", left.right - left.left, 0.0f);
        check("right margin left", right.left, 200.0f);
        check("right margin right", right.right, 220.0f);
    }

    if (lines.size() >= 8) {
        // Corner mark lengths/positions are frame-relative: 8px, ending at the
        // half-stroke-inset corner.
        check("corner 1 start", lines[0].x0, 0.0f);
        check("corner 1 end (8px long)", lines[0].x1, 8.0f);
        check("corner 4 x (right - half stroke)", lines[3].x0, 219.5f);
        check("corner 4 end y (8px down)", lines[3].y1, 8.0f);
        check("corner 7 start (right - 8 - half)", lines[6].x0, 211.5f);
        check("corner 8 start y (bottom - 8)", lines[7].y0, 112.0f);
        check("corner 8 end y (bottom)", lines[7].y1, 120.0f);
    }

    // --- rasterise the captured commands -----------------------------------
    constexpr int kSurfaceW = 240;
    constexpr int kSurfaceH = 240;
    constexpr uint32_t kBackground = 0x00141822u;
    constexpr uint32_t kBoundary = 0x00fa2a2du;  // BOUNDARY_COLOR
    constexpr uint32_t kCorner = 0x00007dffu;    // BOUNDARY_CORNER_COLOR
    constexpr uint32_t kMargin = 0x00ff00aau;    // BOUNDARY_MARGIN_COLOR (alpha dropped)
    acehost::Surface surface(kSurfaceW, kSurfaceH, kBackground);

    if (rects.size() >= 1 && lines.size() >= 8) {
        // Engine order: boundary outline, then corners, then margin fills. The
        // outline is stroked exactly on the captured rect edges (DrawRect with a
        // pen strokes the rect path), so the sampled edge is the engine's edge.
        const auto& b = rects[0];
        StampLine(surface, { b.left, b.top, b.right, b.top }, kBoundary, 1);
        StampLine(surface, { b.right, b.top, b.right, b.bottom }, kBoundary, 1);
        StampLine(surface, { b.right, b.bottom, b.left, b.bottom }, kBoundary, 1);
        StampLine(surface, { b.left, b.bottom, b.left, b.top }, kBoundary, 1);
        for (const auto& line : lines) {
            StampLine(surface, line, kCorner, 1);
        }
        // The four margin rects; the zero-area ones draw nothing.
        for (size_t i = 1; i < rects.size(); ++i) {
            const auto& r = rects[i];
            surface.FillRect(static_cast<int>(r.left), static_cast<int>(r.top),
                static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top), kMargin);
        }
    }

    check("top boundary edge", surface.At(100, 0) == kBoundary ? 1 : 0, 1.0);
    check("left boundary edge", surface.At(0, 60) == kBoundary ? 1 : 0, 1.0);
    check("bottom boundary edge", surface.At(100, 119) == kBoundary ? 1 : 0, 1.0);
    check("bottom margin fill", surface.At(100, 110) == kMargin ? 1 : 0, 1.0);
    check("right margin fill", surface.At(210, 50) == kMargin ? 1 : 0, 1.0);
    check("inside the boundary (bg)", surface.At(100, 60) == kBackground ? 1 : 0, 1.0);
    check("just inside the top edge (bg)", surface.At(100, 1) == kBackground ? 1 : 0, 1.0);
    check("top-left corner mark", surface.At(4, 0) == kCorner ? 1 : 0, 1.0);

    const char* out = (argc > 1) ? argv[1] : "boundary_probe.ppm";
    if (!surface.WritePpm(out)) {
        std::printf("  [FAIL] could not open %s for writing\n", out);
        failures++;
    }
    std::printf("  captured %zu rect(s) + %zu line(s), wrote %s (%dx%d)\n", rects.size(), lines.size(), out,
        kSurfaceW, kSurfaceH);

#ifdef ACE_BOUNDARY_MUTANT
    // Negative control: the same probe with one deliberately impossible
    // expectation. If this build still passed, the checks above would not be real
    // comparisons.
    std::printf("  (mutant build)\n");
    check("MUTANT control: deliberately wrong expectation", 1.0, 2.0);
#endif

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
