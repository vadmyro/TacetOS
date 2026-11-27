# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The health of the disk and the battery (docs/specs/health.md): the files the
# firmware writes, the blocks of the disk, the verdicts and the history kept
# on the computer. Qt Core only.
qt_add_library(tacet_health STATIC
    battery.cpp
    battery.h
    blocks.cpp
    blocks.h
    disk.cpp
    disk.h
    files.cpp
    files.h
    history.cpp
    history.h
    reading.cpp
    reading.h
)
target_include_directories(tacet_health PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_health PUBLIC Qt6::Core tacet_device)
