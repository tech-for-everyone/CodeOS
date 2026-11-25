# CodeOS usability roadmap

This is the working plan for turning CodeOS from a buildable hobby kernel into
something that can be used on real machines. It covers the requested software
and hardware targets, but it is deliberately staged: each phase has a check
that can fail, because a green compile alone has repeatedly hidden broken
runtime behaviour in this tree.

## Targets

### Software

- **OpenWeb**: make the existing browser usable for everyday web navigation.
  It already has a Rust HTTP/tab backend, a text-grid HTML renderer, a Qt
  desktop panel, a tty app and shell commands. The missing work is the part
  that makes it feel like a browser: URL entry that does not fight single-key
  hotkeys, reliable history/back/forward, form/image handling, live frontend
  wiring and boot-level assertions against a served page.
- **Antartica**: integrate `third_party/kernel/antartica` with the main build
  instead of leaving it as an isolated checkout. The exact role of the project
  must be established from its own build files before it is wired in; the
  integration has to work from any checkout path and must not add generated
  binaries to git.

### Hardware

| Machine | OS target | Notes |
|---|---|---|
| AMD Ryzen 5 5600X / Ryzen 5 5600 desktop | CodeOS | Generic x86-64 base already exists; validate PCIe, USB and memory on AM4. |
| Nvidia RTX 2080 | CodeOS | GPU bring-up means a real display path, not just VBE/ramfb. Nvidia's proprietary stack is not present in this tree. |
| 32 GB RAM | CodeOS | Validate high-memory page-table/PMM assumptions, not just low-memory boots. |
| Logitech G305 Lightspeed | CodeOS | USB receiver HID mouse; current x86 USB HID input is incomplete. |
| Aula F87 keyboard | CodeOS | USB HID keyboard; current PS/2 path is keyboard-only and USB deltas are hardcoded. |
| Xiaomi Pad 6 (Snapdragon 8 Gen 1) | CodeOS and Zircon | ARM64 FDT, GIC, panel/touch, USB, clock/power and storage work. |
| Samsung SM-G960U (Snapdragon 845 / SDM845) | Zircon | ARM64 device tree, GIC, panel/touch and Zircon's compositor/input stack. |

## Phase 1 — Make OpenWeb usable

1. Confirm which OpenWeb components are actually linked into the x86_64 image;
   `--gc-sections` has hidden whole modules in this tree before.
2. Fix URL entry so `h`, `t`, `p` and other hotkeys do not fire while typing an
   address.
3. Make back/forward/stop/reload reachable from every frontend through one
   shared backend state.
4. Verify forms and images against a local HTTP server from the booted OS, not
   only in host tests.
5. Add a boot check that serves a known page, drives the UI and asserts on the
   rendered result.

**Done when:** a booted CodeOS session can navigate to a local page, follow a
link, go back, submit a form and show the result, with a repeatable check.

Progress: the tty app's hotkeys no longer fight URL entry once the address field
has text, numbered links are followed with `1-9` + Enter using relative-URL
joining, and the PWA in `OpenWeb/static/` now has its missing `app.js` so the
toolbar and modals are live. These are host-verified by `make openweb-check`;
items 4 and 5 (booted-OS form/image verification and a boot assertion) remain
open.

## Phase 2 — Integrate Antartica

1. Record what Antartica is, how it builds and what it exports.
2. Decide whether it belongs in the kernel image, userspace, or as a build-time
   tool; wire only the interface that is actually consumed.
3. Make all paths relative to the checkout or an explicit environment variable.
4. Add a host-level test and, if it is runtime code, a boot-level check.
5. Update `docs/BUILDING.md` and `docs/INSTALL.md` with the new dependency and
   target.

**Done when:** a clean checkout builds Antartica as part of the normal flow and
a check proves its output is consumed, not merely compiled.

## Phase 3 — AMD desktop hardware

1. Boot on the Ryzen 5600X/5600 class machine with 32 GB installed and verify
   the full memory map from the serial log.
2. Enumerate PCIe devices and identify the RTX 2080; decide whether the path is
   a real GPU driver or a framebuffer-compatible display mode.
3. Repair USB HID mouse/keyboard handling for the G305 receiver and Aula F87;
   stop hardcoding pointer position in the input layer.
4. Add a hardware-profile document that separates “detected”, “initialised”
   and “usable”, so a device showing up in a scan is not reported as working.

**Done when:** the desktop reaches a usable framebuffer, pointer and keyboard
on the target machine, with a reproducible capability report.

## Phase 4 — ARM devices

1. Replace hardcoded QEMU `virt` addresses with device-tree discovery for
   Xiaomi Pad 6 and SM-G960U: GIC, generic timer, UART, panel, touch, storage
   and USB.
2. Bring up the Zircon compositor/input path on both ARM devices and the
   CodeOS ARM shell on Xiaomi Pad 6.
3. Keep the QEMU `virt` target working as the fast regression environment;
   device profiles must be additive, not a replacement that breaks emulation.
4. Add per-device boot checks that assert on the device identity and the
   drivers that actually initialised.

**Done when:** each named device boots its named OS with working display and
input, and a check records which drivers came up.

## Current baseline

```sh
make check
make -C kernel ace-check
make -C kernel ARCH=arm64 all
make -C kernel ARCH=arm64 ARM64_UI=zircon all
./run.sh -a arm64 -n
./run.sh -a x86_64 -n
```

Do not claim a phase complete from a build alone. Every item above needs the
same evidence standard used elsewhere in this tree: a host check where
possible, a boot/runtime assertion where the code is live, and a negative
control where the check could otherwise pass for the wrong reason.
