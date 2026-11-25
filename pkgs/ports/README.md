# Porting prebuilt-only packages

Every package that previously existed only as a `.ftech` descriptor now has a
`manifest.json`. The 28 still awaiting a port keep `type: prebuilt`, the
upstream `source` URL, the original declared `size`, and
`port_status: not-started`. The `.ftech` files have been removed —
`manifest.json` is the only descriptor format in this tree.

This directory tracks the work needed to turn those downloads into real source
packages that build against the CodeOS freestanding toolchain. Four have been
ported (`gzip`, `tar`, `zip`, `unzip`), plus a new `micro` editor package that
was requested alongside them.

## Status

| Package | Group | Version | Upstream source | Status |
| --- | --- | --- | --- | --- |
| `7zip` | `dev` | 26.01 | www.7-zip.org/download.html | not-started |
| `binutils` | `dev` | 2.39 | ftp.gnu.org/gnu/binutils/binutils-2.39.tar.xz | not-started |
| `cmake` | `dev` | 3.24.0 | github.com/Kitware/CMake/releases/download/v3.24.... | not-started |
| `gcc` | `dev` | 12.2.0 | ftp.gnu.org/gnu/gcc/gcc-12.2.0/gcc-12.2.0.tar.xz | not-started |
| `gdb` | `dev` | 12.1 | ftp.gnu.org/gnu/gdb/gdb-12.1.tar.xz | not-started |
| `git` | `dev` | 2.38.0 | mirrors.edge.kernel.org/pub/software/scm/git/git-... | not-started |
| `golang` | `dev` | 1.19 | go.dev/dl/go1.19.src.tar.gz | not-started |
| `make` | `dev` | 4.3 | ftp.gnu.org/gnu/make/make-4.3.tar.gz | not-started |
| `node` | `dev` | 18.7.0 | nodejs.org/dist/v18.7.0/node-v18.7.0.tar.xz | not-started |
| `python` | `dev` | 3.10.8 | www.python.org/ftp/python/3.10.8/Python-3.10.8.tgz | not-started |
| `ruby` | `dev` | 3.1.2 | cache.ruby-lang.org/pub/ruby/3.1/ruby-3.1.2.tar.gz | not-started |
| `curl` | `extra` | 7.85.0 | curl.se/download/curl-7.85.0.tar.gz | not-started |
| `less` | `extra` | 590 | www.greenwoodsoftware.com/less/less-590.tar.gz | not-started |
| `nano` | `extra` | 2.9.8 | www.nano-editor.org/dist/v2.9/nano-2.9.8.tar.gz | not-started |
| `openssh` | `extra` | 9.0 | ftp.openbsd.org/pub/OpenBSD/OpenSSH/portable/open... | not-started |
| `rsync` | `extra` | 3.2.5 | download.samba.org/pub/rsync/src/rsync-3.2.5.tar.gz | not-started |
| `telnet` | `extra` | 0.17 | ftp.gnu.org/gnu/inetutils/inetutils-2.3.tar.xz | not-started |
| `vim` | `extra` | 8.2 | github.com/vim/vim/archive/refs/tags/v8.2.0000.ta... | not-started |
| `wget` | `extra` | 1.21.2 | ftp.gnu.org/gnu/wget/wget-1.21.2.tar.gz | not-started |
| `bzip2` | `util` | 1.0.8 | sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz | not-started |
| `file` | `util` | 5.42 | astron.com/pub/file/file-5.42.tar.gz | not-started |
| `gzip` | `util` | 1.12 | ftp.gnu.org/gnu/gzip/gzip-1.12.tar.xz | **ported** |
| `lsof` | `util` | 4.96.4 | github.com/lsof-org/lsof/archive/refs/tags/4.96.4... | not-started |
| `ltrace` | `util` | 0.7.3 | github.com/elkrane/ltrace | not-started |
| `netcat` | `util` | 1.10 | sourceforge.net/projects/netcat/files/netcat/1.10/ | not-started |
| `openssl` | `util` | 3.0.5 | www.openssl.org/source/old/3.0/openssl-3.0.5.tar.gz | not-started |
| `socat` | `util` | 1.7.4 | www.dest-unreach.org/socat/download/ | not-started |
| `strace` | `util` | 5.19 | github.com/strace/strace/releases/download/v5.19/... | not-started |
| `tar` | `util` | 1.24 | ftp.gnu.org/gnu/tar/tar-1.24.tar.gz | **ported** |
| `unzip` | `util` | 6.0 | downloads.sourceforge.net/project/infozip/UnZip%2... | **ported** |
| `xz` | `util` | 5.2.6 | tukaani.org/xz/xz-5.2.6.tar.gz | not-started |
| `zip` | `util` | 3.0 | downloads.sourceforge.net/project/infozip/Zip%203... | **ported** |

28 of the 32 are still `not-started`: their upstream sources are identified,
but no source has been vendored yet. `gzip`, `tar`, `zip` and `unzip` are
**ported** — see below. The URLs above are the starting points recorded in
each manifest.

## Completed ports

| Package | Source | Host tests | Install path |
| --- | --- | --- | --- |
| `gzip` | `pkgs/util/gzip/src/` | `sh pkgs/util/gzip/tests/run.sh` | `/bin/gzip` |
| `tar` | `pkgs/util/tar/src/` | `sh pkgs/util/tar/tests/run.sh` | `/bin/tar` |
| `zip` | `pkgs/util/zip/src/` | `sh pkgs/util/zip/tests/run.sh` | `/bin/zip` |
| `unzip` | `pkgs/util/unzip/src/` | `sh pkgs/util/unzip/tests/run.sh` | `/bin/unzip` |
| `micro` (new) | `pkgs/extra/micro/src/` | `sh pkgs/extra/micro/tests/run.sh` | `/bin/micro` |

These are compact freestanding implementations rather than builds of the
upstream sources: GNU gzip/tar and Info-ZIP depend on autoconf/make and a POSIX
libc, and upstream micro is a Go program. None of those runtimes is available
in the CodeOS toolchain. Downloaded upstream tarballs remain in
`ports/<name>/upstream/` as reference. Each package's `README.md` records the
supported options and deliberate deviations (64 KiB kernel file cap; gzip and
zip use stored blocks; tar has no symlinks/hardlinks or `-z`; unzip reads
stored entries only; micro is ASCII-only with no undo).

Every host suite compiles the real port sources against the shared
`ports/tests/codeos_fs_shim.h` and checks them with independent implementations
(Python's `gzip`, `tarfile` and `zipfile` modules), so interoperability is
verified rather than assumed.

The four converted manifests are now `type: source`, `source: src/`,
`port_status: ported`, with `upstream` recording the original download URL and
`size` retaining the old `.ftech` declared size. `micro` is a new package: it
has no original `.ftech` size, so it omits `size`.

## How to port a package

1. Download the upstream source with
   `python3 pkgs/tools/fetch-source.py <name>`. It writes only into
   `pkgs/ports/<name>/upstream/` and records the expected version.
2. Vendor the subset that is actually needed into
   `pkgs/<group>/<name>/src/`, keeping upstream license files.
3. Replace hosted libc/POSIX calls with CodeOS kernel syscalls or the shims
   already used by `pkgs/core/zircapp/src/shim/`.
4. Update `pkgs/<group>/<name>/manifest.json`:
   `type: source`, `source: src/`, `port_status: ported`, a real
   `install_path`, accurate `dependencies`, and the SPDX `license`.
5. Add a short `README.md` in the package describing the port and any
   deviations from upstream.
6. **Build wiring lives in the root Makefile, outside `pkgs/`.** Under the
   current scope rule this directory cannot add targets there, so a maintainer
   must register the new package with the top-level build before it can be
   compiled into an image.

## Prioritisation

The small, dependency-light utilities are the sensible first ports:

| Order | Packages |
| --- | --- |
| 1 | ~~gzip, tar, zip, unzip~~ (done); bzip2, xz, file, less, nano |
| 2 | curl, wget, netcat, socat, openssl, openssh, rsync, telnet |
| 3 | strace, ltrace, lsof, make, cmake, git, 7zip |
| 4 | gcc, binutils, gdb, golang, python, node, ruby, vim |
