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
