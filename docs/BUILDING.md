# Building CodeOS on Linux, macOS and Android/Termux

This guide covers building CodeOS **from source on your own machine**, from any
checkout path. The build is host-agnostic: the kernel is compiled with `clang`
for an explicit target, generated files are recreated locally, and optional
tools are detected rather than assumed.

> **Terminology:** "host" is the machine compiling CodeOS. "Target" is the
> architecture of the kernel you are building (`x86_64` or `arm64`). You can
> build either target from either host, provided you install the right tools.

## 1. Pick a target

| Target | Command | Status |
|---|---|---|
| `x86_64` (default) | `make -C kernel all` | Full kernel, desktop, Rust/OpenWeb, Qt panels, ISO |
| `arm64` / `aarch64` | `make -C kernel ARCH=arm64 all` | **CodeOS ARM64** early port: serial, ramfb, virtio input and a CodeOS shell. Add `ARM64_UI=zircon` for the standalone Zircon mobile demo. |

The ARM64 target is intentionally **much smaller** than the x86_64 one: no
userspace, no Rust, no Qt, no filesystem. See [HARDWARE.md](HARDWARE.md) for
the exact support matrix. It is the target to use on an aarch64 phone or on
Apple Silicon when you want a bootable image quickly.

## 2. Prerequisites by host OS

Every host needs: `git`, `make`, `python3`, `clang`/`clang++`, and (for the
x86_64 target) `cargo`. Prebuilt static archives ship in the tree, so you do
**not** normally need to rebuild Qt6, OpenSSL or SDL3.

On a **native x86_64 host**, also install `cmake`. It builds the `arkui`
submodule through `ace-host/CMakeLists.txt` into `/bin/ace-layout`, which the
kernel runs at boot and exposes as `ace layout` in the shell. On a cross host
the engine is skipped unless you can supply an x86_64 libstdc++ archive and set
`ACE_BUILD=1`; the skip is reported by `make -C kernel ace-check` rather than
silently passing. The ARM64 image never embeds the x86_64 engine ELF.

### Linux — Arch / Manjaro / Arch Linux ARM

```sh
sudo pacman -Syu base-devel clang lld python rust \
    qemu-system-x86-64 qemu-system-aarch64 xorriso e2fsprogs parted
```

For a native aarch64 host (e.g. an Android phone running Arch Linux ARM) the
`qemu-system-aarch64` package is only needed to *run* the ARM64 image.

### Linux — Debian / Ubuntu / Termux-on-a-Debian-chroot

```sh
sudo apt install build-essential clang lld python3 cargo \
    qemu-system-x86 qemu-system-arm xorriso e2fsprogs parted
```

The kernel link prefers `x86_64-elf-ld` when installed and otherwise uses the
host GNU `ld` (x86_64 hosts) or `ld.lld` (cross hosts). Optional cross binutils
from your distro are only needed if you want to rebuild Qt6 from source.

### macOS (Intel or Apple Silicon)

Install Xcode Command Line Tools and Homebrew, then:

```sh
xcode-select --install
brew install llvm make python xorriso e2fsprogs parted qemu
```

Apple clang alone is enough for the ARM64 build, but install the Homebrew LLVM
toolchain so `ld.lld` and the `aarch64-linux-gnu` target are available:

```sh
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

### Android / Termux (aarch64)

```sh
pkg update
pkg install clang make python rust binutils qemu-system-aarch64 \
    xorriso e2fsprogs parted
```

Termux uses its own prefix, so never hardcode `/usr/bin` or `/usr/include` in
your build commands. The Makefiles already avoid that. Notes:

- The ARM64 build uses `clang --target=aarch64-linux-gnu`; Termux's LLVM
  normally supports this target out of the box.
- The x86_64 build additionally needs the Rust `x86_64-unknown-none` target
  (see below) and an x86_64-capable linker (`ld.lld`).
- QEMU under Termux is headless-friendly: use `arm64-run-nographic` and the
  `-serial stdio` targets. Graphical output needs an X11 or Termux:X11 server.

## 3. Get the source

```sh
git clone --recurse-submodules <this-repo> CodeOS
cd CodeOS
```

If you already cloned without submodules:

```sh
git submodule update --init --depth 1
```

Two submodules are declared: `Zircon` (the native GUI toolkit, needed for
`make check`'s `move-on-check`) and `arkui`. Older commits also contained
gitlinks for `third_party/*` that were never declared in `.gitmodules`; those
have been removed so `git submodule status` works cleanly.

Optional sibling checkouts (`../bas`, `../OpenWeb`) enable extra targets. Their
absence is reported as a skip, not an error.

## 4. Build

### One-shot build (x86_64)

```sh
./build.sh
```

This builds the kernel + Limine ISO and copies the artifacts to `~/codeos/`.
`build.sh` derives the checkout path from its own location, so it works from
any cwd and any directory name.

### Direct make targets

```sh
make -C kernel all                  # kernel ELF + userspace programs
make -C kernel codeos-1.0.iso       # bootable Live ISO
make -C kernel ARCH=arm64 all       # CodeOS ARM64 kernel ELF + flat binary
make -C kernel ARCH=arm64 ARM64_UI=zircon all  # Zircon mobile demo instead
make -C kernel ARCH=arm64 arm64-run-display   # boot it in QEMU with ramfb
```

Artifacts:

| File | Meaning |
|---|---|
| `kernel/codeos-1-kernel.bin` | x86_64 kernel ELF (Multiboot2) |
| `kernel/codeos-1-kernel-arm64.bin` | CodeOS aarch64 kernel ELF |
| `kernel/codeos-1-kernel-arm64.flat.bin` | raw CodeOS aarch64 image |
| `kernel/codeos-1-kernel-arm64-zircon.bin` | Zircon ARM64 mobile-demo ELF (only with `ARM64_UI=zircon`) |
| `kernel/codeos-1.0.iso` | Limine Live ISO (x86_64) |
| `kernel/disk.img` | populated ext2 data disk shipped on the ISO |

The ISO carries `disk.img`, but the kernel **cannot mount a file inside the
ISO** — every `run-iso*` QEMU target attaches the image explicitly with
`-drive file=kernel/disk.img,format=raw,if=ide`. See
[VIRTUALIZATION.md](VIRTUALIZATION.md).

### Parallelism

Use `JOBS` to cap build parallelism:

```sh
make -j4
make JOBS=4 qt6-build
```

`JOBS` is detected portably (`nproc`, then `getconf _NPROCESSORS_ONLN`, then 2).
Shell scripts that build QEMU honor the `JOBS` environment variable too.

## 5. Test

```sh
make check
```

This runs every host-side check. Individual checks can be run on their own,
e.g. `make jengine-check`, `make ncvm-check`, `make codec-check`,
`make device-check`, `make move-on-check`, `make ace-check`.

`make ace-boot-check` boots the x86_64 ISO and asserts that the engine ran both
from the boot probe and from `ace layout` in the shell. It needs QEMU and a
built `codeos-1.0.iso`; it is intentionally separate from `make check` because
boot checks are slower and need a working serial console.

Expected skips are normal and are reported as `SKIP`:

- `bas-check` / `bas-target-check` — no `../bas` checkout.
- `swift-check` — no `swiftc`.
- `ncvm` pause/resume state assertions — skipped when SIGSTOP state is not
  observable (macOS has no `/proc`, and some sandboxes ignore SIGSTOP). The
  portable part of that test still runs: the signal target and the supervisor
  isolation are checked, and SIGCONT delivery is verified via its trap marker.

A summary line looks like `checks=111 failed=0 skipped=3`. Only `failed > 0`
fails the build.

### Cross-host x86_64 builds

When building the x86_64 target on an aarch64 host, the Makefiles pass
`--target=x86_64-linux-gnu` to clang automatically. Two prerequisites apply:

1. Install the Rust bare-metal target. Distro Rust packages often ship only
   the host target, and `rustup` may not be present:

   ```sh
   rustup target add x86_64-unknown-none
   ```

   On Arch Linux ARM without `rustup`, use an AUR package that provides
   `x86_64-unknown-none` (for example `rust-x86_64-unknown-none`) or install
   rustup from your package manager first.

2. The hosted C++ translation units use the host GCC's libstdc++ headers
   (`/usr/include/c++/...` on Linux). On macOS/Termux these paths differ, so
   the full Qt desktop build is currently best done on Linux. The ARM64 target
   avoids these headers entirely and works on every host.

## 6. Portability fixes baked into the tree

The following used to make fresh checkouts fail on other machines. They are
now fixed and should stay fixed:

- **Generated dependency files are no longer tracked.** 482 `.d` files with
  absolute paths from the original `/home/codeosuser/CodeOS` checkout were
  removed from git and are covered by `*.d` in `.gitignore`. They are
  regenerated by `-MMD -MP` on the next compile.
- **`kernel/kernel/embed_kernel.S` is generated**, not source. It is created by
  `kernel/scripts/gen_embed.py` with a path relative to the build, and is
  gitignored.
- **Qt `moc` is auto-detected** across Arch, Debian, Homebrew and Termux
  layouts. Override with `make MOC=/path/to/moc`.
- **Hardcoded paths in scripts/sysroot configs were replaced** with paths
  derived from each file's own location or with environment overrides
  (`CODEOS_ROOT`, `ACE_ROOT`, `CODEOS_CONTAINER_DIR`, `CDP_CC`, ...).
- **`nproc` is no longer required** on macOS/Termux.
- **The `x86_64-linux-gnu-gcc` CDP bridge compiler falls back** to the host C
  compiler (override with `CDP_CC`).

## 7. Troubleshooting

**"No rule to make target /home/.../CodeOS/foo.d"**
You have stale generated artifacts from an old checkout. Run `make clean` (or
remove the `.d` files) and rebuild. They are gitignored now, so they will not
come back from a fresh clone.

**"moc not found"**
Install Qt6 development tools (`qt6-base-tools`, `qt6-tools`, or Homebrew
`qt6`) or pass `MOC=/path/to/moc`.

**"can't find crate for core / x86_64-unknown-none target may not be installed"**
You are cross-building x86_64 from a non-x86_64 host. Install the Rust target
as described above, or build the ARM64 target instead.

**`move-on-check` fails with "can't open Zircon/scripts/test_move_on.py"**
The `Zircon` submodule is not initialized. Run `git submodule update --init
--depth 1 Zircon`.

**SIGSTOP lifecycle checks fail**
This is an environment limitation, not a source bug. The suite now detects it
and reports `SKIP`; if you still see `FAIL`, verify that `/proc/<pid>/stat`
reports `T` for a SIGSTOPped child on your host.
