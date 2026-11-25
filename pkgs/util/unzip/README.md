# unzip port

`src/unzip.c` is a compact freestanding ZIP reader, not a build of Info-ZIP 6.0.
The upstream tarball is kept in `ports/unzip/upstream/` for reference only.

## What works

- `unzip ARCHIVE` extracts all members, creating directories as needed.
- `unzip ARCHIVE MEMBER...` extracts only the named members.
- `unzip -l ARCHIVE` lists names, sizes and compression methods.
- `unzip -p ARCHIVE MEMBER` writes a member's raw bytes to stdout with no
  surrounding status text.
- A backward EOCD scan with comment-length validation handles archives with
  comments. CRC-32 is checked before any file is written.

Archives written by this tree's `zip` and by Python's `zipfile` (stored) can be
extracted here.

## Deviations

- Only method 0 (stored) is supported. Deflate members are listed but
  extraction fails with a message naming the unsupported method.
- Absolute paths and `..` components are rejected before touching the filesystem.
- The kernel's 64 KiB `FS_CONTENT_MAX` limits the whole archive.

## Tests

```sh
sh pkgs/util/unzip/tests/run.sh
```

The script compiles `src/unzip.c` against `ports/tests/codeos_fs_shim.h` and
checks it against Python-generated archives, including corrupt CRC, deflate,
commented archives and traversal attempts.
