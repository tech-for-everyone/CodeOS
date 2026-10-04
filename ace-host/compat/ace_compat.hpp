/*
 * Forced-include compatibility header for building ace_engine off OpenHarmony.
 *
 * WHY THIS EXISTS
 * ---------------
 * `frameworks/base` has five headers that use a standard-library facility
 * without including the header that provides it. The tree builds under
 * OpenHarmony anyway because its clang config happens to get those declarations
 * transitively; a bare host compile does not. The upstream fix is to add the
 * include at each site, and that is where they belong.
 *
 * This exists so the port does not have to patch a vendored clone to make
 * progress: it is passed with `-include`, so every translation unit sees these
 * declarations before its own includes run. Nothing in arkui/ is modified, so a
 * re-clone reproduces the build exactly, and the patches stay optional.
 *
 * The bugs are real and worth reporting upstream; each is noted below with the
 * site that needs it.
 *
 *   <algorithm>  frameworks/base/geometry/matrix3.h:43   std::for_each,
 *                frameworks/base/geometry/least_square_impl.h  (matrix solve)
 *   <cstdint>    frameworks/base/json/uobject.h:24          uint8_t
 *                frameworks/base/log/ace_scoring_log.h:41   uint64_t
 *                frameworks/base/log/jank_frame_report.h:25 uint32_t
 *                interfaces/inner_api/ace_kit/include/ui/base/inspector_filter.h:38
 *                interfaces/inner_api/ace_kit/include/ui/base/type_info_base.h:94
 */
#ifndef ACE_HOST_COMPAT_HPP
#define ACE_HOST_COMPAT_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#endif // ACE_HOST_COMPAT_HPP
