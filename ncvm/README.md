# ncvm (in-tree CodeOS port)

**ncvm** — CodeOS's own QEMU fork, now living inside the CodeOS tree. Two
tiny system emulators built from QEMU v10.2.4, stripped to only what CodeOS
needs:

| Binary             | Machine                | Purpose                |
|--------------------|------------------------|------------------------|
| `ncvm/bin/ncvm`       | — (runner)             | `ncvm` CodeOS VM runner |
| `ncvm/bin/ncvm-x86_64`| q35 only               | CodeOS desktop (x86_64) |
| `ncvm/bin/ncvm-aarch64`| virt only              | CodeOS / Zircon ARM64   |

This directory is the delta-style source of truth (build script + device
configs + patches + runner). The QEMU checkout itself stays pristine
upstream except for the ncvm changes. The canonical upstream lives at
`github.com/tech-for-everyone/NCVM`; this copy is customized for the CodeOS
tree (in-tree ISO/kernel auto-detection, arm64 auto-kernel, see below).

ncvm appears in CodeOS in two places:

1. **`ncvm/` (this dir)** — the host-side program: build it with `make ncvm`
   at the CodeOS root, then `bin/ncvm` boots the CodeOS VM straight from the
   tree.
2. **`pkgs/core/ncvm/src/ncvm.c`** — the *in-guest* side: ncvm is seeded into
   the rootfs as `/bin/ncvm`, where it is the VM backend the kernel VM
   manager hands workloads to (the role stock crosvm used to play), and a
   small `ncvm` command for driving VMs from inside CodeOS (see below).

## What is stripped

- Boards: only `q35` (x86_64) and `virt` (aarch64) remain — `pc`, `microvm`,
  `isapc` and dozens of ARM boards are compiled out via
  `configs/devices/*/codeos.mak` (`--without-default-devices`).
- Legacy device set removed (FDC, ne2k/pcnet/rtl8139, ac97/sb16, uhci/ohci,
  cirrus-vga, PIIX IDE, ...).
- Kept devices: e1000 + virtio-net, USB-EHCI/XHCI + HID (tablet/kbd), ICH9
  AHCI, std VGA (+EDID), ISA serial.
- ACPI_CXL pruned from the machine configs and guarded out of the ACPI
  builders (`patches/0001-codeos-strip-acpi-cxl.patch`) so the firmware
  tables build without the CXL machinery.

## Quickstart

Requires: `git`, `gcc`, `make`, `ninja`, `pkg-config`, `python3`, `glib2`
and `pixman` dev headers (Arch: `base-devel ninja python pkgconf glib2
pixman`).

```sh
cd <CodeOS root>
make ncvm                  # builds both targets into ncvm/bin/, installs
                           # firmware data to ncvm/share/qemu
ncvm/bin/ncvm              # boot CodeOS (finds the ISO in the tree)
```

## Running CodeOS

`ncvm/bin/ncvm` is the CodeOS VM runner: it assembles the QEMU invocation
the way CodeOS expects and launches it.

```sh
ncvm/bin/ncvm              # CodeOS desktop (x86_64 / q35), boots the ISO
                           #   (ISO auto-found in this tree's kernel/ or cwd)
ncvm/bin/ncvm -m 6G -c 6   # more memory / cpu cores (defaults 2G / 2)
ncvm/bin/ncvm -n           # headless, serial on stdio
ncvm/bin/ncvm --iso path.iso
ncvm/bin/ncvm --disk disk.img
ncvm/bin/ncvm -a           # CodeOS arm64 (virt, ramfb); the arm64 kernel ELF
                           #   is auto-detected like the ISO on x86_64
ncvm/bin/ncvm -- <qemu args>   # anything after -- goes straight to QEMU
```

The preset: q35 machine, std VGA + EDID, USB EHCI + tablet/kbd, e1000
user-net with `hostfwd tcp::7070-:80` and `tcp::2222-:22`, KVM when
`/dev/kvm` exists (x86_64 only), threaded TCG otherwise. aarch64 runs under
TCG with a `ramfb` display, and `-monitor none` keeps the (qemu) monitor off
stdio for both archs (override: `ncvm -- -monitor stdio`).

The ISO is located automatically (env `NCVM_ISO`, `$PWD`, `../CodeOS/kernel/`,
`~/CodeOS/kernel/`, `~/Projects/CodeOS/kernel/`, plus `ncvm/../kernel` in this
tree). The arm64 kernel ELF is found the same way.

- Reuse an existing QEMU checkout: `NCVM_QEMU_SRC=/path/to/qemu bash ncvm/build-codeos.sh`
  (the local checkout `/home/codeosuser/Projects/ncvm` or `~/Projects/ncvm` works).
- System install: `ncvm/build-codeos.sh install` (binaries + runner →
  `/usr/local/bin`, firmware → `/usr/local/share/qemu`).
- Single target: `ncvm/build-codeos.sh x86_64` (or `aarch64`).

## In-guest: ncvm as the CodeOS VM backend

`pkgs/core/ncvm/src/ncvm.c` builds `/bin/ncvm` into the guest rootfs
(`kernel/userspace/Makefile` `CORE_PROGS`, `kernel/Makefile` `USER_PROGS`).

- **Daemon** (`ncvm` or `ncvm daemon`): polls `/tmp/crosvm-cmds/` for
  `<name>.cmd` files written by the kernel VM manager, translates the
  crosvm-style command line into ncvm/QEMU arguments (customary subset: `-m`,
  `-cpus`, `--kernel`, `--root`, `--rwdisk/--disk`, `--serial`; crosvm-only
  flags dropped; unknown flags pass through) and execs the ncvm VMM. Same
  wire protocol as `crosvm-launcher` (`.cmd` / `.ctl` / `.pid` / `.exit` /
  `.sup`) so the kernel side needs no changes. The VMM binary itself is NOT
  embedded in the rootfs (it is tens of MB) — provide it beside the guest
  (disk.img / 9p), default `/usr/bin/ncvm-x86_64`, override with `NCVM_BIN`.
- **CLI**: `ncvm list|info|start|stop|pause|resume` drives VMs through the
  same files, mirroring the kernel `vm` shell builtin without VM syscalls.

## Layout

```
build-codeos.sh                            build + bootstrap script
ncvm                                       CodeOS VM runner (wrapper, source)
configs/devices/<arch>-softmmu/codeos.mak  per-target device deps
patches/0001-codeos-strip-acpi-cxl.patch   ACPI_CXL removal + guards
src/qemu/                                  QEMU v10.2.4 checkout (gitignored)
build-<arch>/  install-<arch>/  bin/       build outputs (gitignored)
share/qemu                                 firmware data used by bin/ (gitignored)
../pkgs/core/ncvm/                         in-guest ncvm backend (userspace pkg)
```

The `codeos.mak` device configs and the ACPI patch are the source of truth;
the QEMU tree itself stays pristine upstream except for these.