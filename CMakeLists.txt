# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

cmake_minimum_required(VERSION 3.21)

project(TacetOS
    VERSION 0.0.1
    DESCRIPTION "Desktop app and custom firmware for the classic iPod"
    HOMEPAGE_URL "https://github.com/vadmyro/TacetOS"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_subdirectory(src/device)
add_subdirectory(src/journal)
add_subdirectory(app)
add_subdirectory(tools/watch)
add_subdirectory(tools/disk)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
