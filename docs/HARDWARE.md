# Hardware Support Matrix

CodeOS has two very different kernel targets. This document describes what
each one actually drives, based on the sources in `kernel/`.

## 1. x86_64 (full kernel)

The default target builds the full kernel from `kernel/kernel/`,
`kernel/drivers/`, `pkgs/core/*/src/`, LVGL and the Qt panels. Hardware support
is centred on the devices exposed by QEMU's `q35`/`pc` machines and common
legacy PC hardware.

| Subsystem | Support | Source |
|---|---|---|
| Serial / console | COM1 (`0x3F8`), port I/O | `kernel/arch/serial.c` |
| Display | VBE framebuffer, EDID, software cursor themes | `kernel/drivers/`, `kernel/arch/fb.cpp`, `kernel/arch/cursor_theme.cpp` |
| PCI | ECAM/config-space enumeration | `kernel/arch/pci.c` |
| Storage | ATA/AHCI, ext2 and the in-memory initramfs | `kernel/drivers/`, `kernel/kernel/` |
| Network | e1000, virtio-net, TCP/IP, DNS, HTTP/HTTPS | `kernel/drivers/virtio_net.c`, `pkgs/core/` |
| Input | PS/2 keyboard (no working mouse), USB HID stubs | `kernel/drivers/ps2.c`, `kernel/drivers/input.c` |
| GPU | virtio-gpu (`run-gpu`), otherwise framebuffer | `pkgs/core/graphics/src/virtio_gpu.c` |
| Timers | PIT/APIC timer | `kernel/drivers/timer.c`, `kernel/arch/apic.c` |
| RTC | CMOS RTC | `kernel/arch/rtc.c` |

Real hardware outside that list (modern Wi-Fi, discrete GPUs, NVMe, USB3
controllers) is **not** generally supported. Limine boots the kernel via
Multiboot2.

### Device profiles (phones/tablets)

`kernel/kernel/device.{c,h}` is the device table used by the Zircon mobile
layer. A profile is a **panel** (physical resolution), a **logical screen**
(the layout the UI targets) and a **scale**, e.g. a 1080x2400 panel with a
360x800 logical screen at 3x.

Selection order: an explicit `zircon.device=<name>` kernel command line, then a
panel matching a profile, then a generic geometry profile. `make device-check`
tests the Galaxy S9 profile and the clamping rules. `SYSCALL_GET_INFO`
command 4 exposes the chosen profile to userspace.

## 2. ARM64 (minimal QEMU `virt` kernel)

Built with `make -C kernel ARCH=arm64 all`. There are **two ARM64 images**:

- **CodeOS ARM64** (default): `codeos_main.c` + `codeos_ui.c` bring up the same
  HAL and present a small CodeOS shell/desktop. This is the path intended to
  grow into an ARM laptop port. Output is `codeos-1-kernel-arm64.bin`.
- **Zircon mobile demo**: `ARM64_UI=zircon` selects `kernel_main.c` + `ui.c`,
  the standalone Zircon phone UI. Output is
  `codeos-1-kernel-arm64-zircon.bin`.

Both share `kernel/arch/arm64/{serial,fb,fw_cfg,display,pci,rtc,handlers,input,string}.c`,
the `arch/arm64/*.S` assembly, and two shared files (`kprintf.c`, `jengine.c`).

| Hardware | Status | Details |
|---|---|---|
| PL011 UART | Working | `0x09000000`; polling TX/RX, used as the kprintf sink and as a keyboard fallback |
| ARM generic timer | Working | Reads `CNTFRQ_EL0`/`CNTPCT_EL0` (`rtc.c`) |
| PL031 wall clock | Working | `0x09010000`; used by the UI status bar |
| QEMU fw_cfg | Working | `0x09020000`; signature check, file-directory lookup, DMA writes |
| ramfb framebuffer | Working | 1024x600 at `0x41000000`, published through `etc/ramfb`; CodeOS shell / Zircon UI draw here |
| PCIe ECAM | Working | `0x4010000000`; brute-force bus/device/function scan |
| virtio-mmio input | Polling driver | Modern (version 2) layout only. QEMU must be started with `-global virtio-mmio.force-legacy=off`; window at `0x0A000000`, 32 slots, virtio-tablet (absolute pointer) and virtio-keyboard |
| PL050 KMI | Address mapped | Device window mapped, but no driver |
| GIC / interrupts | **Stub** | IRQ vector exists; `irq_handler()` and `irq_init()` are placeholders. All devices are polled. |
| Device tree / ACPI | **Absent** | The boot address in `x0` is discarded; all addresses are hardcoded for `virt` |
| Userspace | **Absent** | No ARM64 `crt0`, syscall wrappers or linker script; `USER_ELF` is empty |
| Filesystem / network | **Absent** | No ext2, initramfs, virtio-net or TCP/IP in the ARM64 image |
| Syscalls | Stub | `syscall_handler()` returns `-ENOSYS` for every listed call |

### Recent ARM64 fixes

These were real defects that prevented the ARM64 kernel from building or from
seeing its input devices:

- The default ARM64 image used to be the **Zircon mobile UI**, which is why an
  `arm64` boot announced "Zircon 1.0" instead of CodeOS. `codeos_main.c` and
  `codeos_ui.c` now provide a genuine CodeOS ARM64 entry point and shell; the
  Zircon UI is still available with `ARM64_UI=zircon` and has its own artifact
  name so the two cannot overwrite each other.
- Added `arch/arm64/input.c` and `arch/arm64/ui.c` to the source list (the
  kernel referenced `input_init`, `ui_init` and `ui_run` without linking them).
- Completed the virtqueue structures in `input.c` (`vq_desc_t`, `vq_avail_t`,
  `vq_used_t`, `VQ_N`) so the driver compiles and can hand ring addresses to the
  device.
- Added the missing `draw_text_c()`, `itoa10()` and `circle_pts()` helpers in
  `ui.c`, and the public `display_*` wrappers in `display.c`.
- Excluded x86_64-only OpenSSL archives from the ARM64 link.
- Skipped the x86_64-only Qt panels sub-make on ARM64.
- **Mapped the virtio-mmio window as device memory.** `boot.S` maps
  `0x09000000`-`0x0AFFFFFF` as `nGnRnE`; previously `0x0A000000` fell through
  to a normal mapping to RAM, so the input probe could never see a device.
- Added `make -C kernel ARCH=arm64 arm64-run-input`, which attaches
  `virtio-tablet-device` and `virtio-keyboard-device` in QEMU with
  `-global virtio-mmio.force-legacy=off`. QEMU's default legacy devices report
  version 1 in the MMIO header; the driver only handles the modern version-2
  register layout, so the flag is what makes the two devices actually appear.

## 3. What is missing, in priority order

These are the concrete next steps for broader hardware support. Each item is
based on an existing stub or an absent mechanism in the tree.

1. **GICv2/v3 initialisation and EOI.** `kernel/arch/arm64/handlers.c` has an
   empty `irq_handler()` and a printing `irq_init()` stub. Needed before any
   interrupt-driven driver (timer, virtio, USB) can work. *Medium-to-large.*
2. **Minimal device-tree parsing.** `boot.S` stashes the DTB pointer in `x20`
   but passes `NULL`/`0` to `kernel_main`. A small FDT walker would let the
   kernel discover the UART, RTC, virtio-mmio and framebuffer instead of
   hardcoding QEMU addresses, and is the prerequisite for real ARM boards.
   *Medium.*
3. **Pass and use boot arguments.** Related to (2): expose the DTB pointer to
   `kernel_main` rather than discarding it. *Small.*
4. **Virtio-net / virtio-blk.** `kernel/drivers/virtio_net.c` exists but
   includes x86_64 port-I/O and PCI headers; the shared PCI scan in ARM64 could
   feed a portably rewritten version. *Medium.*
5. **Interrupt-driven input.** The virtio-input driver is polled. Once (1)
   exists, switch to descriptor-used-ring interrupts. *Medium.*
6. **Userspace on ARM64.** Blocked by x86 assembly in `crt0.S`, `int $0x80`
   syscall wrappers in `unistd.h`/`socket.h`, and `OUTPUT_FORMAT(elf64-x86-64)`
   in the linker scripts. *Large.*
7. **Real-device display.** The current `display.c` assumes QEMU ramfb via
   fw_cfg. A simple framebuffer from the device tree (or a DRM driver) is
   needed for non-QEMU hardware. *Large.*

## 4. Your device specifically

The reported host is **Arch Linux ARM aarch64, Linux 6.12.0-andronix, Snapdragon
SM8250, 2880x1800 @ 144 Hz, Mesa llvmpipe**. In the current tree:

- **As a build host:** supported. `make -C kernel ARCH=arm64 all` and all
  host-side `make check` targets run natively on aarch64 Linux. The x86_64
  target also cross-compiles once the `x86_64-unknown-none` Rust target is
  installed (see [BUILDING.md](BUILDING.md)).
- **As a boot target:** not supported natively. The ARM64 kernel only runs
  under `qemu-system-aarch64 -machine virt`. It has no GIC initialisation, no
  device-tree parsing, no display driver for a real panel, and no userland.

The 2880x1800 panel and the SM8250's peripherals are not described anywhere in
the kernel; adding them starts with items (1)-(3) above, not with a framebuffer
resolution change — the display code currently hardcodes 1024x600 for ramfb.
