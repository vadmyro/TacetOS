# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The window code lives in a library so the tests can link against it.
qt_add_library(tacetos_ui STATIC
    mainwindow.cpp
    mainwindow.h
)
target_include_directories(tacetos_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tacetos_ui PUBLIC TACETOS_VERSION="${PROJECT_VERSION}")
target_link_libraries(tacetos_ui PUBLIC Qt6::Widgets Qt6::Svg)

# The drawings of the models, read as :/models/<id>.svg (resources/models/README.md).
qt_add_resources(tacetos_ui drawings
    PREFIX /models
    BASE resources/models
    FILES
        resources/models/classic-6g.svg
        resources/models/generic.svg
        resources/models/ipod-1g2g.svg
        resources/models/ipod-3g.svg
        resources/models/ipod-4g.svg
        resources/models/ipod-color.svg
        resources/models/ipod-video.svg
        resources/models/mini-1g.svg
        resources/models/mini-2g.svg
        resources/models/nano-1g.svg
        resources/models/nano-2g.svg
)

qt_add_executable(tacetos
    main.cpp
)
target_link_libraries(tacetos PRIVATE tacetos_ui)
set_target_properties(tacetos PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "TacetOS"
)
