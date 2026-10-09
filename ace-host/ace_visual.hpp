// The shared "engine layout + engine paint -> pixels" scene for the ace-host
// demos.
//
// WHY THIS EXISTS
// ---------------
// window_demo.cpp (the live X11 window) and gnustep_demo.mm (the GNUstep view)
// must show the *same* thing: an ArkUI tree laid out by the engine, with one node
// painted by the engine's own paint code. Keeping that in one header means the
// two shells cannot drift into showing different content, and the pixel
// assertions in controls.py measure one definition, not two.
//
// WHAT IS REAL AND WHAT IS OURS
// -----------------------------
// * The layout is the engine's: `LayoutScene` runs `LinearLayoutUtils` over the
//   `LayoutWrapper` test double in ace_scene.hpp.
// * The divider's draw command is the engine's: `RenderVisualScene` drives a real
//   `DividerModifier`/`DividerPainter` and captures the `DrawLine` it emits with
//   `acehost::RecordingCanvas`.
// * The rasteriser and the colour are ours: the engine's paint path ends in
//   Rosen/Skia, absent here, and the mock pen is stateless (see paint_capture.hpp),
//   so style cannot come from the canvas.

#ifndef ACE_HOST_ACE_VISUAL_HPP
#define ACE_HOST_ACE_VISUAL_HPP

#include <cstdint>

#include "ace_scene.hpp"
#include "core/components/common/layout/constants.h" // OHOS::Ace::LineCap
#include "core/components/common/properties/color.h" // OHOS::Ace::LinearColor
#include "core/components_ng/pattern/divider/divider_modifier.h"
#include "paint_capture.hpp"
#include "paint_raster.hpp"

namespace acehost {

// The palette and divider configuration. Both are inputs to the scene, not
// outputs of the engine: the engine computes *where* the divider goes, we choose
// what it looks like (see paint_capture.hpp for why).
inline constexpr uint32_t kVisualBackground = 0x00141822u;
inline constexpr uint32_t kVisualDividerColor = 0x00ffd24aU;
inline constexpr float kVisualDividerStroke = 4.0f;
inline constexpr float kVisualDividerWidthFraction = 0.8f;

// The nodes the shells need to introspect after a layout (for printing the
// engine-computed sizes). `root` is the Column; `pct` is the half-width leaf;
// `div` is the node painted by the engine's DividerModifier.
struct VisualScene {
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> root;
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> a;
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> b;
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> pct;
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> div;
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> row;
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> c;
    OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper> d;
};

// A scene whose layout visibly depends on the viewport size: the column centres
// its children, and `pct` is measured as a fraction of the width the engine hands
// down, so both position and size change when the viewport does.
inline VisualScene BuildVisualScene()
{
    namespace NG = OHOS::Ace::NG;
    VisualScene s;
    s.root = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "root", true, NG::ProbeWrapper::Kind::CONTAINER, NG::SizeF(0, 0), kVisualBackground,
        OHOS::Ace::FlexAlign::CENTER, OHOS::Ace::FlexAlign::CENTER);
    s.a = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "a", false, NG::ProbeWrapper::Kind::LEAF, NG::SizeF(100, 50), 0x00e05c5cu);
    s.b = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "b", false, NG::ProbeWrapper::Kind::LEAF, NG::SizeF(160, 40), 0x005ce0a0u);
    // A unique colour for the proportional node, so the resize control can count
    // exactly its pixels and check the count tracks the width the engine gave it.
    s.pct = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "pct", false, NG::ProbeWrapper::Kind::LEAF, NG::SizeF(0, 30), 0x00ff2d95u);
    s.pct->SetPercentWidth(0.5f);
    // A divider whose length is a fraction of the parent width. It is not filled
    // by the raster: RenderVisualScene paints it with a real DividerModifier
    // driven from this node's engine-computed rectangle, so its pixels are the
    // engine's draw command, not ours.
    s.div = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "div", false, NG::ProbeWrapper::Kind::LEAF, NG::SizeF(0, 4), kVisualDividerColor);
    s.div->SetPercentWidth(kVisualDividerWidthFraction);
    s.row = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "row", false, NG::ProbeWrapper::Kind::CONTAINER, NG::SizeF(0, 0), 0x00f0c020u,
        OHOS::Ace::FlexAlign::CENTER, OHOS::Ace::FlexAlign::CENTER);
    s.c = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "c", false, NG::ProbeWrapper::Kind::LEAF, NG::SizeF(80, 60), 0x006090f0u);
    s.d = OHOS::Ace::AceType::MakeRefPtr<NG::ProbeWrapper>(
        "d", false, NG::ProbeWrapper::Kind::LEAF, NG::SizeF(80, 60), 0x00b060e0u);
    s.row->AddChild(s.c);
    s.row->AddChild(s.d);
    s.root->AddChild(s.a);
    s.root->AddChild(s.b);
    s.root->AddChild(s.pct);
    s.root->AddChild(s.div);
    s.root->AddChild(s.row);
    return s;
}

// Lay the engine tree out at w x h, rasterise it, and draw the engine-painted
// divider over it. The surface is sized and cleared here, so callers pass a
// Surface however they like and get back a fresh frame.
inline void RenderVisualScene(VisualScene& scene, int w, int h, Surface& surface)
{
    namespace NG = OHOS::Ace::NG;
    NG::LayoutScene(scene.root, static_cast<float>(w), static_cast<float>(h));

    surface = Surface(w, h, kVisualBackground);
    const auto rects = Flatten(scene.root);
    for (const auto& p : rects) {
        if (p.tag == "div") {
            continue; // painted by the engine's paint code below
        }
        surface.FillRect(p.x, p.y, p.w, p.h, p.color);
    }

    // Engine paint: drive a real DividerModifier from the node's engine-computed
    // rectangle, capture the DrawLine it emits, and rasterise that. The line
    // endpoints are DividerPainter's arithmetic; only the colour and the raster
    // are ours (the mock pen is stateless -- see paint_capture.hpp).
    for (const auto& p : rects) {
        if (p.tag != "div") {
            continue;
        }
        NG::DividerModifier divider;
        divider.SetStrokeWidth(kVisualDividerStroke);
        divider.SetDividerLength(static_cast<float>(p.w));
        divider.SetVertical(false);
        divider.SetLineCap(OHOS::Ace::LineCap::SQUARE);
        divider.SetOffset(NG::OffsetF(static_cast<float>(p.x), static_cast<float>(p.y)));
        divider.SetColor(OHOS::Ace::LinearColor(0xff000000u | kVisualDividerColor));
        RecordingCanvas canvas;
        NG::DrawingContext context { canvas, static_cast<float>(w), static_cast<float>(h) };
        divider.Draw(context);
        for (const auto& line : canvas.lines) {
            StampLine(surface, line, kVisualDividerColor, static_cast<int>(kVisualDividerStroke));
        }
    }
}

} // namespace acehost

#endif // ACE_HOST_ACE_VISUAL_HPP
