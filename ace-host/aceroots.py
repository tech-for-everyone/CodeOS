"""The include roots the real ace_engine build uses, in one place.

WHY THIS FILE EXISTS
--------------------
Both the closure walker and the compile classifier need the same -I list, and
getting it wrong does not fail loudly -- it silently changes which header a name
resolves to, and therefore whether a file "compiles". That is how a measuring
tool produced 20 phantom errors on a file that builds fine, and invented work
items from thin air.

The list is taken from the build itself, not guessed:

    frameworks/core/BUILD.gn:22   include_dirs = [ "$ace_root/frameworks",
                                                    "$ace_root/interfaces/inner_api/ace_kit/include" ]
    frameworks/core/BUILD.gn:85   include_dirs = [ "$ace_root" ]
    BUILD.gn:22                   "$ace_root/interfaces/inner_api/ace_kit/include"

WHAT MUST NOT BE IN THE LIST
----------------------------
Nothing below $ace_root/frameworks may be added. Those directories are full of
one-line trampoline headers that forward to a same-named header elsewhere on the
real include path -- e.g. frameworks/base/json/json_util.h is 21 lines whose only
content is `#include "json/json_util.h"`. Put frameworks/base on the -I path and
that include resolves *to the trampoline itself*; the include guard then swallows
it and the real JsonUtil is never declared. The build fails with "JsonUtil has
not been declared" in a file that has nothing to do with JsonUtil, which reads
like a portability bug and is actually a measurement bug.
"""
import os

ROOT = os.environ.get("ACE_ROOT", "/home/codeosuser/CodeOS/arkui")

# ace-host/ohos-root/foundation/arkui/ace_engine is a symlink back to ROOT. Seven
# files include ace_engine headers by their OpenHarmony absolute path
# (`#include "foundation/arkui/ace_engine/frameworks/base/utils/utf.h"`), which is
# what `$ace_root` means inside the OHOS build. Rather than patch those seven,
# ohos-root recreates the prefix so the includes resolve as written.
HERE = os.path.dirname(os.path.abspath(__file__))
OHOS_ROOT = f"{HERE}/ohos-root"

INCLUDE_DIRS = [
    f"{HERE}/compat/shims",                             # securec.h, hilog/log.h, refbase.h
    OHOS_ROOT,                                          # "foundation/arkui/ace_engine/..."
    ROOT,                                               # "frameworks/...", "adapter/..."
    f"{ROOT}/frameworks",                               # "base/...", "core/..."
    f"{ROOT}/interfaces/inner_api/ace_kit/include",     # "ui/...", "json/..."
    f"{ROOT}/interfaces/inner_api",                     # "ace_kit/..."
    f"{ROOT}/interfaces/native",                        # "native_node.h"
    f"{ROOT}/adapter",
]

# Roots that exist but must never be added, with the reason, so that a future
# reader does not "helpfully" put them back.
FORBIDDEN_DIRS = {
    f"{ROOT}/frameworks/base": "trampoline headers (json/json_util.h) self-shadow",
    f"{ROOT}/frameworks/core": "redundant: reachable as $ace_root/frameworks",
}
