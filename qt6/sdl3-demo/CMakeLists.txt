cmake_minimum_required(VERSION 3.16)
project(sdl3_demo C)

set(CMAKE_SYSTEM_NAME CodeOS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-elf-gcc)

set(CMAKE_C_FLAGS "-ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -fno-stack-protector -fno-PIC -Wall -Wextra -O2 -D_GNU_SOURCE -D__codeos__")

# Derive the sysroot from this file's own location so the checkout can live
# anywhere (and on any host).  Override SDL3_SYSROOT for an out-of-tree SDK.
get_filename_component(CODEOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(SDL3_SYSROOT "${CODEOS_ROOT}/qt6/sysroot/usr" CACHE PATH "CodeOS Qt sysroot")
set(SDL3_INCLUDE "${SDL3_SYSROOT}/include")
set(SDL3_LIB "${SDL3_SYSROOT}/lib/libSDL3.a")

add_executable(sdl3_demo sdl3_demo.c)
target_include_directories(sdl3_demo PRIVATE ${SDL3_INCLUDE})
target_link_libraries(sdl3_demo ${SDL3_LIB} codeos_posixstubs)
