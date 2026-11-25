# cora (CodeOS integration)

**cora** is a small Python-inspired language whose compiler emits C, which a C
compiler then turns into machine code. It replaces the previous `bas` package.

```cora
print("Hello World")
```

is a complete program. `cora hello.co hello.c` writes ordinary C that only
needs `<stdio.h>`.

## Why it replaces bas

`bas` was a Rust-based systems language with C-style syntax and Rust-inspired
ownership. It depended on a sibling Rust checkout and `cargo`, neither of which
fits the freestanding CodeOS package tree. cora is a single C file in `src/`,
buildable with the same toolchain as the rest of userspace, and its syntax is
Python-like: indentation blocks, `print(...)`, `#` comments.

## What works

- `name = expression`, integer arithmetic (`+ - * / %`) and parentheses.
- `if` / `elif` / `else`, `while`, and `and` / `or` / `not`.
- `def name(a, b):` with `return expression`; calls to those functions.
- `print(a, b, ...)` separates arguments with spaces and appends a newline,
  like Python. Strings may use `"` or `'`.
- `#` comments and 4-space indentation. Tabs are rejected.

The generated C uses `long` for every value and `printf` for output, so it
builds against both the host C compiler and the CodeOS libc.

## Use

```sh
cora hello.co hello.c       # translate
cc hello.c -o hello         # compile with any C compiler
./hello                     # prints: Hello World
```

With no output argument, `INPUT.c` is written next to `INPUT.co`. The shell's
`cora <program>` builtin runs prebuilt programs from `/bin/cora-<name>`;
`cora list` shows the ones installed in the image.

## Limits

- Integers only; no floating point, strings as values beyond `print`, lists,
  objects, exceptions, imports or modules.
- One file per program, 512 lines / 64 KiB source cap, 16 arguments per call.
- Functions always return `long`; there is no type system to check mismatches.
- Root build rules live outside `pkgs/` and still need registering for the
  compiler and the example programs to be installed in an image.

## Tests

```sh
sh pkgs/core/cora/tests/run.sh
```

The script compiles `src/cora.c`, translates both example programs, compiles
the generated C with `gcc -Wall -Wextra -Werror`, and checks the output.
