# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The helper (docs/specs/helper.md): the program that reads and writes the sectors
# of a disk, and the client the app uses to talk to it. Qt Core only.
qt_add_library(tacet_helper_core STATIC
    commands.cpp
    commands.h
    helperconstants.h
    gate.cpp
    gate.h
    lock.cpp
    lock.h
    protocol.cpp
    protocol.h
    readstate.cpp
    readstate.h
    server.cpp
    server.h
    target.cpp
    target.h
)
target_include_directories(tacet_helper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_helper_core PUBLIC Qt6::Core tacet_installer)
target_compile_definitions(tacet_helper_core PUBLIC
    TACETOS_VERSION="${PROJECT_VERSION}"
    TACETOS_HELPER_HAS_USB=$<BOOL:${TACETOS_HAVE_LIBUSB}>)

# The program itself, started by the app from the folder of its own executable.
qt_add_executable(tacet-helper main.cpp)
target_link_libraries(tacet-helper PRIVATE tacet_helper_core)

# The client of the app: a QProcess around the program.
qt_add_library(tacet_helper_client STATIC
    helperclient.cpp
    helperclient.h
)
target_include_directories(tacet_helper_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_helper_client PUBLIC Qt6::Core tacet_helper_core tacet_journal)
