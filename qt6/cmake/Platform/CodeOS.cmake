# CMake platform description for the freestanding CodeOS target.
# CodeOS is not a Linux kernel; its POSIX-shaped APIs are compatibility
# interfaces used by selected applications and by Qt's support layer.

set(CMAKE_SYSTEM_NAME CodeOS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "")
set(CMAKE_DL_LIBS "")
