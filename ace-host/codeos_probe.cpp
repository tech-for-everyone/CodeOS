// The CodeOS payload: run ArkUI's real tree-layout algorithm and print the
// geometry over raw `write(1, ...)`, with no libc.
//
// This is the same engine code the host `ace_tree_probe` runs -- the real
// LinearLayoutUtils::Measure/Layout walk, LayoutProperty, GeometryNode and
// calc_length, driven through the ProbeWrapper test double in ace_scene.hpp.
// The only thing that changes for CodeOS is the runtime underneath it
// (codeos_runtime.cpp). Nothing about the layout is reimplemented here.
//
// Output is line-oriented so a boot harness can assert it on the serial log:
//
//   ACEOS layout begin
//     [ok  ] root width  got 360 want 360
//     ...
//   ACEOS PASS: 0 failure(s)

#include <cstdint>
#include <string>
#include <vector>

#include "ace_scene.hpp"

using namespace OHOS::Ace;
using namespace OHOS::Ace::NG;

// Provided by codeos_runtime.cpp.
void AceOut(const char* s);
void AceOutNum(long v);

namespace {

int g_failures = 0;

void check(const char* what, double got, double want)
{
    const bool ok = (got == want);
    if (!ok) {
        ++g_failures;
    }
    AceOut("  [");
    AceOut(ok ? "ok  " : "FAIL");
    AceOut("] ");
    AceOut(what);
    AceOut(" got ");
    AceOutNum(static_cast<long>(got));
    AceOut(" want ");
    AceOutNum(static_cast<long>(want));
    AceOut("\n");
}

void walk(const RefPtr<ProbeWrapper>& node, int parentX, int parentY)
{
    if (!node) {
        return;
    }
    const auto offset = node->GetGeometryNode()->GetMarginFrameOffset();
    const auto size = node->GetGeometryNode()->GetFrameSize();
    const int x = parentX + static_cast<int>(offset.GetX());
    const int y = parentY + static_cast<int>(offset.GetY());
    AceOut("    ");
    AceOut(node->Tag().c_str());
    AceOut(" x=");
    AceOutNum(x);
    AceOut(" y=");
    AceOutNum(y);
    AceOut(" w=");
    AceOutNum(static_cast<long>(size.Width()));
    AceOut(" h=");
    AceOutNum(static_cast<long>(size.Height()));
    AceOut("\n");
    for (const auto& child : node->Children()) {
        walk(child, x, y);
    }
}

constexpr int kW = 360;
constexpr int kH = 640;

} // namespace

extern "C" int codeos_main(void)
{
    AceOut("ACEOS layout begin\n");

    // --- the same tree as ace_tree_probe.cpp -------------------------------
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

    // --- assert the geometry (identical to ace_tree_probe.cpp) -------------
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

    // --- print the flattened rectangles ------------------------------------
    walk(root, 0, 0);

    AceOut("ACEOS ");
    AceOut(g_failures == 0 ? "PASS" : "FAIL");
    AceOut(": ");
    AceOutNum(g_failures);
    AceOut(" failure(s)\n");
    return g_failures == 0 ? 0 : 1;
}
