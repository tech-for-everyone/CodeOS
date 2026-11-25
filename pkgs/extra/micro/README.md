# micro (CodeOS)

A small terminal text editor with the familiar micro-style keys.

## Why this is not upstream micro

Upstream [micro](https://github.com/zyedidia/micro) is a Go program with a
terminal/Unicode runtime that cannot be built against the CodeOS freestanding
toolchain. This package provides the everyday editing workflow with the same
syscalls the rest of the userspace tree uses, in a single C file.

## Keys

| Key | Action |
| --- | --- |
| Arrow keys, Home/End, Ctrl-A/Ctrl-E | move the cursor |
| Enter, Backspace, Delete, Tab | edit (Tab = two spaces) |
| Ctrl-K | delete to end of line |
| Ctrl-O | open a file (prompts for the path) |
| Ctrl-S | save |
| Ctrl-Q | quit (twice when there are unsaved changes) |

`micro FILE` opens `FILE`; a missing file starts an empty buffer that can be
saved with Ctrl-S.

## Limits

- 256 lines x 200 columns in memory; files are capped at 64 KiB by the kernel.
- ASCII only — no Unicode or mouse support.
- No undo history, search, split views or plugin support.
- On CodeOS, `O_TRUNC` acts on the fd's offset rather than truncating the file,
  so saving a shorter version of an existing file may leave trailing bytes.

## Tests

```sh
sh pkgs/extra/micro/tests/run.sh
```

The script compiles the real editor against `ports/tests/codeos_fs_shim.h`,
drives it with piped keystrokes and checks the saved bytes.

## Build wiring

`install_path` is `/bin/micro`, but the root Makefile is outside `pkgs/` and
cannot be changed under the current scope rule. A maintainer must register the
source in the top-level build before it appears in an image.
