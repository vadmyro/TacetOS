# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The helper (docs/specs/helper.md): the program that reads and writes the sectors
# of a disk, and the client the app uses to talk to it. Qt Core only.
# The udev rule file is also built into the helper (section 11.5): one text, two uses.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/70-tacetos-dfu.rules" TACETOS_UDEV_RULE_TEXT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/70-tacetos-dfu.rules")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/udevruletext.h" CONTENT
"#pragma once\ninline constexpr char kUdevRuleText[] = R\"TACETRULE(@TACETOS_UDEV_RULE_TEXT@)TACETRULE\";\n"
    @ONLY)

set(TACETOS_HELPER_CORE_SOURCES
    diskcmds.cpp
    diskcmds.h
    fddevice.cpp
    fddevice.h
    linuxfacts.cpp
    linuxfacts.h
    scsisheet.cpp
    scsisheet.h
    system.cpp
    system.h
    udevrule.cpp
    udevrule.h
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
    writecmds.cpp
    writecmds.h
    killpoint.h
)
qt_add_library(tacet_helper_core STATIC ${TACETOS_HELPER_CORE_SOURCES})
target_include_directories(tacet_helper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(tacet_helper_core PUBLIC Qt6::Core tacet_installer tacet_device)
target_compile_definitions(tacet_helper_core PUBLIC
    TACETOS_VERSION="${PROJECT_VERSION}"
    TACETOS_HELPER_HAS_USB=$<BOOL:${TACETOS_HAVE_LIBUSB}>)

# The program itself, started by the app from the folder of its own executable.
qt_add_executable(tacet-helper main.cpp)
target_link_libraries(tacet-helper PRIVATE tacet_helper_core)

# The same program with the kill points of killpoint.h, for the tests of a cut in
# the middle of an operation. Never installed, never started by the app: the
# normal helper above is built without them and has no trace of them.
if(BUILD_TESTING AND UNIX)
    qt_add_library(tacet_helper_core_killtest STATIC ${TACETOS_HELPER_CORE_SOURCES})
    target_include_directories(tacet_helper_core_killtest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(tacet_helper_core_killtest PUBLIC Qt6::Core tacet_installer tacet_device)
    target_compile_definitions(tacet_helper_core_killtest PUBLIC
        TACETOS_VERSION="${PROJECT_VERSION}"
        TACETOS_HELPER_HAS_USB=$<BOOL:${TACETOS_HAVE_LIBUSB}>
        TACETOS_TEST_KILLPOINTS)
    qt_add_executable(tacet-helper-killtest main.cpp)
    target_link_libraries(tacet-helper-killtest PRIVATE tacet_helper_core_killtest)
endif()

# The client of the app: a QProcess around the program.
qt_add_library(tacet_helper_client STATIC
    crashrecovery.cpp
    crashrecovery.h
    helperclient.cpp
    helperclient.h
)
target_include_directories(tacet_helper_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_helper_client PUBLIC Qt6::Core tacet_helper_core tacet_journal)
