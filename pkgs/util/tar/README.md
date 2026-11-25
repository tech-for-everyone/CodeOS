# tar port

`src/tar.c` is a compact freestanding ustar implementation, not a build of the
GNU tar 1.24 source tarball. The upstream archive is kept in
`ports/tar/upstream/` for reference only; GNU tar's gnulib/POSIX layer cannot
be built against the CodeOS freestanding toolchain.

## What works

- `tar -cf ARCHIVE FILE...` writes a POSIX ustar archive with 512-byte
  headers, octal fields, padded members and two zero blocks at the end.
  Directories are discovered recursively with `sys_readdir`.
- `tar -tf ARCHIVE` lists members (directory names get a trailing `/`).
- `tar -xf ARCHIVE` creates files and directories. Absolute paths and `..`
  components are rejected before any filesystem call.

Archives written here are readable by GNU tar and Python's `tarfile`; archives
from those tools can be extracted here.

## Deviations

- `sys_stat` always fails on this kernel (see `core/shell/src/shell.c`), so
  files are distinguished from directories by trying `sys_readdir` on them.
- No symlinks, hardlinks, device nodes, or `-z` compression. Use
  `gzip archive.tar` for the usual `.tar.gz` combination.
- Member paths are limited to 240 bytes and recursion to 4 levels.
- Files and archives are limited to 64 KiB by the kernel's `FS_CONTENT_MAX`.

## Tests

```sh
sh pkgs/util/tar/tests/run.sh
```

The script compiles `src/tar.c` against `ports/tests/codeos_fs_shim.h` and
checks create/list/extract against Python's `tarfile`, plus corrupt-header,
traversal and over-cap rejection.

## Build wiring

The package manifest declares `/bin/tar` as its install path, but the root
Makefile lives outside `pkgs/` and cannot be changed under the current scope
rule. A maintainer must add the source to the top-level build before it lands
in an image.
