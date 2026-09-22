cmake_minimum_required(VERSION 3.16)
project(sdl3_demo C)

set(CMAKE_SYSTEM_NAME CodeOS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-elf-gcc)

set(CMAKE_C_FLAGS "-ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -fno-stack-protector -fno-PIC -Wall -Wextra -O2 -D_GNU_SOURCE -D__codeos__")

set(SDL3_INCLUDE /home/codeosuser/CodeOS/qt6/sysroot/usr/include)
set(SDL3_LIB /home/codeosuser/CodeOS/qt6/sysroot/usr/lib/libSDL3.a)

add_executable(sdl3_demo sdl3_demo.c)
target_include_directories(sdl3_demo PRIVATE ${SDL3_INCLUDE})
target_link_libraries(sdl3_demo ${SDL3_LIB} codeos_posixstubs)
