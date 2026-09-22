# CodeOS CMake Toolchain File
# Cross-compile Qt6 and applications for CodeOS

set(CMAKE_SYSTEM_NAME CodeOS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# Cross-compiler toolchain
set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_CXX_COMPILER x86_64-elf-g++)
set(CMAKE_ASM_COMPILER x86_64-elf-as)
set(CMAKE_AR x86_64-elf-ar)
set(CMAKE_RANLIB x86_64-elf-ranlib)
set(CMAKE_STRIP x86_64-elf-strip)
set(CMAKE_LINKER x86_64-elf-ld)
set(CMAKE_CXX_COMPILER_AR x86_64-elf-ar)
set(CMAKE_CXX_COMPILER_RANLIB x86_64-elf-ranlib)

# Sysroot for headers/libs (not using CMAKE_SYSROOT because x86_64-elf-as lacks --sysroot)
set(CODEOS_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../sysroot")

# Host system include paths for C/C++ standard headers
# Concatenate into space-separated string to avoid cmake list expansion issues
set(CODEOS_C_INCLUDE_DIRS "-isystem/usr/include -isystem/usr/local/include -isystem/usr/lib/gcc/x86_64-pc-linux-gnu/16/include")
set(CODEOS_CXX_INCLUDE_DIRS "-isystem/usr/include/c++/16 -isystem/usr/include/c++/16/x86_64-pc-linux-gnu -isystem/usr/include/c++/16/backward ${CODEOS_C_INCLUDE_DIRS}")

# Compiler flags for CodeOS freestanding environment
# We include host headers for Qt6 compatibility (stdint, string, math, etc.)
set(CODEOS_C_FLAGS "-ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -fno-stack-protector -fno-PIC -Wall -Wextra -O2 -g -D_GNU_SOURCE -D__codeos__ -DSDL_PLATFORM_UNIX -DSDL_THREAD_GENERIC -DSDL_TIMER_GENERIC -DSDL_FILESYSTEM_POSIX ${CODEOS_C_INCLUDE_DIRS} -isystem${CODEOS_SYSROOT}/usr/include")
set(CODEOS_CXX_FLAGS "-ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -fno-stack-protector -fno-PIC -Wall -Wextra -O2 -g -D_GNU_SOURCE -D__codeos__ -D__STDC_HOSTED__=1 -Wno-builtin-macro-redefined -fno-exceptions -fno-rtti -fno-use-cxa-atexit -std=c++17 ${CODEOS_CXX_INCLUDE_DIRS} ${CODEOS_C_INCLUDE_DIRS} -isystem${CODEOS_SYSROOT}/usr/include")
set(CODEOS_LINK_FLAGS "-nostdlib -z max-page-size=0x1000 -no-pie -Wl,--allow-multiple-definition -L${CODEOS_SYSROOT}/usr/lib")

set(CMAKE_C_FLAGS_INIT "${CODEOS_C_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CODEOS_CXX_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CODEOS_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CODEOS_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CODEOS_LINK_FLAGS}")
set(CMAKE_C_STANDARD_LIBRARIES_INIT "-Wl,--start-group -lminc -lcodeos_posixstubs -Wl,--end-group")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "-Wl,--start-group -lminc -lcodeos_posixstubs -Wl,--end-group")
set(CMAKE_INSTALL_PREFIX "${CODEOS_SYSROOT}/usr" CACHE PATH "" FORCE)

# Search paths (for find_package / find_library)
set(CMAKE_FIND_ROOT_PATH "${CODEOS_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt6 specific: disable features CodeOS doesn't support
set(QT_FEATURE_thread OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_network OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_sql OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_svg OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_xml OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_dbus OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_opengl OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_vulkan OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_wayland OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_xcb OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_x11 OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_directfb OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_linuxfb OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_egl OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_glib OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_pulseaudio OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_alsa OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_javascript OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_webengine OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_printsupport OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_testlib OFF CACHE BOOL "" FORCE)
set(QT_FEATURE_bearer OFF CACHE BOOL "" FORCE)
