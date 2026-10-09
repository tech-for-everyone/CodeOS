# CodeOS Qt 6.11.1 Support

Infrastructure to build Qt 6.11.1 applications for CodeOS. The Qt desktop is
the canonical GUI implementation; the freestanding kernel only supplies the
framebuffer, input, window, and event ABI that the CodeOS platform plugin uses.

CodeOS is not a Linux kernel. Linux support in this tree belongs to the
explicit Linux application/container compatibility layer and is not used as
the desktop platform backend.

## Architecture

```
Qt6 Application (C++)
    │
    ▼
Qt6 Core/Widgets (cross-compiled static libs)
    │
    ▼
QPlatformIntegration (codeos_plugin.cpp)
    │
    ▼
wm_protocol IPC (named pipes)
    │
    ▼
CodeOS Kernel Compositor (xserver)
    │
    ▼
Framebuffer (1280x800+)
```

## Components

### 1. POSIX Stubs (`posixstubs/`)
POSIX-compatible wrappers around CodeOS syscalls:
- `pthread.h` — Thread/mutex/condition variable stubs
- `setjmp.h` — setjmp/longjmp (x86_64 asm, saves all callee-saved regs + SSE/x87)
- `sys/mman.h` — Memory mapping (mmap/munmap)
- `sys/socket.h` — Socket API stubs
- `sys/select.h` — Select/poll stubs
- `sys/stat.h` — File status
- `sys/time.h` — Time functions
- `sys/wait.h` — Process waiting
- `sys/resource.h` — Resource limits
- `qt6stubs.c` — ctype, wchar, stdlib (strtol, strtod, etc.), math, errno, termios, fcntl

### 2. Platform Plugin (`platform/`)
- **C bridge** (`codeos_platform.c`): Standalone C API that Qt6 apps call. Uses Zircon IPC.
- **C++ plugin** (`codeos_plugin.cpp`): Full `QPlatformIntegration` implementation.
  - `CodeOSIntegration` — Main entry, creates windows/screen/clipboard
  - `CodeOSScreen` — Queries framebuffer info from compositor
  - `CodeOSWindow` — Window create/destroy/show/hide/raise/lower
  - `CodeOSBackingStore` — Per-window QImage, blits dirty rects to compositor
  - `CodeOSClipboard` — Stub
  - `CodeOSEventLoop` — Timer integration for Qt event loop

### 3. CMake Toolchain (`cmake/`)
Cross-compilation toolchain for Qt6:
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/codeos-toolchain.cmake
```

### 4. Font Engine (`fonts/`)
Font rendering using embedded bitmap fonts.

### 5. Image Loader (`image/`)
Basic image loading (RGBA, simple formats).

### 6. Desktop Environment (`desktop/`)
Standalone desktop library (macOS-style):
- Window manager with animations
- Dock with magnification
- Menu bar
- Theming (Catppuccin-inspired palette)
- Notification center
- Context menus

## Building

### Build support libraries (no Qt6 needed):
```bash
make qt6          # or: cd qt6 && mkdir build && cd build && cmake .. && make
```

### Cross-compile Qt6 from source:
```bash
make qt6-download    # Download Qt6 source (~1GB)
make qt6-configure   # Configure with CodeOS toolchain
make qt6-build-full  # Build Qt6 (takes a while)
```

Or step-by-step:
```bash
cd qt6 && ./build-qt6.sh download
cd qt6 && ./build-qt6.sh sysroot
cd qt6 && ./build-qt6.sh mkspec
cd qt6 && ./build-qt6.sh configure
cd qt6 && ./build-qt6.sh build
```

## Qt6 Features Disabled
- Threading (single-threaded on CodeOS)
- Networking
- OpenGL / Vulkan
- Wayland / X11 / Linux framebuffer backends
- D-Bus
- SQL
- SVG (rendered by CodeOS's own SVG parser)

## Next Steps
1. Run `make qt6-download` to get Qt6 source
2. Run `make qt6-build-full` to cross-compile Qt6
3. Build a Qt6 app using the CodeOS toolchain
4. Link against CodeOS support libraries
5. Embed into kernel via the panels system
