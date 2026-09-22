# CodeOS AGENTS.md

Guidance for humans and coding agents working on this tree.

## Goals
- Make CodeOS feel like a daily-driver hobby OS
- Android / Linux app support (containers + Zircon)
- Keep the desktop approachable and polished

## Layout notes
- **Canonical panel apps** live in `pkgs/core/panels/src/` — the kernel Makefile
  compiles those, not the older copies under `kernel/kernel/`.
- Userspace programs are built from `pkgs/core/<name>/src/` via
  `kernel/userspace/Makefile`, then embedded with `scripts/gen_initramfs.py`.
- Kernel-injected packages use a `pkgs/core/<name>/src/KERN` marker. The
  graphics stack lives in `pkgs/core/graphics/` and is compiled into the
  kernel automatically.
- OpenWeb's HTTP backend and HTML renderer are Rust: `kernel/kernel/rust_ow/` (needs `cargo`); the renderer writes the C-owned grid globals in `qt6/panels/ow_html.{c,h}` (see the OpenWeb section below).
- **Zircon OS** (`Zircon/`) is a standalone mobile/desktop OS built on the CodeOS kernel.
  It has its own init process (`zircon_init.c`), compositor (`zircond`), and bootable ISO.
  Build: `make -C Zircon all && make -C Zircon iso`.
  Zircon uses `kernel/kernel/windows.h` for `window_t`, `kernel/kernel/gui/` for GUI types,
  and `kernel/kernel/sys/` for kernel IPC syscalls.

## Build
```sh
make -C kernel all
make -C kernel codeos-1-kernel.iso
make -C kernel run-iso
```
Host needs `x86_64-elf-gcc`, `xorriso`, `python3`, `cargo`, and QEMU for run targets.

## Todo
1. Moss-style CCP fetch/sync — stone.index metadata, `fetch fetch`, `sync -u` (in progress)
2. Improve kernel quality (memory, sched, syscalls, drivers)
3. Docker-like containers for apps
4. Improve OpenWeb
5. GUI polish (Big Sur / ThormiumOS direction)
6. Consistent SVG / icon pipeline
7. Drop stale duplicates under `kernel/kernel/` once panels are sole source of truth

## VM / app-compat stack (done)
- ncvm: the in-guest VM backend at `pkgs/core/ncvm/src/ncvm.c` (kernel-syscall only, same wire protocol as crosvm-launcher). `/bin/ncvm` daemon polls `/tmp/crosvm-cmds/`, translates crosvm-style command lines to ncvm/QEMU args and execs the ncvm VMM (default `/usr/bin/ncvm-x86_64`, env `NCVM_BIN`; not rootfs-embedded — provide beside the guest on disk.img/9p). `ncvm list|info|start|stop|pause|resume` drives VMs via the same files; `ncvm --selftest` is a boot-time smoke test. Wired into userspace `CORE_PROGS`/`PROGRAMS` + kernel `USER_PROGS`. A boot-time self-test (`kernel/kernel/ncvm_probe.c`, launched from `main.c` like `compat_probe`) loads and runs `/bin/ncvm --selftest` and prints `NCVM: done status=0`.
- ncvm (host side): `ncvm/` at the CodeOS root is CodeOS's own QEMU 10.2.4 fork — builds `ncvm/bin/ncvm-x86_64`, `ncvm/bin/ncvm-aarch64`, `ncvm/bin/ncvm` runner (`make ncvm`; runtime preset: q35, std VGA + EDID, EHCI + tablet/kbd, e1000 hostfwd 7070→80/2222→22, KVM or TCG, ramfb display + auto arm64 kernel for `-a`).
- crosvm: launcher at `pkgs/core/crosvm-launcher/src/` (kernel-syscall only: SHM, FORK/EXECVE/WAIT, READDIR over `/tmp/crosvm-cmds/{name}.cmd|.ctl|.pid`). Wired into userspace `CORE_PROGS` + kernel `USER_PROGS`.
- VM control: `SYSCALL_VM` (52) + `VM_CMD_*` in `kernel/kernel/syscall.c`; `vm_manager_init()` called from `main.c`. Shell commands: `vm list|info|start|stop|pause|resume|run`.
- Linux compat: `linux_syscall_handler` routed via personality; added KILL(62), TGKILL(234), GETPPID(64), GETEUID/GETEGID(107/108), SETUID/SETGID(105/106), SIGALTSTACK(131), CLONE(56), READLINKAT(267), NEWFSTATAT(262). `linux-runner` sets PERSONALITY_LINUX then execve.
- Process signals: `proc_kill()` (SIGTERM/SIGKILL→zombie+wake parent, SIGSTOP/SIGCONT), `SYSCALL_KILL` (51).
- Android: `android-apps` program (list/containers/launch) in `pkgs/core/android-apps/src/`; container exec syscall fixed (argv now via a4=r10); `android-container` exposes props/binder/ashmem.

## Waydroid (Android on the desktop, kernel shell builtin)
- `waydroid` is a kernel shell builtin (`cmd_waydroid` in `kernel/kernel/waydroid.c`). Flows: `waydroid init` (idempotent — the `android-stock` image ships complete at boot, including `build.prop` and all 10 apps under `<image>/system/app/<name>/<name>`); `session start|stop|pause|resume` drives the appvm container; `app list|launch <app>`; `shell` runs `/bin/sh` in the guest; `status` reports image/root/session/gui.
- Apps are userspace ELFs in `pkgs/core/<name>/src/`, built into `/bin/android-*` via `kernel/userspace/Makefile` (`ANDROID_PROGS` + `LIB_ANDROID` = `lib/android_ui.o`) and embedded by `gen_initramfs.py`; the `android-stock` image is populated at boot from those ELFs by `rootfs_seed_android_stock()` (`kernel/kernel/rootfs.c`), and apps are launched inside the container via `container_exec(id, "/system/app/<name>/<name>")`. The app-name list is the single source of truth `rootfs_android_apps` in `rootfs.{c,h}` (shared by rootfs seed + waydroid UI).
- `appvm`/`container` shell commands work directly on the seeded image: `appvm images` enumerates `/containers/images/`, `appvm pull <debian-minimal|android-stock>` materializes an image, and `appvm run <image> <cmd...>` is docker-style — it activates the fresh container (`container_mark_running`) and execs the command directly (no entrypoint boot), then removes the temporary container on exit; `appvm run <image>` without a cmd boots the entrypoint and keeps the container running.
- **User-window bridge** (`kernel/kernel/user_wm.{c,h}`): maps WM-protocol messages on fd 3 (commands) / fd 4 (events) to LVGL desktop windows, with fds/events wired in `syscall.c` (READ/PWRITE hooks, `UW_FD_EVT=4`/`UW_FD_CMD=3`) and input/tick routed from the compositor thread in `lvgl_port.c`. `container_exec` calls `user_wm_setup/release` for `/system/app/*` paths; `main.c` arms the bridge via `user_wm_init()` when a framebuffer is present.
- When the bridge/desktop is absent (headless boot), apps self-report and fall back to console mode — this keeps `make -C kernel codeos-1-kernel.iso` + headless QEMU (`-vga none -nographic`) verification deterministic: `waydroid app launch android-calculator` boots the app and its console REPL evaluates expressions.
- Note: kernel `snprintf` has no `-` flag (right-align with the widest width instead) and `fs_resolve()` returns the node index — test with `>= 0`, never `== 0`.

## OpenWeb (litebrowser-style lightweight browser)
- Two Rust halves, one C contract: the **HTTP/tab backend** (`kernel/kernel/rust_ow/src/lib.rs`, built into `libow_http.a`) owns the tab array and fetching; the **HTML renderer** (`kernel/kernel/rust_ow/src/ow_render.rs`) is a direct Rust port of the old C `render_html()` and is exported as `ow_render_rs(const char *html, int len)`.
- The renderer writes the **C-owned output globals** declared in `qt6/panels/ow_html.h` (`ow_txt[512][120]`, `ow_txt_lines`, `ow_line_info`, `ow_links`, `ow_images`, `ow_forms`, `ow_form_fields`, `ow_page_title`, `ow_need_render`, …) through `extern static`, so the Qt frontend (`qt_panels_openweb.cpp`) and `openweb_core.c` are unchanged. `qt6/panels/ow_html.c` keeps only those globals, the parallel image workers + `ow_image_download()`, and the exported form API (`ow_field_set_value/toggle/at`, `ow_form_build_query`).
- The persistent **form edit cache** stays in C (identity keys name+type+form-action) because it is re-applied on every re-render; Rust calls the thin `ow_fv_restore(ow_form_field_t*, int)` / `ow_fv_store(const ow_form_field_t*, int)` bridges instead of duplicating the logic.
- `openweb_tab_t` in `pkgs/core/panels/src/ow_http.h` must mirror the trailing `_redirect_depth` field of the Rust `OpenwebTab` struct — without it the C and Rust `sizeof` differ and tab indexing past 0 is broken.
- Headless verification: `ow render <url>` (shell builtin in `shell.c`, weak-linked to `ow_core_navigate()` + `ow_core_dump_active()` in `openweb_core.c`) fetches the page with the Rust backend, renders with `ow_render_rs`, and dumps the text grid + links + fields to the console. Host a page with `python3 -m http.server 8000 --bind 0.0.0.0`, then boot the ISO with `-vga none -nographic -monitor none -netdev user,id=net0 -device e1000,netdev=net0` and run `ow render http://10.0.2.2:8000/test.html`.
- The full CSS/`litehtml` layout engine is **not** wired up yet (the upstream `litebrowser-linux` `litehtml` submodule is empty); rendering is the text-grid engine above. `libow_http.a` has no Make prerequisites if the rule is left bare — `kernel/Makefile`'s `RUST_LIB` rule depends on `kernel/kernel/rust_ow/src/*.rs` + `Cargo.toml`.
