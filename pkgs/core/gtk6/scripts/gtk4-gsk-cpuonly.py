#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drop the GPU renderer wiring GSK's public/private headers pull in.
#
# PLAN.md "Dependency surgery" replaces gsk/{gl,gpu,broadway} with gsk/qt: the
# GTK6 renderer is gsk/qt/gskqt.cpp, a QPainter walk of the same GskRenderNode
# tree, driven through the node's existing draw() vfunc (which the cairo rename
# already retargeted at GdkPainter).  The GPU renderers are therefore dead, but
# their headers were still reachable:
#
#   * gsk/gsk.h pulls in <gsk/gpu/gskvulkanrenderer.h> and
#     <gsk/gpu/gskglrenderer.h>.  Those two lines alone kept 390 source files
#     from compiling, because gsk.h is the library's public umbrella.  GTK6 has
#     no GL/Vulkan renderer, so the lines are removed.
#
#   * gsk/gskrendernodeprivate.h pulls in "gpu/gskgputypesprivate.h".  The only
#     things it needs from it are GskGpuRenderPass and GskGpuOcclusion, which
#     appear solely as pointers in the vestigial `occlusion` vfunc (the GPU
#     twin of `draw`, which the gsk/qt renderer never calls).  Opaque forward
#     declarations are enough, and they must not drag in the deleted tree.
#
# This is not a tree wide rename, so it is two exact edits.  It is written as a
# script anyway so it is re-runnable and reviewable, and it is idempotent.
#
# Usage: scripts/gtk4-gsk-cpuonly.py [--check]

import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# gsk/gsk.h: the deleted renderer umbrellas.
GSK_H_DROP = [
    "#include <gsk/gpu/gskvulkanrenderer.h>\n",
    "#include <gsk/gpu/gskglrenderer.h>\n",
]

# gsk/gskrendernodeprivate.h: the deleted GPU type header, replaced by the two
# opaque types the vestigial vfunc names.
GPU_INCLUDE = '#include "gpu/gskgputypesprivate.h"\n'
GPU_FORWARDS = """\
/* GTK6: the GPU renderer is replaced by gsk/qt (QPainter); these two types
 * only appear as pointers in the vestigial occlusion vfunc below. */
typedef struct _GskGpuRenderPass GskGpuRenderPass;
typedef struct _GskGpuOcclusion  GskGpuOcclusion;
"""


def apply(text, edits):
    for old, new in edits:
        if old in text:
            text = text.replace(old, new)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report files that would change, do not write")
    args = ap.parse_args()

    edits = {
        os.path.join(ROOT, "gsk", "gsk.h"):
            [(line, "") for line in GSK_H_DROP],
        os.path.join(ROOT, "gsk", "gskrendernodeprivate.h"):
            [(GPU_INCLUDE, GPU_FORWARDS)],
    }

    changed = []
    for path, pairs in edits.items():
        with open(path, "r", encoding="utf-8") as f:
            old = f.read()
        new = apply(old, pairs)
        if new == old:
            continue
        changed.append(path)
        if not args.check:
            with open(path, "w", encoding="utf-8") as f:
                f.write(new)

    verb = "would change" if args.check else "wrote"
    print(f"gtk4-gsk-cpuonly: {len(changed)} files {verb}")
    for path in changed:
        print("  " + os.path.relpath(path, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
