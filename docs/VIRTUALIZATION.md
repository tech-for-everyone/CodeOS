# Running CodeOS in a Virtual Machine

This guide covers QEMU (the primary test target), VirtualBox and libvirt. Build
the images first using [BUILDING.md](BUILDING.md).

## 1. QEMU — x86_64

### Quick start

```sh
./run.sh                 # prompts: 1) x86_64 or 2) arm64, then builds + boots
./run.sh -a x86_64       # skip the prompt and boot the full ISO
./run.sh -a arm64 -n     # CodeOS ARM64 kernel, headless
./run.sh -a arm64 -z     # Zircon mobile demo instead of CodeOS
./run.sh -s              # rebuild is skipped; boot the existing image
./run.sh -m 1G -c 8      # 1 GiB RAM, 8 cores
./run.sh -n              # headless, serial on stdio
```

With no `-a` and no `CODEOS_ARCH`, `run.sh` asks whether to build `x86_64` or
`arm64` and then compiles that architecture before launching QEMU. For scripts
and non-interactive shells, set `CODEOS_ARCH` or pass `-a`; the fallback is
`x86_64`. The x86_64 path builds `kernel` + the Limine ISO and runs
`make ncvm-check` before booting so a broken VM backend cannot produce a
misleading green boot. The arm64 path builds `make -C kernel ARCH=arm64 all`
and boots `codeos-1-kernel-arm64.bin` with ramfb and virtio input. `-z` sets
`ARM64_UI=zircon` and boots `codeos-1-kernel-arm64-zircon.bin` instead.

`run.sh` derives all paths from its own location, detects `/dev/kvm` for the
x86_64 target (arm64 always uses TCG), and attaches `disk.img` when present.

### Make targets

From the repository root or with `make -C kernel <target>`:

| Target | What it boots |
|---|---|
| `run` | Kernel directly via `-kernel`, with the legacy data disk |
| `run-iso` | Live ISO + `disk.img` attached as IDE |
| `run-iso-nographic` | Live ISO, serial only |
| `run-iso-uefi` | Live ISO with OVMF firmware |
| `run-full` | Kernel with the full device set (AHCI, USB, network) |
| `run-gpu` | Kernel with `virtio-gpu-pci` |
| `run-uefi` | Kernel with OVMF |
| `run-sb` | Secure-boot chain (requires `sb-vars` setup) |
| `run-installed` | The persistent image from `install-image` |

### The disk image must be attached explicitly

The ISO **contains** a copy of `disk.img`, but the kernel has no ISO9660 or
loop-device support, so it cannot read that copy. Every `run-iso*` target adds:

```
-drive file=kernel/disk.img,format=raw,if=ide
```

If you launch QEMU by hand, include that argument or the boot log will show
`storage: no block device found`.

### Manual boot command (serial-only, deterministic)

This is the command used by the boot checks:

```sh
qemu-system-x86_64 -machine pc -m 1G -smp 2 -vga none -nographic -monitor none \
    -boot order=d -cdrom kernel/codeos-1.0.iso \
    -drive file=kernel/disk.img,format=raw,if=ide -no-reboot 2>&1 \
  | sed -e 's/\x1b\[[0-9;]*[A-Za-z]//g' > /tmp/boot.log
```

Use `-machine pc -vga none` for serial capture: with `-vga std` the kernel
initialises VBE and the serial log goes quiet before the storage phase.

Expected markers:

- `block: ATA disk detected (129 MB, ...)`
- The output of `/scripts/test1.script` and `/scripts/complex.script`
- `codefs: bad magic ... / codefs: mount failed` is **expected** — it is the
  fallback to ext2, not a second fault. `script: cannot open ...` is the real
  failure.

### Networking

The default QEMU network is user-mode with host forwards:

```
-netdev user,id=net0,hostfwd=tcp::7070-:80,hostfwd=tcp::2222-:22 -device e1000,netdev=net0
```

Guest port 80 is reachable from the host at `localhost:7070`, and guest SSH at
`localhost:2222`.

## 2. QEMU — ARM64 (`virt` machine)

The ARM64 kernel is a minimal, QEMU-`virt`-specific image. It does not build an
ISO; boot the ELF directly with `-kernel`. By default the image is the
**CodeOS ARM64 shell**; add `ARM64_UI=zircon` when building to get the
standalone Zircon mobile UI instead (a separate binary name keeps the two from
overwriting each other).

```sh
make -C kernel ARCH=arm64 all
```

| Target | Command it runs |
|---|---|
| `arm64-run` | `qemu-system-aarch64 -machine virt -cpu max -m 512M -serial stdio -kernel ...` |
| `arm64-run-nographic` | Same, `-nographic` |
| `arm64-run-display` | Adds `-device ramfb` for a graphical framebuffer |
| `arm64-run-input` | Adds `ramfb`, two virtio devices, and `-global virtio-mmio.force-legacy=off` |

The input driver polls two sources: the PL011 serial console as a keyboard,
and the virtio-mmio window at `0x0A000000` for tablet/keyboard devices. Use
`arm64-run-input` to exercise the full path; the other targets only have the
serial keyboard fallback. The `-global virtio-mmio.force-legacy=off` flag is
required: QEMU defaults to legacy virtio-mmio (version 1), while the driver
implements the modern version-2 register layout. Without it the devices are
present in the machine but the probe reports `0 virtio devices`.

Manual equivalent:

```sh
qemu-system-aarch64 -machine virt -cpu max -m 512M -serial stdio \
    -global virtio-mmio.force-legacy=off \
    -device ramfb \
    -device virtio-tablet-device \
    -device virtio-keyboard-device \
    -kernel kernel/codeos-1-kernel-arm64.bin
```

### Capturing the ARM64 framebuffer headlessly

`arm64-run-display-nogui` boots with `-display none -device ramfb`, writes the
serial log to `/tmp/codeos-arm64-serial.log`, and opens a QMP socket at
`/tmp/codeos-arm64-qmp.sock`. You can then request a screenshot:

```sh
printf '{"execute":"screendump","arguments":{"filename":"/tmp/codeos-arm64.ppm"}}\n' \
  | socat - UNIX-CONNECT:/tmp/codeos-arm64-qmp.sock
```

### ARM64 assumptions

The kernel hardcodes the QEMU `virt` memory map: PL011 at `0x09000000`, PL031
at `0x09010000`, fw_cfg at `0x09020000`, virtio-mmio at `0x0A000000`, ramfb
framebuffer at `0x41000000` (1024x600), and PCIe ECAM at `0x4010000000`. It
does **not** parse a device tree, so these addresses must match the emulated
platform. See [HARDWARE.md](HARDWARE.md).

## 3. VirtualBox

```sh
make -C kernel run-vbox
```

This provisions a `CodeOS` VM via `vbox.sh --headless`, with NAT networking and
the data disk attached as IDE. You can also create it manually with the Live
ISO as a CD device. CodeOS's Limine bootloader uses BIOS/MBR, so create the VM
with BIOS firmware (not EFI) unless you are using the `run-iso-uefi` OVMF path.

## 4. libvirt / virt-manager

```sh
make -C kernel virt-install     # creates the 'codeos-1' domain from the ISO
make -C kernel virt-manager     # opens the GUI
```

The domain boots from the ISO with a VNC display on `127.0.0.1`. Attach
`kernel/disk.img` as an IDE disk if you want the writable volume.

## 5. Headless verification tips

- Strip ANSI escapes from serial logs with
  `sed -e 's/\x1b\[[0-9;]*[A-Za-z]//g'`.
- `-no-reboot` makes a triple fault exit instead of silently restarting; a QEMU
  exit code of 124 from `timeout` means "still running", while an immediate 0
  with no log can mean a reset.
- The desktop has no working PS/2 mouse, so drive it with the keyboard:
  `meta_l+ret` launches an app, `meta_l+t` toggles tiling, `meta_l+space` opens
  the launcher.
- For a shell-only boot with a framebuffer, use limine entry 5
  (`CodeOS (Shell + Framebuffer)`), selected by the `no-desktop` cmdline flag.

## 6. Troubleshooting

**`storage: no block device found`**
You did not attach `kernel/disk.img`. Add the `-drive ...` argument shown above.

**ARM64 boot stops after a few serial lines**
Check that you used `-machine virt`. The hardcoded device addresses are only
valid for that machine type.

**No virtio tablet/keyboard events**
Use `arm64-run-input`, and make sure the build includes `arch/arm64/input.o`
(it was added to the ARM64 source list). The page tables map the virtio-mmio
window as device memory; an older binary without that fix will read RAM there.

**QEMU says `/dev/kvm` permission denied**
Add your user to the `kvm` group, or run without acceleration. `run.sh`
automatically falls back to TCG when `/dev/kvm` does not exist.

**No display under Termux**
Install `termux-x11-nightly` and export `DISPLAY=:0`, or use the
`*-nographic` targets and watch the serial log.
