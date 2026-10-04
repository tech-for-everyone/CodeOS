import os, re, sys, collections

from aceroots import ROOT, INCLUDE_DIRS

ROOTS = INCLUDE_DIRS

INC = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')

def resolve(name, from_file):
    if name.startswith("third_party/") or name.startswith("//"):
        return None, "ohos-tree"
    for r in ROOTS:
        p = os.path.join(r, name)
        if os.path.isfile(p):
            return p, None
    return None, "external"

def closure(starts, limit=200000):
    seen, ext, missing = set(), collections.Counter(), collections.Counter()
    q = collections.deque(starts)
    while q and len(seen) < limit:
        f = q.popleft()
        if f in seen: continue
        seen.add(f)
        try:
            txt = open(f, errors="replace").read()
        except OSError:
            continue
        for line in txt.splitlines():
            m = INC.match(line)
            if not m: continue
            name = m.group(1)
            p, why = resolve(name, f)
            if p:
                q.append(p)
            else:
                ext[why] += 1
                missing[name] += 1
    return seen, ext, missing

for start in sys.argv[1:]:
    sp = start if start.startswith("/") else os.path.join(ROOT, start)
    if not os.path.isfile(sp):
        print(f"\n=== {start} ===\n  START FILE DOES NOT EXIST -- refusing to "
              f"report a closure for it"); continue
    seen, ext, missing = closure([sp])
    hdrs = [s for s in seen if s.endswith((".h", ".hpp"))]
    srccs = [s for s in seen if s.endswith((".cpp", ".c", ".cc"))]
    print(f"\n=== {start} ===")
    print(f"  closure: {len(seen)} files  ({len(hdrs)} headers, {len(srccs)} sources)")
    print(f"  unresolvable: {sum(ext.values())} (external={ext.get('external',0)}, ohos-tree={ext.get('ohos-tree',0)})")
    hits = sorted(n for n in missing if "skia" in n.lower() or "ui/" in n.lower() or "flutter" in n.lower())
    if hits:
        print(f"  UI/render/OHOS headers reached ({len(hits)}):")
        for h in hits[:15]: print(f"      {h}")
