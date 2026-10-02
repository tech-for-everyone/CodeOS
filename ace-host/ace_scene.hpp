// Shared scene plumbing for the ace-host demos.
//
// Three things live here, and they are deliberately separate from the engine:
//
//   1. ProbeWrapper -- a LayoutWrapper *test double*.  Upstream backs a
//      LayoutWrapper with a FrameNode; a FrameNode drags in the engine's whole
//      service/render core (Pattern, RenderContext, PipelineContext).  Layout
//      does not need any of that, so this implements the abstract interface
//      directly and delegates back into the real LinearLayoutUtils.  No layout
//      arithmetic is reimplemented -- the engine does the measuring and placing.
//
//   2. The scene builder / constraint runner -- builds a small component tree
//      and runs the real algorithm on it at a given viewport size.
//
//   3. Surface -- a trivial software rasteriser.  The engine's paint path is the
//      Skia/Rosen backend, absent on this host, so the geometry the engine
//      produces is drawn here instead.  Pixels are a rendering of ArkUI's layout
//      output, not ArkUI's paint output.
//
// tree_probe.cpp asserts the geometry this produces; window_demo.cpp shows it in
// a live X11 window and re-runs the layout on every resize.

#ifndef ACE_HOST_ACE_SCENE_HPP
#define ACE_HOST_ACE_SCENE_HPP

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <vector>

#include "core/components_ng/base/geometry_node.h"
#include "core/components_ng/layout/layout_property.h"
#include "core/components_ng/layout/layout_wrapper.h"
#include "core/components_ng/pattern/linear_layout/linear_layout_property.h"
#include "core/components_ng/pattern/linear_layout/linear_layout_utils.h"

namespace OHOS::Ace::NG {

class ProbeWrapper final : public LayoutWrapper {
    DECLARE_ACE_TYPE(ProbeWrapper, LayoutWrapper);

public:
    enum class Kind { CONTAINER, LEAF };

    ProbeWrapper(std::string tag, bool vertical, Kind kind, SizeF leafSize, uint32_t color,
        FlexAlign crossAlign = FlexAlign::FLEX_START, FlexAlign mainAlign = FlexAlign::FLEX_START)
        : LayoutWrapper(WeakPtr<FrameNode>()), tag_(std::move(tag)), vertical_(vertical), kind_(kind),
          leafSize_(leafSize), color_(color), crossAlign_(crossAlign), mainAlign_(mainAlign)
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

    // A LEAF whose width is a fraction of the parent's content width.  This is
    // still "the leaf measured its content": the fraction is test content, but
    // the parent content size it is measured against (percentReference) is
    // computed by the engine and propagated by the engine's constraint code.
    void SetPercentWidth(float fraction)
    {
        percentWidth_ = fraction;
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
            SizeF size = leafSize_;
            if (percentWidth_ > 0.0f && parentConstraint) {
                size.SetWidth(parentConstraint->percentReference.Width() * percentWidth_);
            }
            geometryNode_->SetFrameSize(size);
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
            LinearLayoutUtils::Layout(this, vertical_, crossAlign_, mainAlign_);
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
    float percentWidth_ = 0.0f;
    FlexAlign crossAlign_ = FlexAlign::FLEX_START;
    FlexAlign mainAlign_ = FlexAlign::FLEX_START;
    RefPtr<GeometryNode> geometryNode_;
    RefPtr<LayoutProperty> layoutProperty_;
    RefPtr<LayoutAlgorithmWrapper> layoutAlgorithm_;
    std::list<RefPtr<LayoutWrapper>> children_;
    std::vector<RefPtr<ProbeWrapper>> concreteChildren_;
    RecursiveLock lock_;
};

// Run the engine's real layout over `root` at the given viewport size.
inline void LayoutScene(const RefPtr<ProbeWrapper>& root, float width, float height)
{
    LayoutConstraintF rc;
    rc.selfIdealSize.SetWidth(width);
    rc.selfIdealSize.SetHeight(height);
    rc.maxSize = SizeF(width, height);
    rc.minSize = SizeF(0.0f, 0.0f);
    rc.percentReference = SizeF(width, height);
    root->Measure(rc);
    root->Layout();
}

} // namespace OHOS::Ace::NG

namespace acehost {

// One laid-out rectangle in absolute window coordinates.
struct PlacedRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    uint32_t color = 0;
    std::string tag;
};

// Walk the laid-out tree and flatten it to absolute rectangles, parents before
// children (so a child paints over its parent).  Mirrors what the engine's
// geometry nodes say; no arithmetic beyond the coordinate accumulation that a
// flattening pass is.
inline std::vector<PlacedRect> Flatten(const OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper>& root)
{
    std::vector<PlacedRect> out;
    std::function<void(const OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper>&, int, int)> walk =
        [&](const OHOS::Ace::RefPtr<OHOS::Ace::NG::ProbeWrapper>& node, int parentX, int parentY) {
            if (!node) {
                return;
            }
            const auto offset = node->GetGeometryNode()->GetMarginFrameOffset();
            const auto size = node->GetGeometryNode()->GetFrameSize();
            const int x = parentX + static_cast<int>(offset.GetX());
            const int y = parentY + static_cast<int>(offset.GetY());
            out.push_back({ x, y, static_cast<int>(size.Width()), static_cast<int>(size.Height()), node->Color(),
                node->Tag() });
            for (const auto& child : node->Children()) {
                walk(child, x, y);
            }
        };
    walk(root, 0, 0);
    return out;
}

// A trivial 32-bit software surface.  Pixels are 0x00RRGGBB.
class Surface {
public:
    Surface(int width, int height, uint32_t background)
        : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height, background)
    {}

    int Width() const
    {
        return width_;
    }
    int Height() const
    {
        return height_;
    }
    uint32_t At(int x, int y) const
    {
        return pixels_[static_cast<size_t>(y) * width_ + x];
    }
    const uint32_t* Data() const
    {
        return pixels_.data();
    }
    uint32_t* Data()
    {
        return pixels_.data();
    }

    void Clear(uint32_t color)
    {
        std::fill(pixels_.begin(), pixels_.end(), color);
    }

    // Inset by one pixel so rectangles keep a visible border, matching the
    // Stage 2a probe's raster so the same pixels can be asserted.
    void FillRect(int x, int y, int w, int h, uint32_t color)
    {
        for (int j = y + 1; j < y + h - 1; ++j) {
            for (int i = x + 1; i < x + w - 1; ++i) {
                if (i >= 0 && i < width_ && j >= 0 && j < height_) {
                    pixels_[static_cast<size_t>(j) * width_ + i] = color;
                }
            }
        }
    }

    bool WritePpm(const char* path) const
    {
        std::FILE* f = std::fopen(path, "wb");
        if (!f) {
            return false;
        }
        std::fprintf(f, "P6\n%d %d\n255\n", width_, height_);
        for (uint32_t px : pixels_) {
            const unsigned char rgb[3] = {
                static_cast<unsigned char>((px >> 16) & 0xffu),
                static_cast<unsigned char>((px >> 8) & 0xffu),
                static_cast<unsigned char>(px & 0xffu),
            };
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        return true;
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint32_t> pixels_;
};

} // namespace acehost

#endif // ACE_HOST_ACE_SCENE_HPP
