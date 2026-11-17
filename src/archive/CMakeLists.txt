# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The reader of zip archives (docs/specs/install-wizard.md section 7.6), with
# its own deflate decoder. Qt Core only.
qt_add_library(tacet_archive STATIC
    crc32.cpp
    crc32.h
    inflate.cpp
    inflate.h
    zipreader.cpp
    zipreader.h
)
target_include_directories(tacet_archive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_archive PUBLIC Qt6::Core)
