# Swift integration

CodeOS uses `/home/codeosuser/container` as the initial Swift integration
anchor. The container project is macOS-oriented, so it is not linked into the
freestanding kernel. `make swift-check` compiles and runs a standard-library-
only Linux Swift probe with the available Swift 6 toolchain.

The default CodeOS runtime remains C/Rust. A native Swift runtime still needs
an `x86_64-unknown-none` Swift standard library, allocator, concurrency
runtime, and syscall ABI before Swift programs can be placed in the image.
