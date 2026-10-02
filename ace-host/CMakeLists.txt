cmake_minimum_required(VERSION 3.16)
project(ace_host CXX)

# Host build of OpenHarmony's ArkUI ace_engine, foundation + layout.
#
# WHY THIS EXISTS
# ---------------
# ace_engine ships only a GN build, and BUILD.gn imports `//build/ohos.gni` --
# the OpenHarmony build system, which is not in the ace_engine repo. So there is
# no way to build this tree as it stands, on any host, without first standing up
# the OpenHarmony build. This CMake answers a smaller question first: **how much
# of ace_engine compiles and runs off OpenHarmony, unchanged?**
#
# WHY clang AND NOT g++
# ---------------------
# ace_engine is built with clang upstream. Measured over frameworks/base, g++ 16
# compiles 32/63 files and clang 22 compiles 47/63; the difference is not code but
# strictness (GCC 16 rejects an anonymous union member with a constructor at
# ng/rect_t.h:543 that clang accepts). Measuring with the compiler the project
# does not use would report a compiler choice as a porting cost, so this uses
# clang and libstdc++.
#
# WHAT IS NOT PATCHED
# -------------------
# Nothing under arkui/ is modified. The five missing-standard-include bugs are
# worked around with `-include compat/ace_compat.hpp`, so a fresh clone
# reproduces this build byte for byte and the real fixes stay optional patches.
# See that header for the list of sites that want fixing upstream.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dead-code elimination. The layout-tree probe reaches only a handful of
# functions in large TUs (layout/layout_property.cpp and the property vtables),
# but an object file is linked whole unless it is split. `-ffunction-sections`
# splits it and `--gc-sections` drops what `main` cannot reach, instead of
# dragging in the service-core symbols that only the unreachable functions
# reference. This is a build-hygiene change -- it removes dead code, it does not
# change behaviour -- and it also shrinks the existing probes.
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

set(ACE ${CMAKE_CURRENT_SOURCE_DIR}/../arkui)
if(NOT EXISTS ${ACE}/frameworks/base)
    message(FATAL_ERROR
        "arkui/ not found at ${ACE}. This build consumes a clone of\n"
        "  https://github.com/openharmony/arkui_ace_engine\n"
        "checked out at the repo root as `arkui/`.")
endif()

# Files that need third-party behaviour, not a compiler flag. Faking any of these
# would make the build lie: cJSON is a real JSON implementation, chnsecal is a
# real lunar calendar, and the other four are the rasterisation path. They are
# excluded and counted, so the number in the build output is honest.
set(ACE_EXCLUDED
    ${ACE}/frameworks/base/i18n/localization.cpp              # chnsecal.h   (OHOS lunar calendar)
    ${ACE}/frameworks/base/json/json_util.cpp                 # cJSON.h      (cJSON)
    ${ACE}/frameworks/base/network/download_manager_impl.cpp  # http_client.h (OHOS netstack)
    ${ACE}/frameworks/base/log/jank_frame_report.cpp          # Rosen RS client
    ${ACE}/frameworks/base/utils/measure_util.cpp             # rosen_text
    ${ACE}/frameworks/base/base64/base64_util.cpp             # Skia
)

file(GLOB_RECURSE ACE_BASE_SOURCES CONFIGURE_DEPENDS ${ACE}/frameworks/base/*.cpp)
list(FILTER ACE_BASE_SOURCES EXCLUDE REGEX "/test/")
foreach(x ${ACE_EXCLUDED})
    list(REMOVE_ITEM ACE_BASE_SOURCES ${x})
endforeach()

list(LENGTH ACE_BASE_SOURCES n_included)
file(GLOB_RECURSE all_base CONFIGURE_DEPENDS ${ACE}/frameworks/base/*.cpp)
list(FILTER all_base EXCLUDE REGEX "/test/")
list(LENGTH all_base n_total)
message(STATUS "ace_layout: compiling ${n_included} of ${n_total} frameworks/base sources "
               "(${n_total} total, ${n_included} included; see ACE_EXCLUDED for the rest)")

# The layout engine (components_ng): the FrameNode base, the layout property
# types (LayoutConstraint), and the linear layout algorithm. This is the layer
# that decides where everything goes, and it is the layer that can be made to
# work without a rasteriser -- its only renderer dependency is behind
# components_ng/render/drawing.h, which swaps to the in-tree mock renderer under
# ACE_UNITTEST (see the README). Measured separately from frameworks/base
# because the two fail for different reasons.
file(GLOB_RECURSE ACE_NG_LAYOUT_SOURCES CONFIGURE_DEPENDS
    ${ACE}/frameworks/core/components_ng/base/*.cpp
    ${ACE}/frameworks/core/components_ng/property/*.cpp
    ${ACE}/frameworks/core/components_ng/pattern/linear_layout/*.cpp
)
list(FILTER ACE_NG_LAYOUT_SOURCES EXCLUDE REGEX "/test/")

# `property/calc_length.cpp` defines `ScaleProperty::CreateScaleProperty`, which
# `LayoutConstraintT` calls from its default member initialiser
# (layout_constraint.h:54). The real definition reaches
# `PipelineBase::GetCurrentContextSafely()` purely to read the current density:
#
#     auto pipeline = context ? AceType::Claim(context) : PipelineBase::GetCurrentContextSafely();
#     CHECK_NULL_RETURN(pipeline, scaleProperty);      // default 1.0 scale
#
# On a host with no container the call is null-safe and returns the default
# scale -- but the reference is still emitted, so the linker then drags in
# PipelineBase and everything PipelineBase transitively needs (Container,
# AceEngine, Kit::UIContext, JsonUtil/cJSON, the trace machinery, ...). That is
# a service-core closure, not layout logic, and it is not needed to measure
# layout geometry.
#
# ace_engine already ships the correct seam for this: its own unit tests use
#     test/mock/frameworks/core/components_ng/property/mock_calc_length.cpp
# which implements the SAME symbol returning the default ScaleProperty() -- i.e.
# exactly the value the real function returns when there is no container. This
# is not a homemade stub; it is upstream's mock, selected the same way the
# engine's own tests select it. The real TU is excluded by name so its other
# entry points cannot silently re-enter the engine.
set(ACE_NG_MOCKED
    ${ACE}/frameworks/core/components_ng/property/calc_length.cpp
)
foreach(x ${ACE_NG_MOCKED})
    list(REMOVE_ITEM ACE_NG_LAYOUT_SOURCES ${x})
endforeach()
list(APPEND ACE_NG_LAYOUT_SOURCES
    ${ACE}/test/mock/frameworks/core/components_ng/property/mock_calc_length.cpp
)

list(LENGTH ACE_NG_LAYOUT_SOURCES n_ng)
message(STATUS "ace_layout: + ${n_ng} components_ng layout sources (calc_length -> upstream mock)")

# The OS abstraction layer and the pipeline. Linking *any* non-trivial engine
# code needs these, because base/utils types log through LogWrapper and ask
# SystemProperties for debug flags, and Dimension asks PipelineBase for the
# current density. Three files, each the only definition of a symbol the rest of
# the tree calls, and each a platform seam rather than engine logic:
#
#   adapter/preview/osal/system_properties.cpp  the host (preview) SystemProperties.
#                                               The ohos one hard-codes OHOS flags.
#   adapter/ohos/osal/log_wrapper.cpp           the ONLY log backend in the tree.
#                                               There is no preview log_wrapper,
#                                               so this is not a choice between two
#                                               platforms -- it is the only sink
#                                               that exists. It resolves hilog via
#                                               dlopen, which works unchanged here
#                                               (hilog is never loaded, so it falls
#                                               back to stderr).
#   frameworks/core/pipeline/pipeline_base.cpp  PipelineBase::GetCurrentContextSafely,
#                                               called by Dimension::NormalizeToPx.
#
# Deliberately NOT globbing adapter/ohos or adapter/preview wholesale: those are
# full platform ports that want Skia and the OHOS IPC stack. Only the three files
# whose symbols the layout path actually references are pulled in.
set(ACE_PLATFORM_SOURCES
    ${ACE}/adapter/preview/osal/system_properties.cpp
    ${ACE}/adapter/ohos/osal/log_wrapper.cpp
    ${ACE}/frameworks/core/pipeline/pipeline_base.cpp
)

add_library(ace_layout STATIC ${ACE_BASE_SOURCES} ${ACE_NG_LAYOUT_SOURCES} ${ACE_PLATFORM_SOURCES})

target_include_directories(ace_layout PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/shims                # securec.h, hilog/log.h, refbase.h -- must precede the tree
    ${CMAKE_CURRENT_SOURCE_DIR}/ohos-root                   # "foundation/arkui/ace_engine/..." (7 files include by full OHOS path)
    ${ACE}                                                   # "frameworks/...", "adapter/..."
    ${ACE}/frameworks                                        # "base/...", "core/..."
    ${ACE}/interfaces/inner_api/ace_kit/include               # "ui/...", "json/..."
    ${ACE}/interfaces/inner_api
    ${ACE}/interfaces/native
    ${ACE}/adapter
)

target_compile_options(ace_layout PUBLIC
    -include ${CMAKE_CURRENT_SOURCE_DIR}/compat/ace_compat.hpp
    # Near-miss: dimension.cpp:486 narrows double->float in a braced init. Only
    # one file, and upstream's clang config evidently permits it. Fixing the
    # source (a cast) is the honest fix; this flag is so the port is not blocked
    # on a warning.
    -Wno-c++11-narrowing
    # Selects ace_engine's in-tree mock renderer instead of Rosen/Skia. This is
    # the flag the engine's own unit tests use, and it is what makes the engine
    # buildable with no rasteriser at all: components_ng/render/drawing.h is
    # hard-wired to `drawing/engine_adapter/skia_adapter/skia_canvas.h` by
    # default and to test/mock/.../testing_canvas.h under this flag. Without it
    # the layout core does not compile here -- see controls C5.
    -DACE_UNITTEST=1
    -Wall -Wextra -Wno-unused-parameter
)

add_executable(ace_layout_probe main.cpp)
target_link_libraries(ace_layout_probe PRIVATE ace_layout)

# Negative control on the probe's own pass/fail machinery. Identical source with
# one deliberately wrong expectation compiled in; controls.py requires it to FAIL.
# If this ever passes, every other PASS from ace_layout_probe is worthless.
add_executable(ace_layout_probe_mutant main.cpp)
target_link_libraries(ace_layout_probe_mutant PRIVATE ace_layout)
target_compile_definitions(ace_layout_probe_mutant PRIVATE ACE_PROBE_MUTANT=1)

add_executable(ace_demo demo.cpp)
target_link_libraries(ace_demo PRIVATE ace_layout)
target_include_directories(ace_demo PRIVATE
    ${ACE}/test/mock/frameworks/core
    ${ACE}/test/mock
)
target_compile_definitions(ace_demo PRIVATE ACE_UNITTEST=1)

# The layout engine's own probe: asserts the geometry the layout layer computes.
# Separate binary from ace_layout_probe because it asserts a different layer and
# fails for different reasons.
#
# This target links and passes (Stage 1b). It used to be EXCLUDE_FROM_ALL while
# `LayoutConstraintT`'s default member initialiser
# (layout_constraint.h:54, `ScaleProperty scaleProperty = ...CreateScaleProperty()`)
# dragged in the whole service core through `calc_length.cpp`. That is resolved
# above by compiling upstream's own `mock_calc_length.cpp` in place of the real
# TU: the mock returns the default ScaleProperty, which is exactly what the real
# function returns with no container present. The closure therefore stays inside
# the layout layer -- no PipelineBase, no Skia, no OHOS IPC, no cJSON.
add_executable(ace_ng_layout_probe layout_probe.cpp)
target_link_libraries(ace_ng_layout_probe PRIVATE ace_layout)

# Stage 2: run the *real* linear-layout algorithm over a hand-built tree, assert
# the resulting geometry, and rasterise it. See tree_probe.cpp for what is real
# (the algorithm, LayoutProperty, GeometryNode) and what is a test double (the
# LayoutWrapper, which upstream backs with a FrameNode; here it is implemented
# directly so the engine's service core is not pulled in).
#
# The sources below are the measured link closure of that run, nothing more:
#
#   inspector_constants.cpp  the V2::*_ETS_TAG string constants. They are
#                            referenced from LayoutProperty's vtable (the
#                            ToJsonValue virtual), so they are kept even though
#                            the probe never serialises anything.
#   alignment.cpp            Alignment::GetAlignment, used by layout properties.
#   layout_property.cpp      the real LayoutProperty implementation.
#   layout_algorithm.cpp     LayoutAlgorithmWrapper.
#   calc_length.cpp          the real CalcLength::NormalizeToPx. It is the only
#                            source of this symbol; it also defines
#                            ScaleProperty::CreateScaleProperty, which therefore
#                            wins over the mock in ace_layout (a directly-linked
#                            object beats an archive member).
#   tree_seams.cpp           the "no service exists on this host" answers for the
#                            unreachable paths (see that file).
#   mock_*                   upstream's own mocks for the services that are not
#                            here: the render JSON layer, the application info
#                            singleton, the log sink and the LayoutWrapper
#                            out-of-line methods.
set(ACE_TREE_ENGINE_SOURCES
    ${ACE}/frameworks/core/components_v2/inspector/inspector_constants.cpp
    ${ACE}/frameworks/core/components/common/properties/alignment.cpp
    ${ACE}/frameworks/core/components_ng/layout/layout_property.cpp
    ${ACE}/frameworks/core/components_ng/layout/layout_algorithm.cpp
    ${ACE}/frameworks/core/components_ng/property/calc_length.cpp
)
set(ACE_TREE_MOCK_SOURCES
    ${ACE}/test/mock/frameworks/core/components_ng/layout/mock_layout_wrapper.cpp
    ${ACE}/test/mock/frameworks/base/json/mock_json_util.cpp
    ${ACE}/test/mock/frameworks/base/json/mock_json_value.cpp
    ${ACE}/test/mock/frameworks/core/common/mock_ace_application_info.cpp
    ${ACE}/test/mock/adapter/ohos/osal/mock_log_wrapper.cpp
)
# Split into a library so the probe and its mutant share one compilation of the
# engine TUs. It must come BEFORE ace_layout on the link line: the linker is
# left-to-right, so the real calc_length.o here resolves CreateScaleProperty and
# ace_layout's mock_calc_length.o is never pulled (which is the point -- the tree
# path needs the real CalcLength::NormalizeToPx).
add_library(ace_tree_support STATIC
    tree_seams.cpp
    ${ACE_TREE_ENGINE_SOURCES}
    ${ACE_TREE_MOCK_SOURCES}
)
# PRIVATE: ace_tree_support needs ace_layout's include dirs and compile options,
# and records it as a LINK_ONLY dependency, so the executable's link line is
# `ace_tree_support ace_layout` -- support first, which is the greedy order that
# lets the real calc_length.o win.
target_link_libraries(ace_tree_support PRIVATE ace_layout)

# The two archives reference each other: ace_layout's real log_wrapper.cpp calls
# the va_list sink defined by a mock in ace_tree_support, and ace_tree_support's
# engine TUs call symbols in ace_layout. A plain left-to-right archive order
# cannot resolve that without a rescan, so wrap them in a group. The group is
# still ordered support-then-layout, so the real calc_length.o wins.
add_executable(ace_tree_probe tree_probe.cpp)
target_link_libraries(ace_tree_probe PRIVATE
    "-Wl,--start-group" ace_tree_support ace_layout "-Wl,--end-group")

# Negative control on the tree probe's pass/fail machinery: identical source with
# one deliberately wrong expectation compiled in. controls.py requires it to FAIL.
# If it ever passes, ace_tree_probe's PASS is not a comparison.
add_executable(ace_tree_probe_mutant tree_probe.cpp)
target_link_libraries(ace_tree_probe_mutant PRIVATE
    "-Wl,--start-group" ace_tree_support ace_layout "-Wl,--end-group")
target_compile_definitions(ace_tree_probe_mutant PRIVATE ACE_TREE_MUTANT=1)
