// Paint probe: make ace_engine's *paint* code emit a draw command, capture it,
// and rasterise it. This is the other half of Stage 2.
//
// WHY THIS EXISTS
// ---------------
// tree_probe proves the engine computes geometry. This probe proves the engine's
// drawing code can be driven off-pipeline and observed: it builds a real
// DividerModifier, hands it a DrawingContext around a RecordingCanvas, and calls
// ContentModifier::Draw (base/modifier.cpp), which calls DividerModifier::onDraw,
// which builds a DividerPainter and calls DividerPainter::DrawLine
// (render/divider_painter.cpp). That function computes the line's endpoints from
// the stroke width, line-cap style, orientation and length -- engine arithmetic --
// and emits canvas.DrawLine. Nothing here reimplements that arithmetic.
//
// WHAT IS REAL AND WHAT IS OURS
// -----------------------------
// Real: DividerModifier, DividerPainter and the whole geometry of the line.
// Ours: the rasteriser (the engine's paint path ends in Skia/Rosen, absent here)
// and the colour, because the mock pen that carries style is stateless (see
// paint_capture.hpp). The assertions below are on the engine's computed
// endpoints, so they are assertions about engine behaviour.
//
// The shape of the checks: a SQUARE cap makes DividerPainter inset the endpoints
// by half the stroke width, a BUTT cap does not, and vertical swaps the axes.
// Three configurations therefore give three distinct, predictable segments.
//
// One engine subtlety is asserted too: DividerModifier::onDraw coerces BUTT to
// SQUARE while `strokeWidthLimitation_` is on (its default; divider_modifier.cpp
// lines 43-45). So a real BUTT segment only appears once the limitation is turned
// off, and the default-but-limited config must reproduce the SQUARE geometry.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ace_scene.hpp"
#include "core/components_ng/pattern/divider/divider_modifier.h"
#include "paint_capture.hpp"

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

// Drive the real engine paint path for one divider configuration and return the
// ops the engine emitted.
static std::vector<acehost::LineOp> PaintDivider(float strokeWidth, float length, bool vertical, LineCap cap,
    const OffsetF& offset, bool strokeWidthLimitation = true)
{
    DividerModifier modifier;
    modifier.SetStrokeWidth(strokeWidth);
    modifier.SetDividerLength(length);
    modifier.SetVertical(vertical);
    modifier.SetLineCap(cap);
    modifier.SetOffset(offset);
    modifier.SetStrokeWidthLimitation(strokeWidthLimitation);
    modifier.SetColor(LinearColor(0xffff0000));

    acehost::RecordingCanvas canvas;
    DrawingContext context { canvas, 240.0f, 240.0f };
    modifier.Draw(context); // ContentModifier::Draw -> DividerModifier::onDraw -> DividerPainter::DrawLine
    return canvas.lines;
}

// Rasterise a captured line as a thick, square-ended stroke. Thickness is passed
// in because, as paint_capture.hpp explains, the mock pen does not carry it.
static void StampLine(acehost::Surface& surface, const acehost::LineOp& line, uint32_t color, int thickness)
{
    int x0 = static_cast<int>(line.x0);
    int y0 = static_cast<int>(line.y0);
    const int x1 = static_cast<int>(line.x1);
    const int y1 = static_cast<int>(line.y1);
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    const int lo = -(thickness / 2);
    const int hi = thickness + lo;
    for (;;) {
        for (int oy = lo; oy < hi; ++oy) {
            for (int ox = lo; ox < hi; ++ox) {
                const int px = x0 + ox;
                const int py = y0 + oy;
                if (px >= 0 && px < surface.Width() && py >= 0 && py < surface.Height()) {
                    surface.Data()[static_cast<size_t>(py) * surface.Width() + px] = color;
                }
            }
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

int main(int argc, char* argv[])
{
    std::printf("ace paint probe (engine draw commands -> our raster)\n");

    // --- engine emits the draw commands; we capture geometry ---------------
    const auto square = PaintDivider(4.0f, 200.0f, false, LineCap::SQUARE, OffsetF(10.0f, 20.0f));
    const auto butt = PaintDivider(4.0f, 200.0f, false, LineCap::BUTT, OffsetF(10.0f, 20.0f), false);
    const auto coerced = PaintDivider(4.0f, 200.0f, false, LineCap::BUTT, OffsetF(10.0f, 20.0f));
    const auto vertical = PaintDivider(4.0f, 200.0f, true, LineCap::SQUARE, OffsetF(10.0f, 20.0f));

    check("one DrawLine per divider (square)", static_cast<double>(square.size()), 1.0);
    check("one DrawLine per divider (butt)", static_cast<double>(butt.size()), 1.0);
    check("one DrawLine per divider (coerced)", static_cast<double>(coerced.size()), 1.0);
    check("one DrawLine per divider (vertical)", static_cast<double>(vertical.size()), 1.0);

    if (!square.empty()) {
        check("square-cap start.x (inset by width/2)", square[0].x0, 12.0f);
        check("square-cap start.y", square[0].y0, 22.0f);
        check("square-cap end.x (length - width)", square[0].x1, 208.0f);
        check("square-cap end.y", square[0].y1, 22.0f);
    }
    if (!butt.empty()) {
        check("butt-cap start.x (at offset)", butt[0].x0, 10.0f);
        check("butt-cap end.x (offset + length)", butt[0].x1, 210.0f);
        check("butt-cap start.y", butt[0].y0, 22.0f);
    }
    if (!coerced.empty()) {
        // strokeWidthLimitation_ is on by default, so onDraw turns BUTT into SQUARE.
        check("coerced BUTT->SQUARE start.x", coerced[0].x0, 12.0f);
        check("coerced BUTT->SQUARE end.x", coerced[0].x1, 208.0f);
    }
    if (!vertical.empty()) {
        check("vertical start.x == end.x", vertical[0].x0, vertical[0].x1);
        check("vertical end.y (length - width)", vertical[0].y1, 218.0f);
    }

    // --- rasterise the horizontal square-cap divider -----------------------
    constexpr int kW = 240;
    constexpr int kH = 240;
    constexpr uint32_t kRed = 0x00ff0000u;
    acehost::Surface surface(kW, kH, 0x00101018u);
    if (!square.empty()) {
        StampLine(surface, square[0], kRed, 4);
    }

    // The SQUARE cap insets the endpoints, but the cap itself extends half the
    // stroke width again, so the painted span is exactly [offset.x, offset.x +
    // length] = [10, 210) -- the same span a BUTT cap would paint. That is the
    // engine's cap compensation, visible in the pixels.
    check("painted left edge", surface.At(10, 22) == kRed ? 1 : 0, 1.0);
    check("painted right edge", surface.At(209, 22) == kRed ? 1 : 0, 1.0);
    check("just left of the span (bg)", surface.At(9, 22) == kRed ? 1 : 0, 0.0);
    check("just right of the span (bg)", surface.At(210, 22) == kRed ? 1 : 0, 0.0);
    check("top row of stroke", surface.At(100, 20) == kRed ? 1 : 0, 1.0);
    check("bottom row of stroke", surface.At(100, 23) == kRed ? 1 : 0, 1.0);
    check("above the stroke (bg)", surface.At(100, 19) == kRed ? 1 : 0, 0.0);
    check("below the stroke (bg)", surface.At(100, 24) == kRed ? 1 : 0, 0.0);

    const char* out = (argc > 1) ? argv[1] : "paint_probe.ppm";
    if (!surface.WritePpm(out)) {
        std::printf("  [FAIL] could not open %s for writing\n", out);
        failures++;
    }
    std::printf("  captured %zu+%zu+%zu+%zu draw op(s), wrote %s (%dx%d)\n", square.size(), butt.size(),
        coerced.size(), vertical.size(), out, kW, kH);

#ifdef ACE_PAINT_MUTANT
    // Negative control: the same probe with one deliberately impossible
    // expectation. If this build still passed, the checks above would not be
    // real comparisons.
    std::printf("  (mutant build)\n");
    check("MUTANT control: deliberately wrong expectation", 1.0, 2.0);
#endif

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
