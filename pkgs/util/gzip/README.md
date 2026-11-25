# gzip port

`src/` contains a freestanding gzip implementation, not a build of the GNU
gzip 1.12 source tarball. The upstream archive is kept in
`ports/gzip/upstream/` for reference only.

## Why a new implementation

GNU gzip is built on autoconf and a POSIX libc; neither is available in the
CodeOS freestanding toolchain. The port instead uses the same syscalls as the
rest of the userspace tree (`sys_open`, `sys_read`, `sys_pwrite`, `sys_write`)
and reuses the puff-style raw-DEFLATE decoder from `core/codec` in
`src/inflate.c`, vendored so this package has no cross-package dependency.

## What works

- `gzip FILE` writes `FILE.gz`; `gzip -c FILE` writes the stream to stdout.
- `gzip -d FILE.gz` / `gunzip FILE.gz` restores `FILE`; `-c` sends it to stdout.
  An `argv[0]` ending in `gunzip` implies `-d`.
- `gzip -l FILE.gz` prints the uncompressed size and CRC without writing a file.
- gzip headers with FEXTRA, FNAME, FCOMMENT and FHCRC are skipped correctly.
- The CRC-32 and ISIZE trailer are both checked on decompression.

## Deviations

- Compression emits **stored** DEFLATE blocks. This is a valid gzip stream
  (readable by gzip/zlib/Python), but there is no Huffman compressor, so the
  output is usually larger than GNU gzip's.
- The kernel's `FS_CONTENT_MAX` caps files at 64 KiB. Inputs above that are
  rejected explicitly instead of being silently truncated.
- No multi-member concatenation, recursion into directories, or GNU-style
  flag options (`-r`, `-9`, ...).

## Tests

```sh
sh pkgs/util/gzip/tests/run.sh
```

The script compiles the real sources against `ports/tests/codeos_fs_shim.h`
and checks interoperability with Python's `gzip` module, plus corrupt-input
and over-cap rejection.
