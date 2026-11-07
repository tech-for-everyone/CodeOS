# Swift integration

CodeOS has a host-side Swift smoke test and a Swift implementation of the
`cmatrix` utility. Swift is not currently a native CodeOS userland language:
the operating system does not ship an `x86_64-unknown-none` Swift standard
library, allocator, concurrency runtime, or syscall ABI.

The container project is macOS-oriented, so it is not linked into the
freestanding kernel. `make swift-check` compiles and runs a standard-library-only Linux Swift
probe when a Swift 6 compiler is installed. It reports a skip when no compiler
is available; it does not fetch packages or require a network connection.

The offline IDE can edit Swift files and generate a small Swift template.
Compiling or running that source still requires a Swift toolchain and runtime
installed in the system where the IDE runs.

The default CodeOS runtime remains C/Rust. A native Swift runtime still needs
an `x86_64-unknown-none` Swift standard library, allocator, concurrency
runtime, and syscall ABI before Swift programs can be placed in the image.

## Swift CLI Utilities

- **cmatrix (`swift/cmatrix.swift`)**: Analogous Swift implementation of the Matrix digital rain terminal utility, featuring ANSI colors, Katakana and ASCII streams, variable speed, bold leading glyphs, and screensaver modes.
- Build and run:
  ```sh
  swiftc swift/cmatrix.swift -o tools/cmatrix-swift
  ./tools/cmatrix-swift
  # Or via Makefile:
  make cmatrix-swift
  ```
