#!/usr/bin/env python3
"""Pin the optional Rust backends on both kernel architectures.

CodeOS builds the same two no-std crates for two triples:

  * x86_64-unknown-none  (the normal x86_64 kernel image)
  * aarch64-unknown-none (the arm64 kernel image / aarch64 host builds)

The Makefile must find rustc/cargo through PATH or the standard rustup shims,
must not hard-code an x86_64 toolchain directory, must only pass x86 target
features to the x86_64 triple, and must let arm64 link the aarch64 archives
rather than emptying RUST_LIB/RUST_PR_LIB. This script also compiles each crate
for every installed target with the same rustflags the Makefile uses and does
an arm64 dry-run link.

Four reverted controls prove the check is sensitive:

  * restore the old hard-coded x86_64 toolchain path and make the Makefile
    assertion fail;
  * empty the arm64 archive variables and make the Makefile assertion fail;
  * restore the stale x86_64 ow_engine.o to the arm64 object list;
  * remove the x86_64-only cfg around the COM1 port logger and make the
    aarch64 metadata compile fail on the invalid x86 registers.

Run with no arguments. Targets that are not installed are skipped with a
message so a Rust-free checkout can still run `make check`.
"""
from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
KERNEL = ROOT / "kernel"
MAKEFILE = KERNEL / "Makefile"
OW_SRC = KERNEL / "kernel/rust_ow/src"
PR_SRC = KERNEL / "kernel/rust_penrose/src"

TARGETS = ("x86_64-unknown-none", "aarch64-unknown-none")
X86_FEATURES = "-C target-feature=-mmx,-sse,-sse2,+soft-float"

# The aarch64 port uses the PL011, not x86 port I/O. Removing these two guards
# is the negative control: rustc must then reject `out dx, al` on aarch64.
X86_ASM_GUARDS = (
    '#[cfg(target_arch = "x86_64")]\n    #[inline(always)]\n    unsafe fn outb',
    '#[cfg(target_arch = "x86_64")]\n    #[inline(always)]\n    unsafe fn inb',
)
HARDCODED_TOOLCHAIN = (
    "RUST_TOOLCHAIN = $(HOME)/.rustup/toolchains/1.92.0-x86_64-unknown-linux-gnu/bin"
)


def find_tool(name: str, override: str | None) -> str | None:
    for candidate in (override, shutil.which(name), str(pathlib.Path.home() / ".cargo/bin" / name)):
        if not candidate:
            continue
        resolved = shutil.which(candidate) or candidate
        path = pathlib.Path(resolved)
        if path.exists() and os.access(path, os.X_OK):
            return resolved
    return None


def rust_env() -> dict[str, str]:
    env = os.environ.copy()
    shim = str(pathlib.Path.home() / ".cargo/bin")
    path = env.get("PATH", "")
    if shim not in path.split(os.pathsep):
        env["PATH"] = shim + os.pathsep + path if path else shim
    return env


def rustflags(target: str) -> list[str]:
    flags = ["-C", "no-redzone"]
    if target == "x86_64-unknown-none":
        flags += ["-C", "target-feature=-mmx,-sse,-sse2,+soft-float"]
    return flags


def check_makefile(text: str) -> list[str]:
    failures: list[str] = []

    def want(cond: bool, message: str) -> None:
        if not cond:
            failures.append(message)

    want(HARDCODED_TOOLCHAIN not in text,
         "Makefile still hard-codes an x86_64 host toolchain path")
    want("RUSTC ?= $(shell command -v rustc" in text,
         "Makefile does not discover rustc through PATH/rustup shims")
    want("CARGO ?= $(shell command -v cargo" in text,
         "Makefile does not discover cargo through PATH/rustup shims")
    want("RUST_TARGET_LIBDIR := $(shell $(RUSTC) --print target-libdir" in text,
         "Rust target detection does not use $(RUSTC)")
    want("ifeq ($(ARCH),arm64)\nRUST_TARGET ?= aarch64-unknown-none" in text,
         "arm64 builds do not select aarch64-unknown-none")
    want("else\nRUST_TARGET ?= x86_64-unknown-none" in text,
         "x86_64 builds do not select x86_64-unknown-none")
    want("RUSTFLAGS = -C no-redzone" in text,
         "Rust builds no longer use the architecture-neutral no-redzone flag")
    want('ifeq ($(RUST_TARGET),x86_64-unknown-none)' in text and
         X86_FEATURES in text,
         "x86-only target features are no longer gated on the x86_64 triple")
    want(text.count("$(CARGO) build --target $(RUST_TARGET) --release") >= 2,
         "both Rust crate recipes must use the discovered $(CARGO)")
    want('PATH="$(RUST_TOOLCHAIN):$$PATH"' not in text,
         "Rust recipe still prepends a hard-coded toolchain directory")
    want("ifeq ($(USE_RUST),1)\nRUST_LINK = $(RUST_LIB) $(RUST_PR_LIB)" in text,
         "Rust archives are not selected for both architectures")
    want("RUST_LIB :=" not in text and "RUST_PR_LIB :=" not in text,
         "arm64 overrides still empty the Rust archives")
    want("OW_ENGINE_OBJ = ../pkgs/core/openweb/src/ow_engine-arm64.o" in text and
         "filter-out ../src/jengine.o ../pkgs/core/openweb/src/ow_engine.o" in text,
         "arm64 still links the stale x86_64 ow_engine.o")
    return failures


def check_source(text: str) -> list[str]:
    failures: list[str] = []
    for guard in X86_ASM_GUARDS:
        if text.count(guard) != 1:
            failures.append(f"rust_ow panic logger no longer has a unique guard: {guard!r}")
    if '#[cfg(target_arch = "aarch64")]' not in text:
        failures.append("rust_ow panic logger has no aarch64 backend")
    return failures


def target_available(rustc: str, target: str, env: dict[str, str]) -> bool:
    proc = subprocess.run(
        [rustc, "--print", "target-libdir", "--target", target],
        env=env, capture_output=True, text=True,
    )
    if proc.returncode != 0:
        return False
    libdir = pathlib.Path(proc.stdout.strip())
    return libdir.is_dir() and any(libdir.glob("libcore*"))


def compile_metadata(rustc: str, source: pathlib.Path, target: str,
                     output: pathlib.Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [rustc, "--edition", "2021", "-Awarnings", "--crate-type", "lib",
         "--emit", "metadata", *rustflags(target), "--target", target,
         "-o", str(output), str(source)],
        env=env, capture_output=True, text=True,
    )


def check_arm64_link(env: dict[str, str]) -> list[str]:
    """Dry-run the arm64 image with Rust forced on and inspect the link line."""
    failures: list[str] = []
    if shutil.which("make") is None:
        print("make not found; skipping the arm64 link dry-run")
        return failures
    proc = subprocess.run(
        ["make", "-C", str(KERNEL), "-B", "-n",
         "ARCH=arm64", "USE_RUST=1", "codeos-1-kernel-arm64.bin"],
        env=env, capture_output=True, text=True,
    )
    if proc.returncode != 0:
        failures.append("arm64 Rust link dry-run failed:\n" + proc.stderr.strip())
        return failures
    out = proc.stdout
    for needle, message in (
        ("--target aarch64-unknown-none", "arm64 dry-run does not build the aarch64 Rust target"),
        ("libow_http.a", "arm64 link line omits libow_http.a"),
        ("libwindowland.a", "arm64 link line omits libwindowland.a"),
    ):
        if needle not in out:
            failures.append(message)
    for stale in ("x86_64-unknown-none", "-C target-feature=-mmx"):
        if stale in out:
            failures.append(f"arm64 dry-run unexpectedly contains x86-only {stale}")
    return failures


def main() -> int:
    makefile_text = MAKEFILE.read_text()
    failures = check_makefile(makefile_text)

    # Negative control 1: the pre-nightly/pre-aarch64 Makefile must be caught.
    broken_makefile = re.sub(
        r"^RUSTC \?= .*$", HARDCODED_TOOLCHAIN, makefile_text, count=1, flags=re.M,
    )
    if broken_makefile == makefile_text or not check_makefile(broken_makefile):
        failures.append("negative control: reverted hard-coded toolchain path still passed")
    else:
        print("negative control: hard-coded toolchain path fails the Makefile assertions (expected)")

    # Negative control 2: the old arm64 overrides deliberately emptied both
    # archive paths, so an aarch64 nightly install could be detected but never
    # linked. The assertion must reject that shape.
    broken_arm64 = makefile_text.replace(
        "ifeq ($(USE_RUST),1)\nRUST_LINK = $(RUST_LIB) $(RUST_PR_LIB)",
        "ifeq ($(USE_RUST),1)\nRUST_LIB :=\nRUST_PR_LIB :=\nRUST_LINK = $(RUST_LIB) $(RUST_PR_LIB)",
        1,
    )
    if broken_arm64 == makefile_text or not check_makefile(broken_arm64):
        failures.append("negative control: emptied arm64 Rust archives still passed")
    else:
        print("negative control: emptied arm64 Rust archives fail the Makefile assertions (expected)")

    # Negative control 3: the arm64 image used to link the x86_64 ow_engine.o
    # left in the tree by an x86 build. The assertion must catch that shape.
    broken_obj = makefile_text.replace(
        "OW_ENGINE_OBJ = ../pkgs/core/openweb/src/ow_engine-arm64.o",
        "OW_ENGINE_OBJ = ../pkgs/core/openweb/src/ow_engine.o",
        1,
    )
    if broken_obj == makefile_text or not check_makefile(broken_obj):
        failures.append("negative control: stale x86 ow_engine.o still passed")
    else:
        print("negative control: stale x86 ow_engine.o fails the Makefile assertions (expected)")

    ow_text = OW_SRC.joinpath("lib.rs").read_text()
    failures.extend(check_source(ow_text))

    rustc = find_tool("rustc", os.environ.get("RUSTC"))
    if rustc is None:
        print("rustc not found; skipping Rust metadata compiles and their negative control")
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1 if failures else 0

    env = rust_env()
    failures.extend(check_arm64_link(env))
    available = [t for t in TARGETS if target_available(rustc, t, env)]
    for target in TARGETS:
        if target not in available:
            print(f"note: {target} target is not installed; skipping its metadata compile")

    compiled = 0
    with tempfile.TemporaryDirectory(prefix="codeos-rust-target-") as tmp:
        tmpdir = pathlib.Path(tmp)
        for crate, src in (("ow_http", OW_SRC), ("windowland", PR_SRC)):
            for target in available:
                out = tmpdir / f"{crate}-{target}.rmeta"
                proc = compile_metadata(rustc, src / "lib.rs", target, out, env)
                if proc.returncode != 0:
                    failures.append(f"{crate} failed to compile for {target}:\n{proc.stderr.strip()}")
                else:
                    compiled += 1

        # Negative control 4: without the cfg, the x86 port-I/O logger is
        # compiled on aarch64 and rustc must reject the x86 registers.
        if "aarch64-unknown-none" in available:
            broken_src = tmpdir / "ow-broken" / "src"
            shutil.copytree(OW_SRC, broken_src)
            lib = broken_src / "lib.rs"
            text = lib.read_text()
            broken = text
            for guard in X86_ASM_GUARDS:
                if broken.count(guard) != 1:
                    failures.append("negative control: could not find the unique x86 asm guard")
                    broken = ""
                    break
                broken = broken.replace(guard, "    #[inline(always)]\n    unsafe fn " +
                                        ("outb" if "outb" in guard else "inb"), 1)
            if broken:
                lib.write_text(broken)
                out = tmpdir / "ow-broken-aarch64.rmeta"
                proc = compile_metadata(rustc, lib, "aarch64-unknown-none", out, env)
                if proc.returncode == 0:
                    failures.append(
                        "negative control: x86 port-I/O logger compiled on aarch64"
                    )
                elif "invalid register" not in proc.stderr and "unknown register" not in proc.stderr:
                    failures.append(
                        "negative control: aarch64 compile failed for an unexpected reason:\n"
                        + proc.stderr.strip()
                    )
                else:
                    print("negative control: unguarded x86 logger fails on aarch64 (expected)")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print(f"rust targets: {compiled} crate/target metadata compiles passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
