// Capturing the draw operations that ace_engine's paint code emits.
//
// WHY THIS EXISTS
// ---------------
// Stage 2a drew geometry that the engine's *layout* produced. Stage 2b asks the
// other half: what does the engine's *paint* code draw? In the real build, a
// pattern's NodePaintMethod / Modifier turns paint properties into calls on a
// Rosen canvas. Under ACE_UNITTEST that canvas is Testing::TestingCanvas
// (aliased at frameworks/core/components_ng/render/drawing_mock.h:62, defined in
// test/mock/frameworks/core/rosen/testing_canvas.h), whose draw methods are
// virtual no-ops. Overriding them is
// therefore how a host renderer observes exactly what the engine asked to be
// drawn -- the draw commands are the engine's, the rasterisation is ours.
//
// WHAT CAN AND CANNOT BE RECOVERED
// --------------------------------
// Geometry (endpoints, rects) is recoverable, because it is passed to the draw
// call. Style is NOT: the engine's pen/brush types are stateless mocks
// (TestingPen::SetColor/SetWidth are no-ops, testing_pen.h:48-53), so the colour
// or width a painter set on a pen never reaches the canvas. A host backend must
// take style from the property values the engine was configured with. That is a
// property of the mock renderer, not a choice made here, and it is why the paint
// probe asserts *geometry* (which the engine computes) and supplies colour.

#ifndef ACE_HOST_PAINT_CAPTURE_HPP
#define ACE_HOST_PAINT_CAPTURE_HPP

#include <cstdint>
#include <vector>

#include "core/components_ng/base/modifier.h"
#include "core/components_ng/render/drawing.h"

namespace acehost {

struct LineOp {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct RectOp {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// A canvas that records rather than rasterises. It only overrides the methods
// the probes actually exercise; the rest stay the mock's no-ops.
class RecordingCanvas final : public OHOS::Ace::RSCanvas {
public:
    void DrawLine(const OHOS::Ace::Testing::TestingPoint& startPt,
        const OHOS::Ace::Testing::TestingPoint& endPt) override
    {
        lines.push_back({ startPt.GetX(), startPt.GetY(), endPt.GetX(), endPt.GetY() });
    }

    void DrawRect(const OHOS::Ace::Testing::TestingRect& rect) override
    {
        rects.push_back({ rect.GetLeft(), rect.GetTop(), rect.GetRight(), rect.GetBottom() });
    }

    std::vector<LineOp> lines;
    std::vector<RectOp> rects;
};

} // namespace acehost

#endif // ACE_HOST_PAINT_CAPTURE_HPP
