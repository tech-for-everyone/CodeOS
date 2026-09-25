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
- **WebView-style navigation API** lives in the Rust core, so every frontend shares one history: per-tab back/forward stacks (`ow_go_back()`, `ow_go_forward()`, `can_go_back`/`can_go_forward` fields, filled from the stacks), `ow_stop_loading()` (best-effort — fetches are synchronous, so the flag is polled between redirect/retry iterations), and per-tab URL recording on `ow_navigate`/`ow_search`/`ow_navigate_post`/`ow_tab_new`. History is stored in `HIST_URL`/`HIST_LEN`/`HIST_POS` statics (32 entries × 256 bytes per tab) and shifted together with `TABS` in `ow_tab_close()`. This mirrors the webkit6 `WebView` API (`go_back`/`go_forward`/`reload`/`stop_loading`/`can_go_back`/`can_go_forward`/`estimated-load-progress`) so a future GTK4 or Zircon frontend can port the demo `WebView` browser directly; `openweb_core.c` exposes the same through `ow_core_back/forward/stop/can_go_*`, and the userspace tty app gets them as `sys_web_go_back/forward/stop` (`WEB_GO_BACK=11`, `WEB_GO_FORWARD=12`, `WEB_STOP_LOADING=13`).
- Note: the desktop Qt panel keeps its own per-tab C++ history (`m_hist`/`m_histPos`) for back/forward button state — it is a display layer on top of the same backend, not a second source of truth for the other frontends.
- **tty app single-key hotkeys need `TCSETS`, and `TCGETS` was broken for every app.** The serial input path is canonical by construction: `sys_read()`/`kernel_read()` on fd 0 spin on `serial_readchar()` until `\n`/`\r`, so the tty `openweb` app's `b`/`f`/`s` hotkeys could not fire until an Enter followed each one. `ioctl` now implements `TCSETS`/`TCSETSW`/`TCSETSF` (0x5402-4) alongside the existing `TCGETS`; clearing `ICANON` in `c_lflag` (offset 12 of the 44-byte x86-64 `struct termios`) makes both read paths return keystrokes as they arrive. `TCGETS` reports `ICANON` according to the mode actually in force so the usual `tcgetattr`→tweak→`tcsetattr` round trip works. Only `c_lflag`/`ICANON` is interpreted — the serial input path does no echoing and has no signal or flow-control handling, so `ECHO`/`ISIG` are accepted and ignored.
- **The tty mode is `process_t::tty_raw`, deliberately not a global.** fd 0 is shared by every process, so a global would let an app that exits in raw mode strand the shell in raw mode with no line buffering and no way to type a command. Per-process means the flag dies with the process; the app still restores on exit as hygiene. `cmd_exec` uses `proc_create()`, so the app gets its own `process_t` rather than sharing the shell's.
- **The console fds must bypass `FD_CHECK` in the `LINUX_IOCTL` case.** `fd_table` is only populated by `fd_alloc()` on the Linux-compat `open` path; `proc_create()` registers nothing, and `sys_read`/`sys_write` special-case fd 0-2 without consulting `fd_table`. Gating the ioctl on `FD_CHECK` therefore rejected the console for exactly the programs that want to set the terminal mode — `tcgetattr` returned `-EINVAL` and the app silently stayed in canonical mode. Accept fd 0/1/2 directly, as the read/write paths do.
- `ioctl` is a Linux-compat syscall, so it is only reachable under `PERSONALITY_LINUX`; setting that personality permanently would divert the app's CodeOS-native syscalls (including `SYSCALL_WEB`) into the Linux translation table and break them. `sys_ioctl_linux()` in `kernel/userspace/include/unistd.h` sets the personality for the one call and restores it, and the app calls only `tcgetattr`/`tcsetattr` from there. There is no syscall to read the personality back, so the restore assumes native `0` — correct for the openweb app, but a process that lives under `PERSONALITY_LINUX` (`linux-runner`, `crosvm-launcher`) must not use these helpers.
- Hotkeys still shadow URL text entry: `handle_key()` tests the hotkeys before the printable-character fallthrough, so typing a URL like `http://…` triggers `h`/`t`/`p` first. The help text's "Type to enter URL" is therefore only true for characters that are not hotkeys. Fixing this needs a mode toggle (a key that suspends hotkeys while a URL is typed) — a UX decision, not a bug fix.
- Headless verification: `ow render <url>` (shell builtin in `shell.c`, weak-linked to `ow_core_navigate()` + `ow_core_dump_active()` in `openweb_core.c`) fetches the page with the Rust backend, renders with `ow_render_rs`, and dumps the text grid + links + fields to the console. `ow state` prints `can_go_back`/`can_go_forward`/`load_progress`, and `ow back`/`ow forward`/`ow stop` drive the navigation API from the shell — all pure getters or the same core calls the frontends make, so they need no network of their own. Host a page with `python3 -m http.server 8000 --bind 0.0.0.0`, then boot the ISO with `-vga none -nographic -monitor none -netdev user,id=net0 -device e1000,netdev=net0` and run `ow render http://10.0.2.2:8000/test.html`.
- **`cache_lookup`/`cache_store` must be given a NUL-bounded slice, never a whole buffer.** `CACHE_URL` is `[u8; OW_URL_MAX]`, and both functions probe `CACHE_URL[i][url.len()]` to confirm the stored key is NUL-terminated. Handing them `&final_url` (the full 512-byte array) made that index 512 — one past the end — and the `CACHE_URL[i][..].len() >= url.len()` guard written to catch it was a tautology (`512 >= 512`), so it caught nothing. With `panic = "abort"` and a silent `loop {}` panic handler the result was an unreported system-wide hang, not a crash. Both now reject `url.len() >= OW_URL_MAX` and are called with `&final_url[..strlen_u8(&final_url) + 1]`; the same slicing is applied in `ow_go_back`/`ow_go_forward`, where `buf_as_str()` UTF-8-validates the *whole* slice and so would otherwise read stale bytes past the NUL and silently misclassify a URL as a search query.
- A Rust panic here prints `OWPANIC: <file>:<line>` on COM1 before spinning. The previous handler was a bare `loop {}`, which is indistinguishable from a hang or a wedged scheduler — always check for that line when OpenWeb stops responding. Note COM1 is a *port* (`in`/`out`), not memory-mapped: dereferencing `0x3F8` faults with `CR2=0x3fd`.
- The full CSS/`litehtml` layout engine is **not** wired up yet (the upstream `litebrowser-linux` `litehtml` submodule is empty); rendering is the text-grid engine above. `libow_http.a` has no Make prerequisites if the rule is left bare — `kernel/Makefile`'s `RUST_LIB` rule depends on `kernel/kernel/rust_ow/src/*.rs` + `Cargo.toml`.
