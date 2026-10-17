# bas (CodeOS integration)

**bas** is a systems programming language: C-style syntax with Rust-inspired
ownership, borrowing, sum types, pattern matching, generics and traits. Its
reference compiler is written in Rust and lowers to C, delegating final machine
code generation to the host C compiler (`cc`).

The language lives in its own repository,
[`CodeOS-Comunity/bas`](https://github.com/CodeOS-Comunity/bas). CodeOS treats it
as an **optional sibling checkout**, the same way OpenWeb is integrated: nothing
in `make -C kernel all` depends on it, but the root Makefile picks it up when a
`bas` checkout sits next to the CodeOS tree.

## Build / check from the CodeOS tree

```sh
# `../bas` (or $BAS_DIR) is the sibling checkout
make bas         # build the compiler with cargo (produces compiler/target/debug/bas)
make bas-check   # build + run the full example and error-diagnostic suite
```

`make check` runs `bas-check` too, and skips it with a message when there is no
checkout rather than failing the build.

## Language sources use the `.ba` extension

Programs are `*.ba` (`examples/hello.ba`), and the compiler's test-runner and
docs use that extension throughout.

## Use

```sh
cd ../bas
cargo run -p bas-cli -- run  examples/hello.ba
cargo run -p bas-cli -- build examples/enums.ba -o /tmp/enums
cargo run -p bas-cli -- check  examples/structs.ba
cargo run -p bas-cli -- emit-c examples/hello.ba
```

See the sibling repository's `SPEC.md` for the language specification.
