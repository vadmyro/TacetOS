# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The window code lives in a library so the tests can link against it.
qt_add_library(tacetos_ui STATIC
    mainwindow.cpp
    mainwindow.h
)
target_include_directories(tacetos_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tacetos_ui PUBLIC TACETOS_VERSION="${PROJECT_VERSION}")
target_link_libraries(tacetos_ui PUBLIC Qt6::Widgets)

qt_add_executable(tacetos
    main.cpp
)
target_link_libraries(tacetos PRIVATE tacetos_ui)
set_target_properties(tacetos PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "TacetOS"
)
