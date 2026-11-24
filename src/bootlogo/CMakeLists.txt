# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# Boot logo file reading, writing and image conversion
# (docs/specs/boot-screen.md sections 3, 8.3, 8.4).
qt_add_library(tacet_bootlogo STATIC
    bootlogo.cpp
    bootlogo.h
)
target_include_directories(tacet_bootlogo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_bootlogo PUBLIC Qt6::Core Qt6::Gui tacet_device)
