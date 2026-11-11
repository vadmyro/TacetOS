# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The window code lives in a library so the tests can link against it.
qt_add_library(tacetos_ui STATIC
    mainwindow.cpp
    mainwindow.h
    home/homeaction.h
    home/homescreen.cpp
    home/homescreen.h
    home/ipodcard.cpp
    home/ipodcard.h
    style/appfont.cpp
    style/appfont.h
    style/appicon.cpp
    style/appicon.h
    style/palette.cpp
    style/palette.h
    style/stylecontroller.cpp
    style/stylecontroller.h
)
target_include_directories(tacetos_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tacetos_ui PUBLIC TACETOS_VERSION="${PROJECT_VERSION}")
target_link_libraries(tacetos_ui PUBLIC Qt6::Widgets Qt6::Svg tacet_device tacet_journal)

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

# The list of the models, read as :/data/models.toml: the app never looks in the source tree.
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/models/models.toml" PROPERTIES QT_RESOURCE_ALIAS models.toml)
qt_add_resources(tacetos_ui modellist
    PREFIX /data
    FILES "${PROJECT_SOURCE_DIR}/models/models.toml"
)

# The marks of the logo for the header and the empty state, read as :/logo/<name>.svg. They
# stay in docs/assets/logo; the alias gives them a short name in the resources.
set(TACETOS_LOGO_DIR "${PROJECT_SOURCE_DIR}/docs/assets/logo")
set_source_files_properties(
    "${TACETOS_LOGO_DIR}/mark.svg" PROPERTIES QT_RESOURCE_ALIAS mark.svg)
set_source_files_properties(
    "${TACETOS_LOGO_DIR}/mark-white.svg" PROPERTIES QT_RESOURCE_ALIAS mark-white.svg)
# The icon of the app (window, task bar): the drawing, and a picture as a fallback.
set_source_files_properties(
    "${TACETOS_LOGO_DIR}/icon.svg" PROPERTIES QT_RESOURCE_ALIAS icon.svg)
set_source_files_properties(
    "${TACETOS_LOGO_DIR}/icon-512.png" PROPERTIES QT_RESOURCE_ALIAS icon-512.png)
qt_add_resources(tacetos_ui logo
    PREFIX /logo
    FILES
        "${TACETOS_LOGO_DIR}/mark.svg"
        "${TACETOS_LOGO_DIR}/mark-white.svg"
        "${TACETOS_LOGO_DIR}/icon.svg"
        "${TACETOS_LOGO_DIR}/icon-512.png"
)

# The font of the app (docs/specs/home-screen.md section 8). The file never enters the
# repository: it comes from the system, or from a download of a fixed version when asked to.
option(TACETOS_FETCH_FONT "Download Adwaita Sans at configure time when the system has none" OFF)
set(TACETOS_FONT_NAME AdwaitaSans-Regular.ttf)
find_file(TACETOS_FONT_FILE ${TACETOS_FONT_NAME}
    PATHS
        /usr/share/fonts/adwaita-sans-fonts
        /usr/share/fonts/adwaita-sans
        /usr/share/fonts/truetype/adwaita-sans
        /usr/share/fonts/truetype
        /usr/share/fonts/opentype
        /usr/local/share/fonts
        /Library/Fonts
        "$ENV{HOME}/.local/share/fonts"
        "$ENV{HOME}/Library/Fonts"
    NO_DEFAULT_PATH)
if(NOT TACETOS_FONT_FILE AND TACETOS_FETCH_FONT)
    set(TACETOS_FONT_DOWNLOAD "${CMAKE_BINARY_DIR}/fonts/${TACETOS_FONT_NAME}")
    if(NOT EXISTS "${TACETOS_FONT_DOWNLOAD}")
        file(DOWNLOAD
            https://raw.githubusercontent.com/GNOME/adwaita-fonts/50.0/sans/${TACETOS_FONT_NAME}
            "${TACETOS_FONT_DOWNLOAD}"
            EXPECTED_HASH SHA256=8381c33b9a44f066f2b99dba3d416a2342891e28c956a35dfd8d16ee2987e6d4)
    endif()
    if(EXISTS "${TACETOS_FONT_DOWNLOAD}")
        set(TACETOS_FONT_FILE "${TACETOS_FONT_DOWNLOAD}")
    endif()
endif()
if(TACETOS_FONT_FILE)
    message(STATUS "Font: ${TACETOS_FONT_FILE}")
    get_filename_component(TACETOS_FONT_DIR "${TACETOS_FONT_FILE}" DIRECTORY)
    qt_add_resources(tacetos_ui font
        PREFIX /fonts
        BASE "${TACETOS_FONT_DIR}"
        FILES "${TACETOS_FONT_FILE}"
    )
    target_compile_definitions(tacetos_ui PUBLIC TACETOS_HAS_FONT)
else()
    message(STATUS "Font: ${TACETOS_FONT_NAME} not found, the app will use the font of the system")
endif()

qt_add_executable(tacetos
    main.cpp
)
target_link_libraries(tacetos PRIVATE tacetos_ui)
set_target_properties(tacetos PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "TacetOS"
)

# The launcher entry and the icon for the desktop of Linux. Nothing is installed by the build:
# only `cmake --install` puts them in place.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(GNUInstallDirs)
    install(FILES io.github.vadmyro.TacetOS.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${TACETOS_LOGO_DIR}/icon.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME io.github.vadmyro.TacetOS.svg)
endif()
