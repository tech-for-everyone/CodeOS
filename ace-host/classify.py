#!/usr/bin/env python3
"""Classify how far a directory of ace_engine sources gets on a plain Linux host.

WHY THIS EXISTS
---------------
"How much work is the port" is not answerable by reading includes. A header can
transitively reach no Skia and its .cpp can still include it. So this compiles
every .cpp in a subtree with host g++ -fsyntax-only and buckets each file by the
FIRST error class it hits. The buckets are the work items:

  clean         compiles; no action
  portable-inc  missing standard include (std::for_each with no <algorithm>,
                uint64_t with no <cstdint>); a one-line patch each, and a real
                bug in the upstream tree rather than a porting problem
  other-inc     missing non-standard header that is not a known OHOS/Rosen lib
  gcc-strict    newer-GCC strictness that clang accepted; needs a targeted patch
                or a documented compiler-version floor
  ohos-ext      needs an OpenHarmony system header (securec.h, cJSON.h, RSA...);
                each one is a small vendored shim
  skia          reaches Skia or Rosen. The render/measure frontier, and the most
                decision-relevant bucket: everything below can be ported with
                patches, everything above needs a rasteriser.

Only the first error is reported per file, so a file bucketed "portable-inc" may
hide an "ohos-ext" behind it. The counts are therefore LOWER BOUNDS on the harder
buckets -- the safe direction for a feasibility estimate.

Each bucket reports the file the ERROR is in, not the file being compiled. Most
failures live in a shared header, and attributing them to the .cpp turns one
header bug into sixteen apparent ones.
"""
import os, re, subprocess, sys, collections

from aceroots import ROOT, INCLUDE_DIRS

INCARG = [f"-I{d}" for d in INCLUDE_DIRS]

# Which compiler to measure with. Defaults to the one that can actually build
# the tree today: ace_engine is built with clang upstream, and GCC 16's stricter
# anonymous-aggregate and nested-name-specifier rules reject constructs clang
# accepts. Measuring with g++ alone would report a compiler choice as a porting
# cost. Override with CXX=... and EXTRA_FLAGS=... (e.g. EXTRA_FLAGS=-stdlib=libc++).
CXX = os.environ.get("CXX", "clang++")
STD = os.environ.get("STD", "-std=gnu++17")
EXTRA = os.environ.get("EXTRA_FLAGS", "").split()

ERR = re.compile(r"^(.*?):(\d+):(\d+):\s+(fatal error|error):\s+(.*)$")

def classify(msg):
    low = msg.lower()
    if "skia" in low or "skdata" in low:
        return "skia"
    if "render_service_client" in low or "rosen" in low:
        return "rosen"
    missing = ("no such file or directory" in low) or ("file not found" in low)
    if missing:
        if re.search(r"(securec|hitrace|hilog|uv\.h|zlib|cjson|napi|node_api|"
                     r"parameters\.h|bounds_checking|ace/xcomponent|native_window|"
                     r"chnsecal|hilog)", low):
            return "ohos-ext"
        return "other-inc"
    # Undeclared standard names: <algorithm> for std::for_each, <cstdint> for
    # uint64_t/uintptr_t, <memory> for unique_ptr. One-line patches each.
    if re.search(r"no member named .(for_each|find|sort|max|min|unique). in namespace .std|"
                 r"is not a member of .std|"
                 r"was not declared in this scope|"
                 r"has not been declared|"
                 r"does not name a type|"
                 r"does not name a template type|"
                 r"unknown type name .(u?int(8|16|32|64)_t|size_t|ptrdiff_t)", msg):
        return "portable-inc"
    # Strictness that clang/gcc apply differently. Flag-configurable, not a code
    # problem: upstream builds this tree with clang, so a clang complaint here is
    # a flag the OHOS build supplies globally.
    if re.search(r"narrowing|anonymous aggregate|changes meaning of|"
                 r"template-id|not allowed|nested-name-specifier", msg):
        return "strict-warn"
    return "other"

def main():
    roots = sys.argv[1:] or [f"{ROOT}/frameworks/base"]
    files = []
    for r in roots:
        p = r if r.startswith("/") else os.path.join(ROOT, r)
        if os.path.isfile(p):
            files.append(p)
        else:
            for dp, _, fns in os.walk(p):
                if "/test" in dp or "/unittest" in dp:
                    continue
                files.extend(os.path.join(dp, f) for f in fns if f.endswith(".cpp"))
    files.sort()

    buckets = collections.Counter()
    # examples keyed by the erroring location, so one header bug shows once
    examples = collections.defaultdict(dict)
    for f in files:
        try:
            p = subprocess.run(
                [CXX, STD, "-fsyntax-only"] + INCARG + EXTRA +
                ["-D__STDC_LIMIT_MACROS", f],
                capture_output=True, text=True, timeout=180)
        except subprocess.TimeoutExpired:
            buckets["timeout"] += 1
            continue
        first = None
        for line in p.stderr.splitlines():
            m = ERR.match(line)
            if m:
                first = m
                break
        if first is None:
            buckets["clean"] += 1
            continue
        b = classify(first.group(5))
        buckets[b] += 1
        where = first.group(1)
        where = where[len(ROOT) + 1:] if where.startswith(ROOT) else where
        key = f"{where}:{first.group(2)}"
        d = examples[b].setdefault(key, {"msg": first.group(5), "ins": []})
        d["ins"].append(f[len(ROOT) + 1:])

    total = len(files)
    print(f"ace_engine host-compile classification over {total} .cpp files")
    for r in roots:
        print(f"  root: {r}")
    print()
    order = ["clean", "portable-inc", "strict-warn", "ohos-ext",
             "other-inc", "rosen", "skia", "other", "timeout"]
    for b in order:
        if buckets[b]:
            print(f"  {b:<14} {buckets[b]:>5}  ({100.0 * buckets[b] / total:4.1f}%)")
    print()
    for b in order:
        if examples[b]:
            print(f"  {b}  -- {len(examples[b])} distinct location(s):")
            for key, d in sorted(examples[b].items())[:6]:
                ins = d["ins"]
                more = "" if len(ins) <= 3 else f" +{len(ins) - 3} more"
                print(f"      {key}")
                print(f"          {d['msg'][:100]}")
                print(f"          hit by: {', '.join(x.split('/')[-1] for x in ins[:3])}{more}")
            if len(examples[b]) > 6:
                print(f"      ... and {len(examples[b]) - 6} more location(s)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
