#!/usr/bin/env python3
"""Pin Windowland's compositor wiring.

The Rust path, the C fallback and Qt must all agree on one frame entry point:
`windowland_frame(&m_wm)`. The bridge then composites X11 surfaces and paints
the HyperDE chrome overlay. This is a source-level wiring check; the runtime
frame ABI is exercised by `make rust-fallback-check`.

Run with `--negative` to prove the assertions are sensitive: the chrome stage
is removed from a copied bridge source and the same check must fail.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FILES = {
    "header": ROOT / "kernel/kernel/penrose_bridge.h",
    "bridge": ROOT / "kernel/kernel/penrose_bridge.c",
    "compositor": ROOT / "kernel/kernel/rust_penrose/src/compositor.rs",
    "codeos": ROOT / "kernel/kernel/rust_penrose/src/codeos.rs",
    "fallback": ROOT / "kernel/rust_fallback.c",
    "qt": ROOT / "qt6/panels/qt_panels_manager.cpp",
}

CHROME_CALL = "    hyperde_shell_pump(wm);"


def function_body(text: str, signature: str) -> str | None:
    start = text.find(signature)
    if start < 0:
        return None
    brace = text.find("{", start)
    if brace < 0:
        return None
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : i]
    return None


def check(texts: dict[str, str]) -> list[str]:
    failures: list[str] = []

    def want(cond: bool, message: str) -> None:
        if not cond:
            failures.append(message)

    header = texts["header"]
    bridge = texts["bridge"]
    compositor = texts["compositor"]
    codeos = texts["codeos"]
    fallback = texts["fallback"]
    qt = texts["qt"]

    want("void prs_compose_frame(void *wm);" in header,
         "penrose_bridge.h does not declare prs_compose_frame")

    body = function_body(bridge, "void prs_compose_frame(void *wm)")
    want(body is not None, "penrose_bridge.c does not define prs_compose_frame")
    if body is not None:
        want("prs_present_frame();" in body,
             "prs_compose_frame does not composite X11 surfaces")
        want("hyperde_shell_pump(wm);" in body,
             "prs_compose_frame does not paint the HyperDE chrome overlay")

    want("windowland_frame(&m_wm);" in qt,
         "Qt frame loop does not call windowland_frame(&m_wm)")
    direct_call = re.compile(
        r"^\s*(?:extern\s+[\w* ]+\s+)?(?:xs_present|hyperde_shell_pump)\s*\(",
        re.MULTILINE,
    )
    want(not direct_call.search(qt),
         "Qt frame loop still calls xs_present()/hyperde_shell_pump() directly")

    want("prs_compose_frame(wm)" in compositor,
         "Rust compositor does not call prs_compose_frame")
    want("prs_present_frame" not in compositor,
         "Rust compositor still bypasses the full-frame bridge")
    want("windowland_frame(wm: *mut ::core::ffi::c_void)" in codeos,
         "windowland_frame no longer accepts the shell window-manager pointer")
    want("crate::compositor::compose_frame(wm)" in codeos,
         "windowland_frame does not commit a composed frame")
    want("prs_init();" in fallback,
         "C fallback does not initialize the presentation bridge")
    want("void windowland_pump(void) { windowland_frame(0); }" in fallback,
         "C fallback no longer mirrors the compositor frame ABI")
    want("void windowland_frame(void *wm) { prs_compose_frame(wm); }" in fallback,
         "C fallback does not route frames through prs_compose_frame")

    return failures


def main() -> int:
    negative = "--negative" in sys.argv[1:]
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and not negative):
        print("usage: compositor_check.py [--negative]", file=sys.stderr)
        return 2

    texts = {name: path.read_text() for name, path in FILES.items()}

    if negative:
        if texts["bridge"].count(CHROME_CALL) != 1:
            print("negative control could not find the unique chrome stage call", file=sys.stderr)
            return 2
        texts["bridge"] = texts["bridge"].replace(CHROME_CALL, "")
        failures = check(texts)
        if not failures:
            print("FAIL: compositor negative control passed -- assertions are not sensitive",
                  file=sys.stderr)
            return 1
        print(f"negative control: chrome stage removed fails {len(failures)} assertions (expected)")
        return 0

    failures = check(texts)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print("compositor wiring: all assertions passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
