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

#include <cstdint>
#include <cstdio>
#include <list>
#include <optional>
#include <string>
#include <vector>

#include "core/components_ng/layout/layout_wrapper.h"
#include "core/components_ng/layout/layout_property.h"
#include "core/components_ng/pattern/linear_layout/linear_layout_property.h"
#include "core/components_ng/pattern/linear_layout/linear_layout_utils.h"
#include "core/components_ng/base/geometry_node.h"

namespace OHOS::Ace::NG {

// A leaf is fixed-size test content. A container delegates to the real
// LinearLayoutUtils. Both are node kinds the engine itself distinguishes.
class ProbeWrapper final : public LayoutWrapper {
    DECLARE_ACE_TYPE(ProbeWrapper, LayoutWrapper);

public:
    enum class Kind { CONTAINER, LEAF };

    ProbeWrapper(std::string tag, bool vertical, Kind kind, SizeF leafSize, uint32_t color)
        : LayoutWrapper(WeakPtr<FrameNode>()), tag_(std::move(tag)), vertical_(vertical), kind_(kind),
          leafSize_(leafSize), color_(color)
    {
        geometryNode_ = AceType::MakeRefPtr<GeometryNode>();
        layoutProperty_ = AceType::MakeRefPtr<LinearLayoutProperty>(vertical_);
    }

    void AddChild(const RefPtr<ProbeWrapper>& child)
    {
        children_.emplace_back(child);
        concreteChildren_.emplace_back(child);
    }

    const std::vector<RefPtr<ProbeWrapper>>& Children() const
    {
        return concreteChildren_;
    }
    uint32_t Color() const
    {
        return color_;
    }
    bool IsContainer() const
    {
        return kind_ == Kind::CONTAINER;
    }
    const std::string& Tag() const
    {
        return tag_;
    }

    // ---- layout entry points -------------------------------------------------
    void Measure(const std::optional<LayoutConstraintF>& parentConstraint) override
    {
        if (parentConstraint) {
            layoutProperty_->UpdateLayoutConstraint(*parentConstraint);
            layoutProperty_->UpdateContentConstraint();
        }
        if (kind_ == Kind::LEAF) {
            // Fixed-size content: this is the "leaf measured its content" answer.
            geometryNode_->SetFrameSize(leafSize_);
            return;
        }
        LinearLayoutUtils::Measure(this, vertical_);
    }

    void Layout() override
    {
        for (const auto& child : children_) {
            child->Layout();
        }
        if (kind_ == Kind::CONTAINER) {
            LinearLayoutUtils::Layout(this, vertical_, FlexAlign::FLEX_START, FlexAlign::FLEX_START);
        }
    }

    // ---- LayoutWrapper interface --------------------------------------------
    const RefPtr<LayoutAlgorithmWrapper>& GetLayoutAlgorithm(bool needReset = false) override
    {
        (void)needReset;
        return layoutAlgorithm_;
    }

    int32_t GetTotalChildCount() const override
    {
        return static_cast<int32_t>(children_.size());
    }

    const RefPtr<GeometryNode>& GetGeometryNode() const override
    {
        return geometryNode_;
    }

    const RefPtr<LayoutProperty>& GetLayoutProperty() const override
    {
        return layoutProperty_;
    }

    RefPtr<LayoutWrapper> GetOrCreateChildByIndex(uint32_t index, bool addToRenderTree = true,
        bool isCache = false) override
    {
        (void)addToRenderTree;
        (void)isCache;
        return GetChildByIndex(index);
    }

    RefPtr<LayoutWrapper> GetChildByIndex(uint32_t index, bool isCache = false) override
    {
        (void)isCache;
        if (index >= children_.size()) {
            return nullptr;
        }
        auto it = children_.begin();
        std::advance(it, index);
        return *it;
    }

    ChildrenListWithGuard GetAllChildrenWithBuild(bool addToRenderTree = true) override
    {
        (void)addToRenderTree;
        return ChildrenListWithGuard(children_, lock_);
    }

    void RemoveChildInRenderTree(uint32_t index) override
    {
        (void)index;
    }
    void RemoveAllChildInRenderTree() override {}
    void SetActiveChildRange(int32_t start, int32_t end, int32_t cacheStart = 0, int32_t cacheEnd = 0,
        bool showCached = false) override
    {
        (void)start;
        (void)end;
        (void)cacheStart;
        (void)cacheEnd;
        (void)showCached;
    }
    void RecycleItemsByIndex(int32_t start, int32_t end) override
    {
        (void)start;
        (void)end;
    }
    const std::string& GetHostTag() const override
    {
        return tag_;
    }
    bool IsActive() const override
    {
        return true;
    }
    void SetActive(bool active = true, bool needRebuildRenderContext = false) override
    {
        (void)active;
        (void)needRebuildRenderContext;
    }
    void SetCacheCount(int32_t cacheCount = 0, const std::optional<LayoutConstraintF>& itemConstraint = std::nullopt) override
    {
        (void)cacheCount;
        (void)itemConstraint;
    }
    float GetBaselineDistance() const override
    {
        return 0.0f;
    }
    bool CheckNeedForceMeasureAndLayout() override
    {
        return false;
    }

private:
    std::string tag_;
    bool vertical_ = false;
    Kind kind_ = Kind::LEAF;
    SizeF leafSize_;
    uint32_t color_ = 0;
    RefPtr<GeometryNode> geometryNode_;
    RefPtr<LayoutProperty> layoutProperty_;
    RefPtr<LayoutAlgorithmWrapper> layoutAlgorithm_;
    std::list<RefPtr<LayoutWrapper>> children_;
    std::vector<RefPtr<ProbeWrapper>> concreteChildren_;
    RecursiveLock lock_;
};

} // namespace OHOS::Ace::NG

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

// --- software rasteriser (ours, not the engine's paint path) ----------------
namespace {

constexpr int kW = 360;
constexpr int kH = 640;
std::vector<uint32_t> gPixels(kW * kH, 0x00101018u);

struct Placed {
    int x, y, w, h;
    uint32_t color;
    std::string tag;
};
std::vector<Placed> gPlaced;

void Blit(const RefPtr<ProbeWrapper>& node, int parentX, int parentY)
{
    const auto offset = node->GetGeometryNode()->GetMarginFrameOffset();
    const auto size = node->GetGeometryNode()->GetFrameSize();
    const int x = parentX + static_cast<int>(offset.GetX());
    const int y = parentY + static_cast<int>(offset.GetY());
    const int w = static_cast<int>(size.Width());
    const int h = static_cast<int>(size.Height());
    gPlaced.push_back({ x, y, w, h, node->Color(), node->Tag() });
    for (const auto& child : node->Children()) {
        Blit(child, x, y);
    }
}

void FillRect(int x, int y, int w, int h, uint32_t color)
{
    for (int j = y + 1; j < y + h - 1; ++j) {
        for (int i = x + 1; i < x + w - 1; ++i) {
            if (i >= 0 && i < kW && j >= 0 && j < kH) {
                gPixels[static_cast<size_t>(j) * kW + i] = color;
            }
        }
    }
}

void WritePpm(const char* path)
{
    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::printf("  [FAIL] could not open %s for writing\n", path);
        failures++;
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", kW, kH);
    for (uint32_t px : gPixels) {
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((px >> 16) & 0xffu),
            static_cast<unsigned char>((px >> 8) & 0xffu),
            static_cast<unsigned char>(px & 0xffu),
        };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

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
    LayoutConstraintF rc;
    rc.selfIdealSize.SetWidth(360.0f);
    rc.selfIdealSize.SetHeight(640.0f);
    rc.maxSize = SizeF(360.0f, 640.0f);
    rc.minSize = SizeF(0.0f, 0.0f);
    rc.percentReference = SizeF(360.0f, 640.0f);
    root->Measure(rc);
    root->Layout();

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
    Blit(root, 0, 0);
    for (const auto& p : gPlaced) {
        FillRect(p.x, p.y, p.w, p.h, p.color);
    }
    const char* out = (argc > 1) ? argv[1] : "tree_layout.ppm";
    WritePpm(out);

    std::printf("  placed %zu rects, wrote %s (%dx%d)\n", gPlaced.size(), out, kW, kH);
    for (const auto& p : gPlaced) {
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
