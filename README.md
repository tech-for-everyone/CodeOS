# CodeOS

A from-scratch hobby operating system with its own kernel, desktop GUI, package
layout, and **Zircon** app toolkit. Built for people who want to see how an OS
works — not a bajillion distros, one approachable system.

**Version:** 1.4.0

**Codename:**"Andromeda"
## Features

- **x86_64 kernel** (experimental ARM64 path) with Limine / Multiboot2 boot
- **Desktop GUI** — menubar, dock, launcher, windows, OpenWeb browser panel
- **Userspace** programs via initramfs (`init`, `shell`, Zircon helpers, ...)
- **Package tree** under `pkgs/` (manifests + compressed `.xora` app archives)
- **Zircon** — lightweight mobile version of CodeOS keeping most features
- **Networking** — TCP/IP stack, DNS, HTTP/HTTPS (TLS 1.3)
- **Containers** — Docker-like container engine with namespaces and cgroups
- **Android compat** — Binder IPC, ashmem, property system
- **Security** — AdBlock, firewall, file integrity monitoring, ClamAV

## Quick start

### Dependencies (host)

```sh
# Arch / Manjaro-style
sudo pacman -S base-devel qemu-full xorriso python

# Ubuntu / Debian
sudo apt install build-essential qemu-system-x86 xorriso python3

# Cross compiler (x86_64 freestanding)
# Install x86_64-elf-gcc / binutils somewhere on PATH
# e.g. from AUR: x86_64-elf-gcc  x86_64-elf-binutils
```

You also need **Rust** (`cargo`) for the OpenWeb HTTP backend (`kernel/rust_ow`).

### Build

```sh
# From repo root — builds kernel + userspace + Limine ISO
./build.sh
# Artifacts land in ~/codeos/  (codeos.bin, codeos.iso, limine files)

# Or use Make directly:
make -C kernel all              # kernel + userspace ELFs
make -C kernel codeos-1-kernel.iso
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
# Quick launch script (auto-detects KVM, boots from kernel)
./run.sh

# Or with options:
./run.sh -m 1G -c 8     # 1GB RAM, 8 cores
./run.sh -i              # Boot from ISO
./run.sh -n              # Serial only (no display)
./run.sh -g              # Use virtio-gpu
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

`fetch` follows a Moss-style repository workflow with cached `stone.index`
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

### Release tarball

```sh
./release.sh   # packs ISO+bin into ~/Downloads/CodeOS-YYYY.MM.DD.tar.gz
```

## Repository layout

```
CodeOS/
├── kernel/           # Kernel sources, drivers, arch, bootloader, userspace build
│   ├── kernel/       # Core kernel C/C++ (mm, sched, syscall, desktop glue, ...)
│   ├── drivers/      # PCI NICs, USB, input, audio, devstore, ...
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

## Roadmap

1. More packages (including Flatpak-style containers later)
2. Broader Linux / Zircon app compatibility
3. Wine-like Windows app translator
4. Stronger desktop polish (macOS Big Sur / ThormiumOS-inspired GUI)
5.Make CodeOS for mobile named Zircon
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

---

## Star History

<a href="https://www.star-history.com/?repos=tech-for-everyone%2Fcodeos&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=tech-for-everyone/codeos&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=tech-for-everyone/codeos&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=tech-for-everyone/codeos&type=date&legend=top-left" />
 </picture>
</a>
