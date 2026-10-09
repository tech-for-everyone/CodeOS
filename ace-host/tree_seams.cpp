// Host-side seams for the layout-TREE probe (tree_probe.cpp).
//
// The real linear-layout algorithm (linear_layout_utils.cpp) and the real
// LayoutProperty (layout/layout_property.cpp) are linked unchanged by this
// probe. What does not exist on this host is the engine's *service core*: there
// is no Container, no PipelineContext and no UI application, and the probe
// builds its tree out of a LayoutWrapper test double with no FrameNode host.
//
// The reachable layout code still *emits references* to a few out-of-line
// service calls -- JSON serialisation pulled in by LayoutProperty's vtable,
// safe-area expansion, colour-mode lookup, trace markers. Every one of these
// lives on a path that a headless probe with a null host never executes:
//
//   * LayoutProperty::ToJsonValue  -- only called by inspectors; pulls JsonUtil.
//   * LayoutWrapper::GetAccumulatedSafeAreaExpand/ResetSafeAreaPadding -- need a
//     host FrameNode's safe-area insets, and there is no host.
//   * PipelineContext::* -- need a current pipeline, and there is none.
//   * Container::Current -- needs AceEngine (the OHOS ability layer).
//
// The returned values below are the "no service is present" answers: null,
// empty, false. That is exactly what the real code returns when no container is
// current, so a missing service can never be mistaken for a layout result. No
// layout arithmetic is defined here; if the probe's geometry assertions depend
// on any of these, they will fail.

#include <optional>

#include "base/log/ace_trace.h"
#include "base/utils/device_config.h"
#include "core/common/ace_application_info.h"
#include "core/common/container.h"
#include "core/components_ng/base/geometry_node.h"
#include "core/components_ng/layout/layout_wrapper.h"
#include "core/pipeline_ng/pipeline_context.h"
#include "ui/view/ui_context.h"

namespace OHOS::Ace::NG {

// No current pipeline exists off-device.
PipelineContext* PipelineContext::GetCurrentContextPtrSafely()
{
    return nullptr;
}

PipelineContext* PipelineContext::GetCurrentContextPtrSafelyWithCheck()
{
    return nullptr;
}

float PipelineContext::GetCurrentRootWidth()
{
    return 0.0f;
}

std::optional<TextDirection> PipelineContext::ResolveDirectionFromEnv(const RefPtr<FrameNode>& host)
{
    (void)host;
    return std::nullopt;
}

void PipelineContext::SetIsDisappearChangeNodeMinDepth(int32_t depth)
{
    (void)depth;
}

bool PipelineContext::ThrottleRenderTreeRebuild(int32_t nodeId, const RefPtr<RenderContext>& renderContext)
{
    (void)nodeId;
    (void)renderContext;
    return false;
}

ColorMode PipelineContext::GetColorMode() const
{
    return ColorMode::LIGHT;
}

// No FrameNode host => no safe-area insets to accumulate.
ExpandEdges LayoutWrapper::GetAccumulatedSafeAreaExpand(
    bool includingSelf, IgnoreLayoutSafeAreaOpts options, IgnoreStrategy strategy)
{
    (void)includingSelf;
    (void)options;
    (void)strategy;
    return ExpandEdges();
}

void LayoutWrapper::ResetSafeAreaPadding() {}

} // namespace OHOS::Ace::NG

namespace OHOS::Ace {

// No OHOS ability layer: no container is ever current. `CurrentSafely` is the
// lookup that PipelineBase::GetCurrentContextSafely uses; with no engine it
// returns null, and the caller then returns a null pipeline.
RefPtr<Container> Container::Current()
{
    return nullptr;
}

RefPtr<Container> Container::CurrentSafely()
{
    return nullptr;
}

// With no container, the real implementation falls back to the application's
// target API version -- this is the same branch, kept faithful on purpose.
bool Container::GreatOrEqualAPITargetVersion(PlatformVersion version)
{
    auto apiTargetVersion = AceApplicationInfo::GetInstance().GetApiTargetVersion() % 1000;
    return apiTargetVersion >= static_cast<int32_t>(version);
}

// Trace markers are debug instrumentation; the engine's own tests compile them
// to no-ops too (test/mock/frameworks/base/log/mock_ace_trace.cpp).
void AceTraceBegin(const char* name)
{
    (void)name;
}

void AceTraceEnd() {}

} // namespace OHOS::Ace

namespace OHOS::Ace::Kit {

// No UI context is attached off-device.
RefPtr<UIContext> UIContext::Current()
{
    return nullptr;
}

} // namespace OHOS::Ace::Kit
