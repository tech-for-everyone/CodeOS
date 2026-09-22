cmake_minimum_required(VERSION 3.16)
project(CodeOSQt6Support LANGUAGES C CXX ASM)

set(CMAKE_FIND_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/sysroot/usr)

# Find SDL3 (installed by build-sdl3.sh into sysroot/usr/lib/cmake/SDL3)
find_package(SDL3 CONFIG REQUIRED)

# C++17 for Qt6 compatibility
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ASM flags: x86_64-elf-as is not GCC, don't pass C/CXX flags
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS_RELEASE "" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS_DEBUG "" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS_RELWITHDEBINFO "" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS_MINSIZEREL "" CACHE STRING "" FORCE)

# Build as static libraries
set(BUILD_SHARED_LIBS OFF)

# Freestanding flags to avoid conflicts with host libc (C/CXX only, not ASM)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-ffreestanding>)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-fno-builtin>)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wall>)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wno-builtin-declaration-mismatch>)

# ── POSIX Stubs Library ──
enable_language(ASM)
add_library(codeos_posixstubs STATIC
    posixstubs/src/posixstubs.c
    posixstubs/src/cppstubs.cpp
    posixstubs/src/qt6stubs.c
    posixstubs/src/setjmp.S
    posixstubs/src/syscall_defs.c
)

target_include_directories(codeos_posixstubs PUBLIC
    posixstubs/include
    posixstubs/src
)

# ── C Platform Bridge (standalone, no Qt6 headers needed) ──
add_library(codeos_platform_c STATIC
    platform/codeos_platform.c
)

target_include_directories(codeos_platform_c PUBLIC
    platform
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/arch/x86_64
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/drivers
)

target_link_libraries(codeos_platform_c PUBLIC
    codeos_posixstubs
)

# ── Font Engine Library ──
add_library(codeos_fonts STATIC
    fonts/src/codeos_font.c
    fonts/src/codeos_freetype.c
)

target_include_directories(codeos_fonts PUBLIC
    fonts/include
)

# ── Image Loader Library ──
add_library(codeos_image STATIC
    image/src/codeos_image.c
)

target_include_directories(codeos_image PUBLIC
    image/include
)

# ── SDL3 Support Library ──
add_library(codeos_sdl3 STATIC
    sdl3-demo/sdl3_demo.c
)

target_include_directories(codeos_sdl3 PUBLIC
    sdl3-demo
)

target_link_libraries(codeos_sdl3 PUBLIC
    codeos_posixstubs
)

# ── Desktop Environment Library ──
add_library(codeos_desktop STATIC
    desktop/src/codeos_desktop.c
    desktop/src/codeos_wm.c
    desktop/src/codeos_theme.c
    desktop/src/codeos_renderer.c
)

target_include_directories(codeos_desktop PUBLIC
    desktop/include
)

target_link_libraries(codeos_desktop PUBLIC
    codeos_fonts
    codeos_image
)

# ── SDL3 Window Manager (placement only) ──
add_library(codeos_sdl3_wm STATIC
    sdl3-wm/src/sdl3_wm.c
)

target_include_directories(codeos_sdl3_wm PUBLIC
    sdl3-wm/include
)

target_link_libraries(codeos_sdl3_wm PUBLIC
    codeos_posixstubs
)

# ── SDL3 Desktop Environment ──
add_library(codeos_sdl3_desktop STATIC
    sdl3-desktop/src/sdl3_desktop.c
    sdl3-desktop/src/sdl3_cpu_driver.c
    sdl3-desktop/src/sdl3_cpu_amd.c
    sdl3-desktop/src/sdl3_cpu_intel.c
)

target_include_directories(codeos_sdl3_desktop PUBLIC
    sdl3-desktop/include
    sdl3-demo
)

target_link_libraries(codeos_sdl3_desktop PUBLIC
    codeos_posixstubs
    SDL3::SDL3
)

# ── Install targets ──
install(TARGETS codeos_posixstubs codeos_platform_c codeos_fonts codeos_image codeos_desktop codeos_sdl3 codeos_sdl3_wm codeos_sdl3_desktop
    ARCHIVE DESTINATION lib
)

install(DIRECTORY posixstubs/include/
    DESTINATION include
)

install(DIRECTORY fonts/include/
    DESTINATION include
)

install(DIRECTORY image/include/
    DESTINATION include
)

install(DIRECTORY desktop/include/
    DESTINATION include
)

install(DIRECTORY sdl3-demo/
    DESTINATION include/sdl3-demo
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY sdl3-desktop/include/
    DESTINATION include/sdl3-desktop
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY sdl3-wm/include/
    DESTINATION include/sdl3-wm
    FILES_MATCHING PATTERN "*.h"
)

message(STATUS "CodeOS Qt6 Support built successfully")
message(STATUS "  POSIX stubs: posixstubs/ (+ qt6stubs.c + setjmp.S)")
message(STATUS "  C platform bridge: platform/codeos_platform.c")
message(STATUS "  C++ platform plugin: platform/codeos_plugin.cpp (built inside Qt6)")
message(STATUS "  Font engine: fonts/")
message(STATUS "  Image loader: image/")
message(STATUS "  Desktop environment: desktop/")
message(STATUS "  SDL3 support: sdl3-demo/")
message(STATUS "  SDL3 desktop: sdl3-desktop/")
