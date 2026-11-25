# CodeOS
A from-scratch hobby operating system with its own kernel, desktop GUI, package
layout, and **ZircApp** app toolkit. Built for people who want to see how an OS
works — not a bajillion distros, one approachable system.

**Version:** 1.5.0.1-t

**Internal Codename:** Andromeda
## Features

- **x86_64 kernel** (experimental ARM64 path) with Limine / Multiboot2 boot
- **Desktop GUI** — menubar, dock, launcher, windows, OpenWeb browser panel
- **ArkUI/ACE** — the real OpenHarmony layout engine is built from `arkui/`,
  embedded as `/bin/ace-layout`, and run at boot and from the `ace` shell command
- **Userspace** programs via initramfs (`init`, `shell`, Zircon helpers, ...)
- **Package tree** under `pkgs/` (manifests + compressed `.xora` app archives)
- **Zircon** — lightweight native C GUI toolkit (`Zircon/`)
- **Networking** — TCP/IP stack, DNS, HTTP/HTTPS (TLS 1.3)
- **Containers** — Docker-like container engine with namespaces and cgroups
- **Android compat** — Binder IPC, ashmem, property system
- **Security** — AdBlock, firewall, file integrity monitoring, ClamAV
- **Automated Driver Detection Subsystem** — runtime hardware identification probes, dynamic loading into volatile RAM, strictly withholding un-detected drivers
- **CLI & Python Tools** — `codeos-cli`, Python `cmatrix`, and analogous Swift `cmatrix`

## Documentation

- [Building on Linux, macOS and Android/Termux](docs/BUILDING.md) — prerequisites,
  submodules, both architectures, cross-host notes and troubleshooting
- [Virtualization guide](docs/VIRTUALIZATION.md) — QEMU (x86_64 and ARM64),
  VirtualBox, libvirt, headless verification and networking
- [Installing on a real PC](docs/INSTALL.md) — Live USB vs. persistent install,
  boot layout, safety notes and known limitations
- [Hardware support matrix](docs/HARDWARE.md) — what each kernel target actually
  drives, and the prioritized work needed for more devices
- [Usability roadmap](docs/ROADMAP.md) — staged plan for OpenWeb, Antartica,
  AMD desktop hardware and the named ARM devices

## Roadmap
1. Make NetBeam for every OS
2. Make it as usable as Linux
3. Add optional AI inside the OS (not paid)
4. Make DevStore more usable
5. Improve Zircon
6. Make NCVM based on QEMU's latest release

### Dependencies (host)

```sh
# Clone with submodules — Zircon is a git submodule:
git clone --recurse-submodules <this-repo>
# If already cloned without it:
git submodule update --init Zircon

# Arch / Manjaro-style
sudo pacman -Syu base-devel clang lld qemu-system-x86_64 xorriso python

# Ubuntu / Debian
sudo apt install build-essential clang lld qemu-system-x86 xorriso python3

# Optional cross binutils (x86_64-elf-gcc / binutils) speed up nothing; when
# x86_64-elf-ld is missing the build discovers ld.lld from PATH (including the
# copy rustup installs).  A bare host ld is used only if it can actually emit
# x86_64 ELF, which is not true on aarch64 hosts.  Install the cross binutils
# from the AUR (x86_64-elf-gcc x86_64-elf-binutils) only if you want the exact
# GNU cross linker, or to rebuild Qt6 from source.
```

**Rust** (`cargo` plus the `x86_64-unknown-none` target for x86_64 images, or
`aarch64-unknown-none` for arm64 images) enables the full OpenWeb backend and
the Windowland compositor (`kernel/kernel/rust_ow` and
`kernel/kernel/rust_penrose`). A nightly rustup install on an aarch64 host works
without extra configuration; the Makefile follows the active `rustc`/`cargo`
proxies and never hard-codes an x86_64 toolchain path. Rust is optional: without
the target the x86_64 image links `kernel/rust_fallback.c`, which keeps the
desktop and frame pump working with a text-only OpenWeb notice and no automatic
window layout. `make rust-target-check` compiles both crates for both triples
and proves the architecture guards are load-bearing.

### Build

```sh
# From repo root — builds kernel + userspace + Limine ISO
./build.sh
# Artifacts land in ~/codeos/  (codeos.bin, codeos.iso, limine files)

# Or use Make directly:
make -C kernel all              # kernel + userspace ELFs
make -C kernel codeos-1.0.iso
make -C kernel run              # QEMU with KVM (needs disk.img)
make -C kernel run-iso          # boot the ISO in QEMU
```

Root `Makefile` shortcuts:

| Target       | What it does                        |
|-------------|--------------------------------------|
| `make`      | Build kernel + ISO                   |
| `make run`  | Build kernel and run in QEMU         |
| `make clean`| Clean kernel/userspace objects       |
| `make help` | Show all available targets           |

### Run in QEMU

```sh
# Asks 1) x86_64 or 2) arm64, builds that target, then boots it in QEMU
./run.sh

# Or select the architecture directly (non-interactive / scripts):
./run.sh -a x86_64       # full kernel + Limine ISO, KVM when available
./run.sh -a arm64 -n     # CodeOS ARM64 kernel, serial only
./run.sh -a arm64 -z     # Zircon mobile demo instead of CodeOS
./run.sh -m 1G -c 8      # override memory and CPU count
./run.sh -s              # skip the rebuild, boot the existing image
```

### Linux applications

The x86_64 Linux personality supports statically linked ELF applications and
the bundled `linux-runner` entrypoint. Copy an application into the CodeOS
filesystem, then run:

```sh
linux-runner debian
linux-runner /path/to/app [arguments]
```

`linux-runner debian` starts the bundled `debian-minimal` rootfs inside CodeOS
namespaces, similar to the isolated userspace role of a crosvm guest. The
compatibility layer currently targets command-line and lightweight userspace
applications; it does not provide a full Linux kernel, glibc ABI, or hardware
driver environment.

### Package management

`fetch` follows a Moss-style repository workflow with cached `pkg.index`
metadata and persistent installed-package state:

```sh
fetch sync -u
fetch search <pattern>
fetch install <package>
fetch fetch <package>
fetch upgrade
```

Installed state is stored in `/.pkg/db`; repository configuration and metadata
are cached under `/.pkg` so package listings and dependency checks survive a
reboot.

### OpenWeb

OpenWeb is the built-in browser package (`pkgs/core/openweb`). The tty app
supports tabs, shared back/forward history, reload/stop, numbered links and
relative URL resolution. Its PWA UI lives in `OpenWeb/static/` and ships with a
service worker, offline cache, tabs, bookmarks, history and settings.

```sh
make openweb-check      # URL joining tests + static UI asset/script checks
```

The package manifest is `pkgs/core/openweb/manifest.json`; installable
`.xora` archives are refreshed with `make xora-all`.

### Move On: transfer data from Zircon to CodeOS

The native Move On app is included in Zircon and in the `zircapp` built by
CodeOS. On Zircon, launch **Move On** and tap **Prepare export** to
copy the phone's user files to
`/root/root/home/Move On/export-NNN`. The app checks the whole tree before
copying and refuses data sets with files over 64 KiB,
unsupported path names, or more than 500 entries (limits of the current
Zircon filesystem APIs). Copy that export directory to a host computer, then
create a portable, checksum-verified bundle and import it into CodeOS:

```sh
python3 Zircon/scripts/move_on.py export /path/to/export-NNN \
  --output /path/to/shared-storage/zircon-data.moveon
python3 Zircon/scripts/move_on.py import /path/to/shared-storage/zircon-data.moveon \
  --destination /path/to/codeos/home/root
```

The importer verifies SHA-256 checksums, rejects unsafe paths and unsupported
file types, and refuses to overwrite an existing destination. Empty
directories and staged file permission bits are preserved. Run `make move-on-check`
to test the export/import flow and its safety checks. The Zircon app is
compiled into the phone launcher as part of both `make all` builds.

### Xora application archives

Installed applications are distributed as `.xora` files: gzip-compressed tar
archives containing a root `manifest.json` and a `payload/` tree. The format
preserves executable permissions while keeping downloaded applications small:

```sh
make xora-pack XORA_SOURCE=build/my-app XORA_OUTPUT=my-app-1.0.xora
make xora-inspect XORA_ARCHIVE=my-app-1.0.xora
make xora-unpack XORA_ARCHIVE=my-app-1.0.xora XORA_DESTINATION=staging/root
make xora-install XORA_ARCHIVE=my-app-1.0.xora XORA_ROOT=staging/root
make xora-audit XORA_ROOT=staging/root
make xora-audit XORA_ROOT=staging/root XORA_REPAIR=1
make xora-all
```

`XORA_SOURCE` should contain `manifest.json` and, for an install-ready app, a
`payload/` directory whose paths are relative to the target filesystem root.
Extraction rejects absolute, parent-traversal, device, and symlink entries.
The `install` operation validates the manifest before placing payload files
under the selected CodeOS root.
Each install records SHA-256 baselines under `.xora/integrity/`. Auditing
reports changed files; repair deletes changed executable/code files only.
`make xora-all` packages every executable in `kernel/userspace/` into
`pkgs/xora/`, including legacy apps that do not yet have a source manifest.

The self-contained terminal IDE can be built and packaged without network
access:

```sh
make offline-ide-package
make xora-inspect XORA_ARCHIVE=pkgs/xora/offline-ide-1.0.0.xora
```

In CodeOS, run `offline-ide [file]` from `csh`. The editor includes C and Swift
templates, local file editing, and build/run attempts using installed local
compilers. Swift source editing is available offline; CodeOS does not yet ship
a native Swift compiler/runtime, so Swift build/run requires one to be added
separately. `make swift-check` tests the host-side Swift toolchain when present.

### Release tarball

```sh
./release.sh   # packs ISO+bin into ~/Downloads/CodeOS-YYYY.MM.DD.tar.gz
```

## Repository layout

```
CodeOS/
├── kernel/           # Kernel sources, drivers, arch, bootloader, userspace build
│   ├── kernel/       # Core kernel C/C++ (mm, sched, syscall, desktop glue, ...)
│   ├── drivers/      # PCI NICs, USB, input, audio, ...
│   ├── arch/         # x86_64 + arm64 boot / low-level
│   ├── gui/          # GUI primitives (desktop, windows, widgets)
│   ├── userspace/    # crt0, libc stubs, link scripts → builds user ELFs
│   └── bootloader/   # Limine assets
├── pkgs/             # Userspace and kernel-injected packages
│   └── core/         # Package definitions and source trees
│       ├── graphics/ # Framebuffer + Virtio-GPU kernel drivers
│       └── panels/   # GUI apps (calc, terminal, OpenWeb, installer, ...)
├── Zircon/           # Zircon toolkit + daemon
├── scripts/          # Helper build scripts (colors, gen_initramfs, etc.)
├── version           # Version info (CODEOS_VERSION, CODEOS_BUILD)
├── build.sh          # One-shot build → ~/codeos
├── release.sh        # Package release archive
├── run.sh            # QEMU launch script
└── Makefile          # Root build targets
```
## Goals

An OS whose code is approachable for non-experts and gamers alike — one clear
system, not a maze of distros.

## License

- Kernel: see `kernel/LICENSE` / `kernel/COPYING` (GPLv3)
- Zircon: MIT — see `Zircon/LICENSE`

## Credits

See `kernel/CREDITS`.


--tech4everyone
      BYE Coders
