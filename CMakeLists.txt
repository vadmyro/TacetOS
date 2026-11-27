# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

cmake_minimum_required(VERSION 3.21)

project(TacetOS
    VERSION 0.3.0
    DESCRIPTION "Desktop app and custom firmware for the classic iPod"
    HOMEPAGE_URL "https://github.com/vadmyro/TacetOS"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Network)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Writing to a real iPod is refused by every build until the beta (docs/specs/
# bootloader-legacy.md section 11, check 8). Disk image files are always allowed.
option(TACETOS_ALLOW_REAL_WRITES "Allow the installer to write to a real disk" OFF)

# The packaged builds of the beta, the only ones that ignore TACETOS_UPDATE_CHANNEL
# (docs/specs/update-channel.md section 4).
option(TACETOS_PACKAGE_BUILD "Build for a package of the beta" OFF)

include(CTest)

add_subdirectory(src/device)
add_subdirectory(src/installer)
add_subdirectory(src/helper)
add_subdirectory(src/journal)
add_subdirectory(src/archive)
add_subdirectory(src/bootlogo)
add_subdirectory(src/wizard)
add_subdirectory(src/update)
add_subdirectory(src/health)
add_subdirectory(app)
add_subdirectory(tools/watch)
add_subdirectory(tools/disk)
add_subdirectory(tools/dfu)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
