"""Link closure for the layout engine: which .cpp files does the linker still want?

WHY THIS FILE EXISTS
--------------------
closure.py answers the *header* question (which headers does this file pull in).
Nothing measured the *link* question, and that is the one that is actually
blocking: ace_ng_layout_probe compiles and then fails with 517 undefined
symbols. Guessing at that list by reading BUILD.gn files is how work items get
invented, so this walks the real linker output instead.

IT IS A PROPOSER, NOT A JUDGE
-----------------------------
Every candidate here is found by matching source text, so it is an
over-approximation: a file may be listed because it *calls* a function rather
than defines it. That is deliberate and safe, because ace_layout is a STATIC
library and the linker pulls in only the object members it actually needs. An
unnecessary file costs nothing but a compile. The real verdict is always the
next link, and controls.py-style honesty means this tool never claims a closure
is closed -- it only reports what the linker asked for and where it might live.

FOUR BUGS THIS TOOL HAD, ALL THE SAME SHAPE
--------------------------------------------
Kept because the failure mode is not obvious from the fixed code, and each one
produced output that looked entirely reasonable:

  1. Found 0 of 517 symbols. The index was keyed on the qualification written at
     the definition site, but ace_engine writes `void JsonUtil::GetJsonObj(...)`
     inside `namespace OHOS { namespace Ace {` and relies on the enclosing
     scopes. Fix: track namespace scope by brace depth.
  2. Found 0 again after that. finditer CONSUMED the `::` terminator, so in
     `const RefPtr<X>& EventHub::GetGestureEventHub()` the pattern matched
     `EventHub::` first, resumed after it, and threw the real match away.
     Fix: lookahead instead of consumption, so the chain is captured whole.
  3. Missed every trailing-const method, because the discriminator looks at the
     character after `)` and that character is `c` of `const`, not `{`.
     Fix: skip trailing specifiers.
  4. Wrote 187 unusable "../ace-host/..." paths into closure_sources.txt, twice,
     because os.path.relpath was applied to paths that were ALREADY relative --
     and it resolves the first argument against the CWD, so the result escapes
     the root. Fixing the first call site did nothing because there were two.
     Fix: normalise once, at insertion, and validate on read as well as write.

The general lesson, which is why the guards below exist: a measuring tool that
reports a confident number is more dangerous than one that reports nothing,
because the number gets believed. Every stage that can silently produce a
plausible-but-wrong answer here has an explicit check.

The failure mode this is built to avoid: reporting "N files needed" as if N were
known-good, when the honest number is "N candidates, of which M were confirmed by
the link that followed".

USAGE
    python3 linkclosure.py build.log            # propose sources for the undefined set
    python3 linkclosure.py build.log --cmake    # emit a ready-to-paste path list
    python3 linkclosure.py build.log --merge    # merge the proposal into closure_sources.txt
    python3 linkclosure.py build.log --external # just the symbols no in-tree file defines
"""
import os
import re
import sys
import collections

from aceroots import ROOT

# `undefined reference to `OHOS::Ace::Color::WHITE'`
UNDEF = re.compile(r"undefined reference to [`']([^`']+)[`']")

# A qualified chain ending in a name, where the chain is fully captured and the
# terminator is only LOOKED AT, never consumed.
#
# The lookahead is not a style preference, it is the fix for a bug that made
# this tool find 0 of 517 symbols. Given
#
#     const RefPtr<GestureEventHub>& EventHub::GetGestureEventHub() const
#
# a pattern that CONSUMES its `::` terminator matches `EventHub::` first (as a
# data-shaped name), finditer resumes after it, and the real definition match
# then starts at `GetGestureEventHub` -- with the `EventHub::` prefix already
# eaten and thrown away. The greedy chain plus a lookahead fixes it: the match
# starts at `EventHub`, captures `EventHub::` as the chain, and ends on the
# name without consuming the `(`, so nothing is lost.
QUAL_FN = re.compile(r"\b((?:[A-Za-z_]\w*::)+)([A-Za-z_]\w*)(?=\s*\()")

# A qualified data member: `const Color Color::WHITE = ...` -> Color::WHITE.
# Uses a lookahead too, for the same reason. `=` is `=[^=]` in effect because a
# following `==` is a comparison, never a definition.
QUAL_DATA = re.compile(r"\b((?:[A-Za-z_]\w*::)+)([A-Za-z_]\w*)(?=\s*(?:=|\{|;|\[))")

# Identifier text, for the paren-matching below.
IDENT = re.compile(r"[A-Za-z_]\w*")

# Scope tracking. ace_engine wraps its code in `namespace OHOS { namespace Ace {
# ... } }` and then defines members with only the INNER qualification:
#
#     namespace OHOS { namespace Ace {
#     const double ACE_PI = M_PI;              // not "OHOS::Ace::ACE_PI"
#     void JsonUtil::GetJsonObj(int key) {}   // not "OHOS::Ace::JsonUtil::..."
#     }}                                        // <- the brace pair is a single token
#
# so the symbol the linker prints (OHOS::Ace::ACE_PI) is assembled from the
# enclosing namespace blocks plus the local text. An index built from the text
# alone matches NONE of them -- which is exactly what the first run of this
# tool did: 0 of 517. Namespace scope therefore has to be tracked, not assumed.
#
# The namespace token swallows its own `{` so that the brace is not counted
# twice. Inline `namespace A::B {` is accepted, since both spellings occur.
SCOPE = re.compile(
    r"\bnamespace\s+((?:[A-Za-z_]\w*\s*::\s*)*[A-Za-z_]\w*)?\s*\{"   # 1: names
    r"|(?=\{)|(?=\})"                                              # 2,3: bare braces
)

# A definition with NO qualification at all, which is how ace_engine defines
# namespace-scope constants and globals:
#
#     namespace OHOS { namespace Ace {
#     const double ACE_PI = M_PI;        // "OHOS::Ace::ACE_PI" as the linker sees it
#     const Color Color::WHITE = ...;    // this one *is* qualified -- QUAL handles it
#
# QUAL cannot see the first shape, because it requires a `(` or `::` after the
# name and `ACE_PI` is followed by ` = `. Matching such a definition by text
# alone is hopeless -- `int x = 0;` is everywhere -- so it is gated on brace
# depth: a namespace-scope definition sits at exactly the depth its innermost
# enclosing namespace opened at, while a local variable is nested one or more
# braces deeper inside a function body. Depth is what makes this safe.
BARE_DATA = re.compile(
    r"^[ \t]*(?:(?:static|const|constexpr|extern)[ \t]+)*"
    r"(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:<[^;={}=]*>)?[ \t]*[*&]*[ \t]*)"
    r"([A-Za-z_]\w*)[ \t]*(?:=[^=]|\{|;)",
    re.M,
)


def source_files():
    """Every .cpp in the tree that is a candidate to be compiled, tests excluded.

    /test/ is excluded for the same reason the build excludes it: the in-tree
    unit tests are the only place ace_engine supplies its own fakes, and
    compiling them would make the measurement describe the tests, not the engine.
    """
    out = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in ("test", ".git", "out")]
        for fn in filenames:
            if fn.endswith(".cpp"):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def _skip_parens(txt, i):
    """Return the index just past the paren group starting at txt[i] == '('."""
    depth = 0
    while i < len(txt):
        c = txt[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


TRAILING = re.compile(
    r"\s*(?:const|volatile|mutable|noexcept|override|final|&\s*&|\*|,\s*)?"
)


def _after_paren(txt, i):
    """The next significant character after the paren group ending at i.

    Out-of-line definitions carry trailing specifiers, so the character that
    tells a definition from a call is often not adjacent to the `)`:

        const RefPtr<GestureEventHub>& EventHub::GetGestureEventHub() const
        ^                                                          ^ 'c' here
        {                                                          <- the real answer

    Skipping only whitespace reports "not a definition" and silently loses
    every trailing-const and trailing-noexcept method, which is a large class
    of symbols. Trailing *return types* (`) -> T {`) are not handled, which is
    recorded as a known limit rather than papered over.
    """
    while True:
        m = TRAILING.match(txt, i)
        if not m or m.end() == i:
            break
        i = m.end()
    return txt[i:].lstrip()[:1]


def definitions_in(path):
    """The fully-qualified names this file appears to DEFINE.

    Two things have to be combined, and getting only one of them right yields
    zero matches rather than a wrong answer, which is how the first version of
    this function failed:

      1. the enclosing namespace, from the brace-depth walk below, and
      2. the local qualification, plus a discriminator that separates a
         definition from a call.

    The discriminator is what follows the argument list: a definition is
    followed by `{`, or by `:` for a constructor's member-initialiser list,
    while a call is followed by `;`, `,` or an operator. Static data members
    have no argument list at all and are recognised by a type in front and
    `=`/`{`/`;`/`[` behind.
    """
    try:
        txt = open(path, errors="replace").read()
    except OSError:
        return set()

    # Walk scopes and definition candidates in one pass, in source order, so
    # that each candidate is qualified by whatever namespaces are open at its
    # own offset. Both lists are sorted by offset, so they merge into a single
    # ordered stream -- doing it any other way silently drops every candidate
    # that comes after the last scope event in the file.
    events = []
    for m in SCOPE.finditer(txt):
        kind = 1 if txt[m.start()] not in "{}" else 0
        events.append((m.start(), kind, m))
    for m in QUAL_FN.finditer(txt):
        events.append((m.start(), 2, m))
    for m in QUAL_DATA.finditer(txt):
        events.append((m.start(), 4, m))
    for m in BARE_DATA.finditer(txt):
        events.append((m.start(), 3, m))
    events.sort(key=lambda e: (e[0], e[1]))

    defs = set()
    stack = []          # [(name, depth_at_which_it_opened)]
    depth = 0
    for _, kind, m in events:
        if kind == 2:
            # Function-shaped. Text alone cannot tell a definition from a call,
            # so the discriminator is what follows the argument list: a
            # definition is followed by `{`, or by `:` for a constructor's
            # member-initialiser list; a call is followed by `;` `,` `)`.
            j = _skip_parens(txt, m.end())
            nxt = _after_paren(txt, j)
            if nxt in ("{", ":"):
                prefix = "".join(n + "::" for n, _ in stack)
                defs.add(f"{prefix}{m.group(1)}{m.group(2)}")
        elif kind == 4:
            prefix = "".join(n + "::" for n, _ in stack)
            defs.add(f"{prefix}{m.group(1)}{m.group(2)}")
        elif kind == 3:
            # Bare namespace-scope definition, admitted only at namespace depth.
            if stack and depth == stack[-1][1]:
                prefix = "".join(n + "::" for n, _ in stack)
                defs.add(f"{prefix}{m.group(1)}")
        elif kind == 1:
            names = re.findall(r"[A-Za-z_]\w*", m.group(1) or "")
            depth += 1
            for n in names:
                stack.append((n, depth))
        else:
            if txt[m.start()] == "{":
                depth += 1
            else:
                depth -= 1
                while stack and stack[-1][1] > depth:
                    stack.pop()

    return defs


def build_index():
    """qualified name -> [files that appear to define it]"""
    idx = collections.defaultdict(set)
    files = source_files()
    for n, f in enumerate(files, 1):
        for d in definitions_in(f):
            idx[d].add(f)
        if n % 250 == 0:
            print(f"  indexed {n}/{len(files)} files...", file=sys.stderr)
    return idx, len(files)


def symbol_key(sym):
    """`OHOS::Ace::JsonUtil::GetJsonObj(int)` -> `OHOS::Ace::JsonUtil::GetJsonObj`"""
    i = sym.find("(")
    if i != -1:
        sym = sym[:i]
    # Strip Itanium ABI tags: e.g. `[abi:cxx11]`
    i = sym.find("[abi")
    if i != -1:
        sym = sym[:i]
    return sym.rstrip()


def excluded_paths():
    """The closure paths CMakeLists excludes, read from CMakeLists itself.

    Not hardcoded here. A second copy of this list is a second thing to forget
    to update, and the failure mode is quiet: the excluded file re-enters the
    build and the build fails on a missing cJSON/IPC/Skia header again.
    """
    cm = os.path.join(os.path.dirname(os.path.abspath(__file__)), "CMakeLists.txt")
    try:
        txt = open(cm, errors="replace").read()
    except OSError:
        return set()
    m = re.search(r"set\(ACE_CLOSURE_EXCLUDED_ROUND1(.*?)\)", txt, re.S)
    if not m:
        return set()
    return {os.path.normpath(p) for p in re.findall(r"\$\{ACE\}/(\S+\.cpp)", m.group(1))}


def read_closure_file(path):
    """Existing closure entries, header comments dropped.

    Entries are validated on the way IN, not only on the way out. An earlier
    version of --merge checked the freshly proposed paths and then blindly
    concatenated whatever the file already held, so 187 corrupt entries written
    by a previous buggy run survived every later run and the tool reported a
    clean merge while closure_sources.txt stayed unbuildable. Reading is the only
    place that can catch that.
    """
    if not os.path.isfile(path):
        return []
    out, bad = [], []
    for line in open(path, errors="replace"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("..") or line.startswith("/") or not line.endswith(".cpp"):
            bad.append(line)
            continue
        if not os.path.isfile(os.path.join(ROOT, line)):
            bad.append(line)
            continue
        out.append(line)
    if bad:
        print(f"  DROPPED {len(bad)} invalid entries already in {os.path.basename(path)}:")
        for b in bad[:5]:
            print(f"      {b}")
        if len(bad) > 5:
            print(f"      ... and {len(bad) - 5} more")
    return out


def write_closure_file(path, entries, log_line):
    hdr = f"""# Link closure for ace_ng_layout_probe -- GENERATED, do not hand-edit.
#
# Advance a round with:
#     cmake --build <build> --target ace_ng_layout_probe   # capture the link log
#     python3 linkclosure.py <link.log> --merge             # add this round's files
#
{log_line}
#
# Paths are relative to the arkui/ checkout. A path here is a CANDIDATE: the
# library is static, so an unnecessary entry costs one compile and nothing else,
# and the link is the only authority on which are real.
"""
    with open(path, "w") as f:
        f.write(hdr)
        for e in entries:
            f.write(e + "\n")


def main():
    logs = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if not logs:
        print(__doc__)
        return 2

    undefined = []
    for lg in logs:
        if not os.path.isfile(lg):
            print(f"  LOG DOES NOT EXIST: {lg} -- refusing to report a closure for it")
            return 1
        for line in open(lg, errors="replace"):
            m = UNDEF.search(line)
            if m:
                undefined.append(m.group(1))
    undefined = sorted(set(undefined))

    if "--index-only" in flags:
        idx, n = build_index()
        print(f"  indexed {n} files -> {len(idx)} qualified names")
        return 0

    print(f"  undefined symbols: {len(undefined)}")

    # Only index the tree if we must; --external skips the expensive part.
    idx = None
    if "--external" not in flags:
        idx, nfiles = build_index()
        print(f"  indexed {nfiles} .cpp files -> {len(idx)} qualified names")

    if idx is None:
        print("  (external-only mode: not searching the tree)")
        return 0

    # Platform policy for the ambiguous bucket. Most of it is NOT arbitrary:
    # ace_engine carries parallel per-platform implementations of the same
    # symbol, and this build already had to choose between two of them before
    # this tool existed (see ACE_PLATFORM_SOURCES in CMakeLists.txt):
    #
    #   system_properties  preview   the host implementation. The ohos one
    #                                hard-codes OpenHarmony flags.
    #   log_wrapper        ohos      chosen because there is NO preview
    #                                equivalent -- not a preference.
    #
    # So the rule is: prefer preview/, and take ohos/ only where preview has no
    # such file. That is a stated policy, applied mechanically, rather than 157
    # symbols resolved by hand and quietly getting it wrong. --platform=ohos
    # flips the preference for a case where the ohos side is genuinely the
    # better host (e.g. an OSAL shim that must exist).
    prefer = "adapter/preview/" if "ohos" not in flags else "adapter/ohos/"

    # Second, separate axis: the RENDERER seam. ace_engine ships parallel
    # `fake_animation_utils.cpp` and `rosen_animation_utils.cpp` (and the same
    # for other adapter classes), where "fake" is the no-rasteriser stand-in.
    # This build already made that choice -- it compiles with -DACE_UNITTEST=1,
    # which makes components_ng/render/drawing.h swap the whole Skia/Rosen
    # include block for the in-tree mock (see controls C5). Picking the rosen_
    # file here while the headers already say "no renderer" would be incoherent:
    # it would pull Rosen/Skia back in through the back door and fail to compile.
    # So fake_* wins, and for the same reason the preview adapter wins above --
    # it is the side this build has already committed to.
    RENDER_FAKE = ("/fake_", "/mock_", "/testing_")
    RENDER_REAL = ("/rosen_", "/skia_")

    resolved, unresolved, ambiguous, by_policy, multi = {}, [], [], [], []
    for sym in undefined:
        key = symbol_key(sym)
        cands = idx.get(key, set())
        if len(cands) == 1:
            # os.path.relpath(f, ROOT) is the invariant every downstream stage
            # relies on. Without it, absolute paths survive into closure_sources.txt
            # and CMake silently ignores the entries that are not under ${ACE}.
            resolved[key] = os.path.relpath(next(iter(cands)), ROOT)
        elif len(cands) > 1:
            rel = sorted(os.path.relpath(c, ROOT) for c in cands)
            # Renderer seam first: it is the more specific rule.
            fakes = [p for p in rel if any(t in p for t in RENDER_FAKE)]
            reals = [p for p in rel if any(t in p for t in RENDER_REAL)]
            if len(fakes) == 1 and len(reals) >= 1:
                resolved[key] = fakes[0]
                by_policy.append((key, fakes[0], rel, "renderer seam -> fake_"))
                continue
            pref = [p for p in rel if prefer in p]
            if len(pref) == 1:
                resolved[key] = pref[0]
                by_policy.append((key, pref[0], rel, "platform -> preview/"))
                continue
            if len(pref) > 1:
                # Several files in the PREFERRED platform define this symbol.
                # resource_adapter_impl.cpp and resource_adapter_impl_standard.cpp
                # both define ResourceAdapter::Create, under different build
                # guards (_STANDARD vs the default). Declining to choose here
                # strands a symbol the build can satisfy, so instead take them
                # all: the library is static, so extra members cost a compile and
                # the link drops whichever the guards leave empty. Recorded as a
                # distinct rule so the choice is visible rather than implied.
                # Keys into `resolved` are always the symbol. The multi-candidate
                # case needs two files for ONE symbol, so the files are collected
                # in a side list rather than by inventing compound keys -- that
                # earlier approach put a file path where a symbol belonged, and
                # relpath() on it produced "../ace-host/adapter/..." paths, which
                # then got merged into closure_sources.txt as 38 broken entries.
                multi.extend(pref)
                by_policy.append((key, " + ".join(pref), rel,
                                  "platform -> preview/ (multiple, taking all)"))
                continue
            ambiguous.append((key, rel))
        else:
            unresolved.append(key)

    # Every value in `resolved` and every entry in `multi` is ALREADY relative to
    # ROOT (enforced where each is inserted), so this must not re-apply relpath:
    # relpath("adapter/x.cpp", "/abs/arkui") resolves the first argument against
    # the CWD and yields "../ace-host/adapter/x.cpp". That mistake wrote 38
    # unbuildable lines into closure_sources.txt before the guard below caught it.
    union = sorted(set(resolved.values()) | set(multi))
    bad = [p for p in union if p.startswith("..") or p.startswith("/")]
    if bad:
        print(f"  REFUSING: {len(bad)} candidate paths escape the arkui root:")
        for p in bad[:5]:
            print(f"      {p}")
        print("  This is a bug in the tool, not a real candidate: os.walk(ROOT)")
        print("  cannot yield a path outside ROOT. Debug before writing the file.")
        return 1
    print(f"  found in exactly one file: {sum(1 for k in resolved if k not in dict((b[0],1) for b in by_policy))}")
    print(f"  resolved by platform policy ({prefer}): {len(by_policy)}")
    print(f"  still ambiguous:          {len(ambiguous)}")
    print(f"  found in NO file:         {len(unresolved)}")
    print(f"  union of candidate files: {len(union)}")
    if by_policy:
        rules = collections.Counter(r for _, _, _, r in by_policy)
        for r, n in rules.most_common():
            print(f"      {n:4d}  {r}")
        print("    (--platform=ohos flips the platform rule)")

    if unresolved:
        print(f"\n  NOT FOUND in the tree ({len(unresolved)}):")
        for k in unresolved[:40]:
            print(f"      {k}")
        if len(unresolved) > 40:
            print(f"      ... and {len(unresolved) - 40} more")
    if ambiguous:
        print(f"\n  AMBIGUOUS, defined in more than one file ({len(ambiguous)}):")
        for k, c in ambiguous[:10]:
            print(f"      {k}")
            for f in c[:4]:
                # These paths are already relative to ROOT (built that way above).
                # Calling relpath on them again produced "../ace-host/..." and hid
                # which side of the tree each candidate was on.
                print(f"          {f}")

    # `union` is already ROOT-relative (see where it is built). Do NOT relpath it
    # again: that was the second occurrence of the bug that put 187 "../ace-host/"
    # entries into closure_sources.txt, and fixing only the first call site left
    # this one to reintroduce it on every run.
    proposed = list(union)

    if "--cmake" in flags:
        # Relative to the arkui root, with NO ${ACE} prefix. An earlier version
        # printed "${ACE}path" (no slash, because os.path.relpath has none) and
        # pasting that into a CMake list produced
        #     "/home/.../arkui//home/.../arkuiadapter/..."
        # -- a doubled path that CMake rejects as legacy variable expansion. The
        # prefix belongs to whoever assembles the file, not to the data.
        print("\n  # candidates -- the link decides which are real:")
        for p in proposed:
            print(f"    {p}")

    if "--merge" in flags:
        here = os.path.dirname(os.path.abspath(__file__))
        cfile = os.path.join(here, "closure_sources.txt")
        excl = excluded_paths()
        existing = read_closure_file(cfile)
        have = {os.path.normpath(e) for e in existing}

        added, dup, blocked = [], 0, []
        for p in proposed:
            pn = os.path.normpath(p)
            if pn in have:
                dup += 1
                continue
            if pn in excl:
                blocked.append(p)
                continue
            have.add(pn)
            added.append(p)

        log = (f"# last round: {len(undefined)} undefined symbols proposed "
               f"{len(proposed)} files; {len(added)} new, {dup} already present, "
               f"{len(blocked)} skipped as excluded.\n"
               f"# cumulative closure: {len(have)} files.")
        write_closure_file(cfile, sorted(have), log)
        print(f"\n  merged into {os.path.basename(cfile)}:")
        print(f"    added:            {len(added)}")
        print(f"    already present:  {dup}")
        print(f"    skipped (excluded in CMakeLists): {len(blocked)}")
        for b in blocked:
            print(f"        {b}")
        print(f"    closure total:    {len(have)}")

    print("\n  REMINDER: candidates, not a confirmed closure. Only the next link "
          "confirms\n  any of this, and files that fail to compile become exclusions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
