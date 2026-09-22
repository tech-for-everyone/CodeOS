# CodeOS Qt6 Examples

cmake_minimum_required(VERSION 3.16)
project(CodeOSQt6Examples LANGUAGES C)

# Find the parent CodeOS Qt6 support libraries
get_filename_component(CODEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Test window example
add_executable(test_window test_window.c)
target_link_libraries(test_window PRIVATE codeos_platform codeos_posixstubs)
