#!/usr/bin/env python3
"""Static checks for the OpenWeb PWA assets.

`index.html` and `sw.js` reference app.js, modtools.js and the OpenWeb mark.
A missing asset made every button inert and broke the service worker's
cache.addAll(), and no host-side build ever noticed.  This check closes that
gap: it verifies that every local asset is present, that every element ID the
scripts look up exists in the markup, that both JS files have balanced
delimiters outside strings/comments, and that the visible identity matches
pkgs/core/openweb/IDENTITY.json.

It also runs its own negative control so an always-passing scanner cannot give
false confidence.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATIC = ROOT / "OpenWeb" / "static"

failures: list[str] = []
checks = 0


def check(condition: bool, message: str) -> None:
    global checks
    checks += 1
    if not condition:
        failures.append(message)


def read_rel(rel: str) -> str:
    path = ROOT / rel
    check(path.is_file(), f"missing source for {rel}")
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def require_all(text: str, needles: tuple[str, ...], label: str) -> None:
    for needle in needles:
        check(needle in text, f"{label} is missing {needle!r}")


def check_mirror(left: str, right: str, label: str) -> None:
    a = ROOT / left
    b = ROOT / right
    check(a.is_file() and b.is_file(), f"missing {label} mirror source")
    if a.is_file() and b.is_file():
        check(a.read_bytes() == b.read_bytes(), f"{label} mirror has drifted")


def local_assets(html: str) -> set[str]:
    assets: set[str] = set()
    for match in re.finditer(r'(?:src|href)\s*=\s*["\']([^"\']+)["\']', html):
        value = match.group(1)
        if value.startswith("/static/"):
            assets.add(value)
    return assets


def service_worker_assets(js: str) -> set[str]:
    match = re.search(r"STATIC_ASSETS\s*=\s*\[(.*?)\]", js, re.S)
    if not match:
        return set()
    return set(re.findall(r'["\']([^"\']+)["\']', match.group(1)))


def strip_js(text: str) -> str:
    """Replace JS strings/comments/regexes with spaces, keeping delimiters."""
    out: list[str] = []
    i = 0
    n = len(text)
    # A '/' starts a regex when the previous significant char cannot end an
    # expression.  Good enough for this file's syntax-checking purpose.
    regex_allowed = True
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            regex_allowed = True
            continue
        if ch == "/" and nxt == "*":
            out.extend("  ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.extend("  ")
            i += 2
            continue
        if ch in "\"'`":
            quote = ch
            out.append(" ")
            i += 1
            while i < n:
                if text[i] == "\\":
                    out.extend("  ")
                    i += 2
                    continue
                if text[i] == quote:
                    out.append(" ")
                    i += 1
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            regex_allowed = False
            continue
        if ch == "/" and regex_allowed:
            out.append(" ")
            i += 1
            in_class = False
            while i < n:
                if text[i] == "\\":
                    out.extend("  ")
                    i += 2
                    continue
                if text[i] == "[":
                    in_class = True
                elif text[i] == "]":
                    in_class = False
                elif text[i] == "/" and not in_class:
                    out.append(" ")
                    i += 1
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            while i < n and text[i].isalpha():
                out.append(" ")
                i += 1
            regex_allowed = False
            continue
        out.append(ch)
        if not ch.isspace():
            regex_allowed = ch in "=(,:!?[{};+-*%&|<>"
        i += 1
    return "".join(out)


def balanced(text: str) -> tuple[bool, str]:
    pairs = {")": "(", "]": "[", "}": "{"}
    stack: list[tuple[str, int]] = []
    line = 1
    for ch in text:
        if ch == "\n":
            line += 1
        elif ch in "([{":
            stack.append((ch, line))
        elif ch in ")]}":
            if not stack or stack[-1][0] != pairs[ch]:
                return False, f"unexpected '{ch}' on line {line}"
            stack.pop()
    if stack:
        return False, f"unclosed '{stack[-1][0]}' from line {stack[-1][1]}"
    return True, ""


def main() -> int:
    for name in ("index.html", "app.js", "modtools.js", "styles.css",
                 "manifest.json", "sw.js", "openweb-mark.svg"):
        path = STATIC / name
        check(path.is_file(), f"missing asset OpenWeb/static/{name}")

    identity_path = ROOT / "pkgs" / "core" / "openweb" / "IDENTITY.json"
    check(identity_path.is_file(), "missing OpenWeb identity manifest")
    identity = {}
    if identity_path.is_file():
        import json
        try:
            identity = json.loads(identity_path.read_text(encoding="utf-8"))
        except ValueError as err:
            check(False, f"invalid OpenWeb identity manifest: {err}")

    html = (STATIC / "index.html").read_text(encoding="utf-8")
    app = (STATIC / "app.js").read_text(encoding="utf-8")
    modtools = (STATIC / "modtools.js").read_text(encoding="utf-8")
    css = (STATIC / "styles.css").read_text(encoding="utf-8")
    sw = (STATIC / "sw.js").read_text(encoding="utf-8")

    referenced = local_assets(html) | service_worker_assets(sw)
    check(bool(referenced), "no /static assets referenced by index.html or sw.js")
    for asset in sorted(referenced):
        # URLs are served from /static, which the checkout keeps under OpenWeb/.
        local = STATIC / asset[len("/static/"):] if asset.startswith("/static/") else ROOT / asset.lstrip("/")
        check(local.is_file(), f"referenced asset is missing: {asset}")

    ids = set(re.findall(r'\bid\s*=\s*["\']([^"\']+)["\']', html))
    used = set(re.findall(r"\$\('([^']+)'\)", app))
    for missing in sorted(used - ids):
        check(False, f"app.js looks up #{missing}, which is not in index.html")

    check('src="/static/app.js"' in html, "index.html does not load app.js")
    check('src="/static/modtools.js"' in html, "index.html does not load modtools.js")
    check(html.index("/static/modtools.js") < html.index("/static/app.js"),
          "modtools.js must load before app.js")
    check(html.index("/static/app.js") < html.index("</body>"),
          "app.js script tag is outside <body>")
    for asset in ("/static/app.js", "/static/modtools.js", "/static/openweb-mark.svg"):
        check(re.search(r"['\"]" + re.escape(asset) + r"['\"]", sw) is not None,
              f"service worker does not cache {asset}")
    check("serviceWorker.register" in app, "app.js no longer registers the service worker")
    check("OpenWebModTools" in app, "app.js does not use ModTools")
    check("openweb://reader" in app, "app.js no longer has Reader mode")
    check("function reader(" in modtools, "ModTools has no reader renderer")

    for name, source in (("app.js", app), ("modtools.js", modtools)):
        ok, detail = balanced(strip_js(source))
        check(ok, f"{name} delimiters are unbalanced: {detail}")
        broken_ok, _ = balanced(strip_js(source + "\n(function () {\n"))
        check(not broken_ok,
              f"negative control: delimiter scanner accepted broken {name}")

    import json
    try:
        manifest = json.loads((STATIC / "manifest.json").read_text(encoding="utf-8"))
        if identity:
            check(manifest.get("short_name") == identity.get("shortName"),
                  "manifest short_name does not match OpenWeb identity")
            check(manifest.get("name") == identity.get("displayName"),
                  "manifest name does not match OpenWeb identity")
            check(manifest.get("description") == identity.get("description"),
                  "manifest description does not match OpenWeb identity tagline")
            palette = identity.get("palette", {})
            check(manifest.get("theme_color") == palette.get("surface"),
                  "manifest theme_color does not match OpenWeb palette")
            check(manifest.get("background_color") == palette.get("background"),
                  "manifest background_color does not match OpenWeb palette")
            mark = identity.get("mark", "")
            check(mark in html, "OpenWeb mark is not linked from index.html")
            check(any(icon.get("src") == mark for icon in manifest.get("icons", [])),
                  "OpenWeb mark is not in the PWA icon list")
            check(f"--accent: {palette.get('accent')};" in css,
                  "stylesheet accent does not match OpenWeb palette")
            check(f"--accent-2: {palette.get('accent2')};" in css,
                  "stylesheet secondary accent does not match OpenWeb palette")
            check(identity.get("tagline", "").lower() in html.lower(),
                  "OpenWeb tagline is missing from the UI")
        check(manifest.get("start_url", "").startswith("/static/"),
              "manifest start_url is not a local asset")
    except (ValueError, OSError) as err:
        check(False, f"invalid manifest.json: {err}")

    # Native frontends may expose adblock only through the kernel engine that
    # actually enforces DNS/HTTP(S). The PWA has no such hook, so an inert
    # switch there would be a lie; keep it out of the static assets.
    for name in ("index.html", "app.js", "modtools.js", "sw.js", "styles.css"):
        source = (STATIC / name).read_text(encoding="utf-8")
        check(re.search(r"ad\s*block", source, re.I) is None,
              f"PWA {name} has an adblock surface with no enforcement path")

    kernel_sc = read_rel("kernel/kernel/syscall.c")
    require_all(kernel_sc, (
        '#include "ad_block.h"',
        "case WEB_ADBLOCK_STATUS:",
        "case WEB_ADBLOCK_SET:",
        "adblock_is_enabled()",
        "adblock_domain_count()",
        "copy_to_user(user_ptr, &info, sizeof(info))",
    ), "kernel SYSCALL_WEB dispatcher")

    kernel_sc_h = read_rel("kernel/kernel/syscall.h")
    require_all(kernel_sc_h, (
        "#define WEB_ADBLOCK_STATUS 14",
        "#define WEB_ADBLOCK_SET    15",
        "} web_adblock_info_t;",
    ), "kernel syscall ABI")

    userspace_h = read_rel("kernel/userspace/include/unistd.h")
    require_all(userspace_h, (
        "#define WEB_ADBLOCK_STATUS 14",
        "#define WEB_ADBLOCK_SET    15",
        "sys_web_adblock_status(web_adblock_info_t *info)",
        "sys_web_adblock_set(int enabled)",
    ), "userspace SYSCALL_WEB ABI")

    tty = read_rel("pkgs/core/openweb/src/openweb.c")
    require_all(tty, (
        "render_adblock",
        "sys_web_adblock_status(&info)",
        "sys_web_adblock_set(!info.enabled)",
        "Toggle AdBlock (kernel enforcement)",
        "a AdBlock",
    ), "OpenWeb TTY frontend")
    check(re.search(r"g_url\[0\] == 0 && \(key == 'a' \|\| key == 'A'\)", tty) is not None,
          "TTY AdBlock hotkey is not guarded by an empty URL field")

    core_h = read_rel("qt6/panels/openweb_core.h")
    core_c = read_rel("qt6/panels/openweb_core.c")
    require_all(core_h, (
        "} ow_core_adblock_info_t;",
        "ow_core_adblock_status(ow_core_adblock_info_t *info)",
        "ow_core_adblock_set(int enabled)",
    ), "Qt OpenWeb core ABI")
    require_all(core_c, (
        '#include "ad_block.h"',
        "int ow_core_adblock_status(",
        "int ow_core_adblock_set(",
        "adblock_is_enabled()",
        "adblock_domain_count()",
        "adblock_enable()",
        "adblock_disable()",
    ), "Qt OpenWeb core backend")

    qt_h = read_rel("qt6/panels/qt_panels.h")
    qt_cpp = read_rel("qt6/panels/qt_panels_openweb.cpp")
    require_all(qt_h, ("refreshAdblock", "m_adblockRect"), "Qt OpenWeb view ABI")
    require_all(qt_cpp, (
        "refreshAdblock()",
        "ow_core_adblock_status(&info)",
        "ow_core_adblock_set(next)",
        "m_adblockRect.right() + 10",
        "AdBlock %1 (%2 domains)",
    ), "Qt OpenWeb view")

    zircon_sc = read_rel("Zircon/kernel/kernel/syscall.c")
    zircon_sc_h = read_rel("Zircon/kernel/kernel/syscall.h")
    zircon_unistd = read_rel("Zircon/kernel/userspace/include/unistd.h")
    zircon_main = read_rel("Zircon/kernel/kernel/main.c")
    require_all(zircon_sc, (
        '#include "ad_block.h"',
        "case WEB_ADBLOCK_STATUS:",
        "case WEB_ADBLOCK_SET:",
    ), "Zircon SYSCALL_WEB dispatcher")
    require_all(zircon_sc_h, (
        "#define WEB_ADBLOCK_STATUS 14",
        "#define WEB_ADBLOCK_SET    15",
        "} web_adblock_info_t;",
    ), "Zircon syscall ABI")
    require_all(zircon_unistd, (
        "} web_adblock_info_t;",
        "sys_web_adblock_status(web_adblock_info_t *info)",
        "sys_web_adblock_set(int enabled)",
    ), "Zircon userspace SYSCALL_WEB ABI")
    check("adblock_init();" in zircon_main,
          "Zircon exposes adblock controls without initializing the engine")

    check("_redirect_depth" in read_rel("pkgs/core/panels/src/ow_http.h"),
          "C openweb_tab_t lost its trailing _redirect_depth field")
    check("_redirect_depth" in read_rel("kernel/kernel/rust_ow/src/lib.rs"),
          "Rust OpenwebTab lost its trailing _redirect_depth field")

    check_mirror("kernel/kernel/rust_ow/src/lib.rs",
                 "Zircon/kernel/kernel/rust_ow/src/lib.rs",
                 "Rust OpenWeb backend")
    check_mirror("kernel/kernel/rust_ow/src/ow_render.rs",
                 "Zircon/kernel/kernel/rust_ow/src/ow_render.rs",
                 "Rust HTML renderer")
    check_mirror("pkgs/core/openweb/src/ow_html.c",
                 "pkgs/core/panels/src/ow_html.c",
                 "C HTML renderer")

    makefile = read_rel("Makefile")
    check("kernel/tests/ad_block_test.c" in makefile,
          "make openweb-check does not run the canonical adblock matcher test")

    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"openweb ui checks: {checks} checks, {len(failures)} failures")
        return 1
    print(f"openweb ui checks: {checks} checks, 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
