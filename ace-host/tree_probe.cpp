// Tree probe: run ArkUI's *real* linear-layout algorithm over a hand-built tree,
// then rasterise the resulting rectangles with a trivial software renderer.
//
// WHY THIS EXISTS
// ---------------
// ace_ng_layout_probe asserts the geometry of a single LayoutConstraint (the
// pure measure math). This probe asserts the *tree* behaviour: that the real
// LinearLayoutUtils::Measure/Layout walk a parent/children structure, size each
// node, and assign child offsets -- the part that turns a constraint solver into
// a layout engine.
//
// WHAT IS REAL AND WHAT IS A TEST DOUBLE
// --------------------------------------
// Real: the layout algorithm (frameworks/.../linear_layout/linear_layout_utils.cpp),
// LayoutProperty (layout/layout_property.cpp), GeometryNode and the constraint
// types. The probe links those from the ace engine, unchanged.
//
// Double: the LayoutWrapper. Upstream's own unit tests use the real
// LayoutWrapperNode, which is backed by a FrameNode, and a FrameNode drags in the
// engine's service core (Pattern, RenderContext, PipelineContext, ...). This probe
// does not need a FrameNode to exercise *layout*, so it implements the abstract
// LayoutWrapper interface directly. No layout arithmetic is reimplemented here:
// Measure/Layout delegate straight back into LinearLayoutUtils.
//
// The rasteriser is intentionally ours, not the engine's: the engine's paint path
// is the Skia/Rosen backend (absent on this host). The claim this probe makes is
// about *geometry*, so it draws that geometry itself and prints the buffer that
// results. Pixels are a rendering of ArkUI's layout output, not ArkUI's paint output.
//
// The scene plumbing (ProbeWrapper, LayoutScene, Flatten, Surface) lives in
// ace_scene.hpp and is shared with window_demo.cpp, which shows the same layout
// in a live window and re-runs it on resize.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ace_scene.hpp"

using namespace OHOS::Ace;
using namespace OHOS::Ace::NG;

static int failures = 0;

static void check(const char* what, double got, double want)
{
    const bool ok = (got == want);
    if (!ok) {
        failures++;
    }
    std::printf("  [%s] %-46s got %-10g want %g\n", ok ? "ok  " : "FAIL", what, got, want);
}

namespace {

constexpr int kW = 360;
constexpr int kH = 640;

} // namespace

int main(int argc, char* argv[])
{
    std::printf("ace layout-TREE probe\n");

    // --- build the tree -----------------------------------------------------
    // root(Column) -> a, b, row(Row) -> {c, d}, e
    constexpr uint32_t kRoot = 0x00e8e8f0u;
    constexpr uint32_t kA = 0x00e05c5cu;
    constexpr uint32_t kB = 0x005ce0a0u;
    constexpr uint32_t kRow = 0x00f0c020u;
    constexpr uint32_t kC = 0x006090f0u;
    constexpr uint32_t kD = 0x00b060e0u;
    constexpr uint32_t kE = 0x00f08050u;

    auto root = AceType::MakeRefPtr<ProbeWrapper>("root", true, ProbeWrapper::Kind::CONTAINER, SizeF(0, 0), kRoot);
    auto a = AceType::MakeRefPtr<ProbeWrapper>("a", false, ProbeWrapper::Kind::LEAF, SizeF(100, 50), kA);
    auto b = AceType::MakeRefPtr<ProbeWrapper>("b", false, ProbeWrapper::Kind::LEAF, SizeF(100, 50), kB);
    auto row = AceType::MakeRefPtr<ProbeWrapper>("row", false, ProbeWrapper::Kind::CONTAINER, SizeF(0, 0), kRow);
    auto c = AceType::MakeRefPtr<ProbeWrapper>("c", false, ProbeWrapper::Kind::LEAF, SizeF(80, 60), kC);
    auto d = AceType::MakeRefPtr<ProbeWrapper>("d", false, ProbeWrapper::Kind::LEAF, SizeF(80, 60), kD);
    auto e = AceType::MakeRefPtr<ProbeWrapper>("e", false, ProbeWrapper::Kind::LEAF, SizeF(100, 50), kE);

    row->AddChild(c);
    row->AddChild(d);
    root->AddChild(a);
    root->AddChild(b);
    root->AddChild(row);
    root->AddChild(e);

    // --- run the real layout engine ----------------------------------------
    LayoutScene(root, static_cast<float>(kW), static_cast<float>(kH));

    // --- assert the geometry ------------------------------------------------
    const auto rootSize = root->GetGeometryNode()->GetFrameSize();
    check("root width", rootSize.Width(), 360.0f);
    check("root height", rootSize.Height(), 640.0f);

    auto off = [](const RefPtr<ProbeWrapper>& n) { return n->GetGeometryNode()->GetMarginFrameOffset(); };
    check("a.y == 0 (first child)", off(a).GetY(), 0.0f);
    check("b.y == 50 (a height)", off(b).GetY(), 50.0f);
    check("row.y == 100", off(row).GetY(), 100.0f);
    check("e.y == 160 (100 + row height 60)", off(e).GetY(), 160.0f);
    check("c.x == 0 (row first child)", off(c).GetX(), 0.0f);
    check("d.x == 80 (c width)", off(d).GetX(), 80.0f);
    check("c.y == 0", off(c).GetY(), 0.0f);

    const auto rowSize = row->GetGeometryNode()->GetFrameSize();
    check("row width == 80+80 (measured from children)", rowSize.Width(), 160.0f);
    check("row height == 60 (max child height)", rowSize.Height(), 60.0f);

    // --- rasterise ----------------------------------------------------------
    const auto placed = acehost::Flatten(root);
    acehost::Surface surface(kW, kH, 0x00101018u);
    for (const auto& p : placed) {
        surface.FillRect(p.x, p.y, p.w, p.h, p.color);
    }
    const char* out = (argc > 1) ? argv[1] : "tree_layout.ppm";
    if (!surface.WritePpm(out)) {
        std::printf("  [FAIL] could not open %s for writing\n", out);
        failures++;
    }

    std::printf("  placed %zu rects, wrote %s (%dx%d)\n", placed.size(), out, kW, kH);
    for (const auto& p : placed) {
        std::printf("    %-6s x=%3d y=%3d w=%3d h=%3d\n", p.tag.c_str(), p.x, p.y, p.w, p.h);
    }

#ifdef ACE_TREE_MUTANT
    // Negative control: the same probe with one deliberately impossible
    // expectation. If this build still printed PASS, the checks would not be
    // real comparisons and the PASS above would be worthless.
    std::printf("  (mutant build)\n");
    check("MUTANT control: deliberately wrong expectation", 1.0, 2.0);
#endif

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
