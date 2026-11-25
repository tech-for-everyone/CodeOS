cmake_minimum_required(VERSION 3.16)
project(sdl3_desktop C)

set(CMAKE_SYSTEM_NAME CodeOS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-elf-gcc)

set(CMAKE_C_FLAGS "-ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -fno-stack-protector -fno-PIC -Wall -Wextra -O2 -D_GNU_SOURCE -D__codeos__")

# Derive every path from this file's own location: the checkout may live
# anywhere and on Linux, macOS or Termux.  Override CODEOS_ROOT for an
# out-of-tree SDK.
get_filename_component(CODEOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CODEOS_ROOT "${CODEOS_ROOT}" CACHE PATH "CodeOS checkout root")
set(SDL3_INCLUDE "${CODEOS_ROOT}/qt6/sysroot/usr/include")
set(SDL3_LIB "${CODEOS_ROOT}/qt6/sysroot/usr/lib/libSDL3.a")
set(POSIXSTUBS_LIB "${CODEOS_ROOT}/qt6/support-build/libcodeos_posixstubs.a")

add_library(codeos_sdl3_desktop STATIC
    src/sdl3_desktop.c
    src/sdl3_cpu_driver.c
    src/sdl3_cpu_amd.c
    src/sdl3_cpu_intel.c
)

target_include_directories(codeos_sdl3_desktop PUBLIC
    include
    ${SDL3_INCLUDE}
)

target_link_libraries(codeos_sdl3_desktop PUBLIC
    ${SDL3_LIB}
    ${POSIXSTUBS_LIB}
)

add_executable(sdl3-desktop src/sdl3_desktop.c src/sdl3_cpu_driver.c src/sdl3_cpu_amd.c src/sdl3_cpu_intel.c)
target_include_directories(sdl3-desktop PRIVATE include ${SDL3_INCLUDE})
target_link_libraries(sdl3-desktop PRIVATE ${SDL3_LIB} ${POSIXSTUBS_LIB})
set_target_properties(sdl3-desktop PROPERTIES OUTPUT_NAME sdl3-desktop)
