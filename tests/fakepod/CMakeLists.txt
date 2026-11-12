# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# Fake iPod disk images (see README.md). The library is used by the tests of
# the installer; the tool writes one image by hand.
qt_add_library(tacet_fakepod STATIC
    bootloaderfile.cpp
    bootloaderfile.h
    fakepod.cpp
    fakepod.h
    fixtures.cpp
    fixtures.h
)
target_include_directories(tacet_fakepod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_fakepod PUBLIC Qt6::Core)

qt_add_executable(tacet-fakepod main.cpp)
target_link_libraries(tacet-fakepod PRIVATE tacet_fakepod)

qt_add_executable(tst_fakepod tst_fakepod.cpp)
target_link_libraries(tst_fakepod PRIVATE tacet_fakepod Qt6::Test)
target_compile_definitions(tst_fakepod PRIVATE
    TACETOS_FAKEPOD_TOOL="$<TARGET_FILE:tacet-fakepod>"
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_fakepod COMMAND tst_fakepod)

qt_add_executable(tst_fixtures tst_fixtures.cpp)
target_link_libraries(tst_fixtures PRIVATE tacet_fakepod Qt6::Test)

add_test(NAME tst_fixtures COMMAND tst_fixtures)
