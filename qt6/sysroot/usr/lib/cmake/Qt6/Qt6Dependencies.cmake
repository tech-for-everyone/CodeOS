# Copyright (C) 2024 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

set(Qt6_FOUND FALSE)

if(DEFINED QT_REQUIRE_HOST_PATH_CHECK)
    set(__qt_platform_requires_host_info_package "${QT_REQUIRE_HOST_PATH_CHECK}")
elseif(DEFINED ENV{QT_REQUIRE_HOST_PATH_CHECK})
    set(__qt_platform_requires_host_info_package "$ENV{QT_REQUIRE_HOST_PATH_CHECK}")
elseif(CMAKE_CROSSCOMPILING OR DEFINED QT_HOST_PATH)
    set(__qt_platform_requires_host_info_package "TRUE")
else()
    set(__qt_platform_requires_host_info_package FALSE)
endif()
set(__qt_platform_initial_qt_host_path "/usr")
set(__qt_platform_initial_qt_host_path_cmake_dir "/usr/lib/cmake")

_qt_internal_setup_qt_host_path(
    "${__qt_platform_requires_host_info_package}"
    "${__qt_platform_initial_qt_host_path}"
    "${__qt_platform_initial_qt_host_path_cmake_dir}")
_qt_internal_find_host_info_package(${__qt_platform_requires_host_info_package}
    Qt6)

# note: _third_party_deps example: "ICU\\;FALSE\\;1.0\\;i18n uc data;ZLIB\\;FALSE\\;\\;"
set(__qt_third_party_deps "")



# Don't propagate REQUIRED so we don't immediately FATAL_ERROR, rather let the find_dependency calls
# set _NOT_FOUND_MESSAGE which will be displayed by the includer of the Dependencies file.
set(${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED FALSE)

if(__qt_third_party_deps)
    _qt_internal_find_third_party_dependencies(Qt6 __qt_third_party_deps)
endif()

set(Qt6_FOUND TRUE)
