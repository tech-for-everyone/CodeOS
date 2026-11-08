# CodeOS
A from-scratch hobby operating system with its own kernel, desktop GUI, package
layout, and **ZircApp** app toolkit. Built for people who want to see how an OS
works — not a bajillion distros, one approachable system.

**Version:** 1.5.0.1-t

**Internal Codename:** Andromeda
## Features

- **x86_64 kernel** (experimental ARM64 path) with Limine / Multiboot2 boot
- **Desktop GUI** — menubar, dock, launcher, windows, OpenWeb browser panel
- **Userspace** programs via initramfs (`init`, `shell`, Zircon helpers, ...)
- **Package tree** under `pkgs/` (manifests + compressed `.xora` app archives)
- **Zircon** — lightweight native C GUI toolkit (`Zircon/`)
- **Networking** — TCP/IP stack, DNS, HTTP/HTTPS (TLS 1.3)
- **Containers** — Docker-like container engine with namespaces and cgroups
- **Android compat** — Binder IPC, ashmem, property system
- **Security** — AdBlock, firewall, file integrity monitoring, ClamAV
- **Automated Driver Detection Subsystem** — runtime hardware identification probes, dynamic loading into volatile RAM, strictly withholding un-detected drivers
- **Peripherals** — Logitech G305 Lightspeed: receiver identification, HID mouse input, DPI presets/software scaling, DPI-cycle button, and best-effort HID++ battery/polling queries. AULA F87: identification, USB HID input, software Win-lock, RGB mode/brightness API with best-effort vendor control, and NKRO activity heuristics. Full HID++ feature-map negotiation and vendor-specific RGB/NKRO report-descriptor parsing remain hardware/firmware-dependent.
- **CLI & Python Tools** — `codeos-cli`, Python `cmatrix`, and analogous Swift `cmatrix`

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

# Optional cross binutils (x86_64-elf-gcc / binutils) speed up nothing; the
# build falls back to host GNU ld when x86_64-elf-ld is not installed.  Install
# them from the AUR (x86_64-elf-gcc x86_64-elf-binutils) only if you want the
# exact cross linkers, or to rebuild Qt6 from source.
```

You also need **Rust** (`cargo`) for the OpenWeb HTTP backend (`kernel/rust_ow`).

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
