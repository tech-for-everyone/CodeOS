# NetBeam: userspace port + multi-format packaging

## Verified findings (evidence, not inference)

1. **NetBeam has no standalone binary today.**
   - `pkgs/extra/qt_apps/netbeam.cpp` -> `qt6/panels/netbeam.o` only.
   - `qt6/panels/Makefile:120` is `all: $(OBJS)` — no link target, ever.
   - Linked into the kernel image: `kernel/Makefile:431` `$(TARGET): ... $(QT6_OBJS) ...`.

2. **Why it can't be a userspace ELF as-is.**
   - `nm -u qt6/panels/netbeam.o` = 116 undefined symbols.
   - Only 6 exist in a userspace ELF (`apk-parser`): memcpy/memmove/strcmp/strlen/memchr/snprintf.
   - The rest are kernel-internal: `tcp_*` (11), `fs_*`, `sched_*`, `ip_get_addr`, `kprintf`, `c_text`/`c_green`/... .
   - Qt itself is kernel-only: `libQt6*.a` built with no libc; userspace links with `-nostdlib -nostartfiles`.
   => The Qt GUI cannot be reused. The **protocol** is what ports.

3. **The userspace socket ABI is missing listen/accept.**
   - `include/codeos/syscall_abi.h` tops out at `SYSCALL_SOCKET_GETSOCKOPT 77`. No listen, no accept.
   - But `kernel/kernel/socket.c:156 socket_listen()` and `:174 socket_accept()` are implemented and
     correctly wrap `tcp_listen()`/`tcp_accept()` (`kernel/kernel/tcp.c:114,125`).
   - NetBeam's *receiver* is unusable from userspace until these are exposed.
   - 78 and 79 are free.

4. **Host packaging tooling gaps** (`command -v`):
   - `dpkg-deb` MISSING, `dpkg` MISSING, `zip` MISSING, `aapt`/`aapt2`/`zipalign`/`apksigner` MISSING.
   - `ar` present, `keytool` present, `python3` present (zipfile+tarfile).
   => `.deb` built by hand via `ar` (verified working in /tmp/opencode/debtest);
      `.apk` built via python `zipfile` + AXML writer + `keytool` RSA key.

5. **`apk-parser` cannot read a real APK.** `char buf[8192]` + `sys_read` reads only the first 8191
   bytes; a ZIP's EOCD lives at EOF. Any APK > ~8 KB fails with "not a valid ZIP/APK file".

## Plan

### Phase 1 — kernel ABI: expose listen/accept
- `include/codeos/syscall_abi.h`: add `SYSCALL_SOCKET_LISTEN 78`, `SYSCALL_SOCKET_ACCEPT 79`.
- `kernel/kernel/syscall.c`: dispatch both, following the existing GETSOCKNAME/GETPEERNAME
  copy-to-user pattern (accept needs a userspace sockaddr_t + addrlen pointer).
- `kernel/userspace/include/socket.h`: `sock_listen()`, `sock_accept()`.
- Verify: clean kernel build; existing 12 socket syscalls unaffected (no renumbering).

### Phase 2 — userspace port (`pkgs/extra/netbeam/src/netbeam.c`)
Pure C, `int $0x80` syscalls only. Same NB1 wire protocol:
- UDP 54917 beacon discovery + reply
- TCP 54918 transfer, `NB1|<name>|<size>\n` + payload
- subcommands: `serve` (listen), `send <ip> <file>`, `discover`, `selftest`
- VFS via `sys_open`/`sys_write`/`sys_mkdir`; receive into `/NetBeam/Incoming/`
- negative controls: `selftest` runs without network and asserts on real values.

### Phase 3 — build wiring
- `kernel/userspace/Makefile`: add `netbeam` to PROGRAMS.
- `pkgs/extra/netbeam/manifest.json`.
- Kernel `USER_PROGS` + `disk.img` /bin count must match (per AGENTS.md verification rule).

### Phase 4 — packages
- `scripts/mkpkg_netbeam.py` — one metadata source, three emitters:
  - `.xora` via `scripts/xora.py` (native; CodeOS's only installable format)
  - `.apk` — real ZIP: `AndroidManifest.xml` (binary AXML), `classes.dex`, `resources.arsc`,
    `META-INF/` v1 signature from a `keytool` RSA key.
  - `.deb` — `ar`: `debian-binary` + `control.tar.gz` + `data.tar.gz`.

## Verification (must actually run, not infer)
- Each artifact opened by a real reader: `ar t` + `tar tzf` for .deb; python zipfile +
  a written AXML struct-walk for .apk; `xora.py inspect` for .xora.
- Boot QEMU on a **scratch copy** of disk.img; confirm `kernel/disk.img` sha256 unchanged
  (`d3c50f61a45871d887ba8ba8a101a18e52337393d5c1ae56986c2f1bd3141315`).
- `netbeam selftest` marker line on the serial log.
- `debugfs -R "ls -l /bin"` count == len(USER_PROGS).

## Out of scope / honest limits
- The Qt GUI stays kernel-side. This port is the *protocol*, as a CLI.
- CodeOS cannot install .apk or .deb; those are for external hosts. Only .xora installs on CodeOS.
- `apk-parser`'s 8 KB ceiling is a separate pre-existing bug; not fixed here unless asked.
---

# Outcome (all four phases done, with what actually ran)

## Phase 1 — kernel ABI
`SYSCALL_SOCKET_LISTEN 78` / `SYSCALL_SOCKET_ACCEPT 79` appended to
`include/codeos/syscall_abi.h` (never renumbered), dispatched in
`kernel/kernel/syscall.c`, declared in `kernel/userspace/include/socket.h`
(addr/addrlen passed via `r10`/`r8`). Both were already implemented in
`kernel/kernel/socket.c`; they were simply unreachable from userspace.

## Phase 2 — userspace port
`pkgs/extra/netbeam/src/netbeam.c` builds `kernel/userspace/netbeam`, a real
standalone ELF. `nm -u` is empty.

Four real bugs found and fixed by the tests, not by inspection:
1. **Byte order.** The host shim fed `s_addr` straight to POSIX
   `sin_addr.s_addr`. CodeOS carries it as a big-endian-packed *integer* (the
   kernel's `addr_to_sock` copies it through and `ip.c` prints it with
   `>> 24`), so the shim was connecting to `1.0.0.127` instead of
   `127.0.0.1`. On a host listening only on loopback that manifests as
   `connect()` hanging forever, which reads exactly like a hang in the code
   under test. Fixed with `htonl`/`ntohl` in `netbeam_shim.h`.
2. **Filename mangling.** The sender put the *whole path* on the wire, so the
   receiver's sanitizer flattened it: `/tmp/x/payload.bin` was stored as
   `tmpnetbeam-hosttestpayload.bin`. Fixed with `nb_basename()`. The Qt
   panel was already correct (it uses `QFileInfo::fileName`); the port was
   the one at fault.
3. **Over-strict sanitizer.** The port stripped `.`, which renamed every real
   file to `hellotxt`. Now matches the Qt panel (drop `/`, `\`, ` `) plus
   `:` and control characters; traversal is still impossible because it needs
   a separator and both separators are gone.
4. **Dead retry loop** around the inbox create, and a hardcoded
   `mkdir "/NetBeam"` that broke the redirected inbox. Both replaced with a
   walk of the inbox's parent chain.

## Phase 3 — build wiring
`netbeam` is a `CORE_PROG` (no android lib needed), not in `PROGRAMS`.
Added to `kernel/Makefile`'s `USER_PROGS`; `pkgs/extra/netbeam/manifest.json`
written.

## Phase 4 — packages
`scripts/mkpkg_netbeam.py` (orchestrator) + `scripts/apkpack.py` +
`scripts/debpack.py`, all three reading one manifest.
`make netbeam-pkg` drives it and verifies each artifact.

### Bugs the packaging itself introduced, and caught
- `ar` header `name.ljust(16)` returned `str`, not `bytes` - crash on write.
- Directory parents derived from the already `./`-prefixed arcname, so the
  first parent was `.` and every directory became `././usr`. tar then unpacked
  `.` as a literal directory and every real path failed with "not a
  directory".
- `TarInfo` defaults to `REGTYPE`; directories written without `DIRTYPE` land
  as zero-length *files*. Same visible symptom, different cause.
- AXML `END_DOCUMENT` is only the 16-byte node header. Appending a
  prefix/uri pair left 8 bytes past the end of the chunk chain, which a
  parser reads as a bogus trailing chunk.
- DEX `file_size` was measured from the post-append data length, double
  counting the pre-map bytes.
- `Path.replace` for the signed APK: rename(2) fails `EXDEV` when TMPDIR is a
  different filesystem than the output. `shutil.move` instead. Only visible
  when building into the repo from `/tmp`.

## Verification that actually ran

| check | result |
|---|---|
| `make netbeam-check` | 12/12 selftest + 200-byte loopback byte-identical |
| negative control: revert the basename fix | loopback FAILS with the mangled name |
| `make -C kernel all` | clean, `netbeam` in the initramfs |
| `/bin` count vs `len(USER_PROGS)` | 34 == 34, nothing missing |
| `/bin/netbeam` on disk | byte-identical to `kernel/userspace/netbeam` |
| QEMU boot (scratch `disk.img`) | clean to `root#`, **0 faults** |
| `exec /bin/netbeam selftest` **in the OS** | **12/12, exit status 0** |
| `.xora` via `xora inspect` + `xora install --root` | ELF installed byte-identical |
| `.deb` via `ar t`, `tar tzf`, `md5sum -c` | all pass |
| `.deb` negative control: tamper 1 byte | `md5sum: FAILED`, exit 1 |
| `.apk` via `unzip -t`, `zipinfo` | no errors, exec bit intact |
| `.apk` via `jarsigner -verify` | **jar verified.** |
| `.apk` negative control: tamper `classes.dex` | `SHA-256 digest error`, exit 1 |
| DEX/AXML structural readers | pass; forged out-of-range offsets rejected |
| `.deb` build reproducibility | byte-identical across runs |

`kernel/disk.img` was never the boot target: QEMU used
`/tmp/opencode/boot-test.img`, a copy.

## Two things found along the way, deliberately NOT changed

1. **The kernel shell cannot run any `/bin/*` program.** `shell.c`'s dispatch
   chain is entirely builtins and ends in
   `kprintf("%s: command not found\n", cmd)` (`kernel/kernel/shell.c:4285`);
   it never consults `/bin`. `exec <elf>` does exist and works - it is
   dispatched at line 4178 but is missing from the `builtins[]` completion
   table, which is a cosmetic inconsistency, not a bug. That is how the
   in-guest selftest was run.
2. **`sys_fork` produces a process nothing schedules.** `proc_create` only
   sets `current_process` when it is 0, and nothing calls
   `sched_switch_to_process` after a fork, so a forked child never runs. An
   in-guest `serve`+`send` loopback is therefore impossible without a process
   scheduler. This is a kernel architecture limit, not a NetBeam defect, and
   the host loopback test covers the protocol instead.

## Honest limits of the packages

- Only `.xora` installs on CodeOS. CodeOS has no `.deb` or `.apk` installer;
  those two are for real Debian/Android hosts.
- The APK declares `android:hasCode="false"`: the payload is a native ELF with
  no Java entry point. It is a well-formed, signed APK, but it will not
  install-and-run on a stock Android device. The DEX exists so the container
  is structurally complete, not to run anything.
- The DEX and the binary manifest are written from their format specs and
  re-parsed by `apkpack.py`'s own independent readers. There is no aapt, d8,
  apksigner or dexdump on this host, so neither has been cross-checked
  against a reference implementation. The JAR signature *is* real and *is*
  validated by jarsigner.
- `apk-parser`'s 8 KB read ceiling is a separate pre-existing bug, still not
  fixed: any real APK larger than ~8 KB fails its "not a valid ZIP/APK" check.
