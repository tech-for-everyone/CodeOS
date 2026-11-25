# zip port

`src/zip.c` is a compact freestanding ZIP writer, not a build of Info-ZIP 3.0.
The upstream tarball is kept in `ports/zip/upstream/` for reference only.

## What works

- `zip ARCHIVE FILE...` writes a standard ZIP with local headers, a central
  directory and an End Of Central Directory record.
- Relative directory components are preserved (`zip out.zip dir/file` stores
  `dir/file`); a leading `/` is stripped so archives never contain absolute
  paths.
- `zip -l ARCHIVE` lists member names and uncompressed sizes.
- Empty files are valid members.

Archives are readable by `unzip`, Python's `zipfile`, 7zip, etc.

## Deviations

- Entries use method 0 (**stored**): the archive is valid but not compressed.
  Use `zip` + a compressor externally when compression matters.
- The kernel's `FS_CONTENT_MAX` caps files at 64 KiB, so the archive and each
  member are bounded by that limit.
- At most 64 members per archive.

## Tests

```sh
sh pkgs/util/zip/tests/run.sh
```

The script compiles `src/zip.c` against `ports/tests/codeos_fs_shim.h` and
validates the output with Python's `zipfile`, including CRC checks.
