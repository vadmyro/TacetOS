# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The installer of the bootloader, with no user interface and no access to the
# system: it works on a SectorDevice. Tested on its own.
qt_add_library(tacet_installer STATIC
    bootfile.cpp
    bootfile.h
    checks.cpp
    checks.h
    constants.h
    convert.cpp
    convert.h
    dfuaround.cpp
    dfuaround.h
    dfuimage.cpp
    dfuimage.h
    dfuprotocol.cpp
    dfuprotocol.h
    filefault.h
    firmware.cpp
    firmware.h
    format.cpp
    format.h
    operation.cpp
    operation.h
    partitiontable.cpp
    partitiontable.h
    recognise.cpp
    record.cpp
    record.h
    recognise.h
    sectordevice.cpp
    sectordevice.h
    select.cpp
    select.h
)
target_include_directories(tacet_installer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_installer PUBLIC Qt6::Core)
# Check 8 of docs/specs/bootloader-legacy.md section 11: a build that does not
# have this option refuses to write to a real disk. A disk image file is always
# allowed. ON only for the beta.
target_compile_definitions(tacet_installer PRIVATE
    TACETOS_ALLOW_REAL_WRITES=$<BOOL:${TACETOS_ALLOW_REAL_WRITES}>)
# The folder of the generated parts of the Classic DFU image (two fixed blocks and
# two programs for the iPod, docs/specs/bootloader-classic.md section 8). It is
# outside the repository, made by CI. Empty: the build has no Classic install.
set(TACETOS_DFU_PARTS_DIR "$ENV{TACETOS_DFU_PARTS_DIR}" CACHE PATH "Folder of the generated parts of the Classic DFU image")
target_compile_definitions(tacet_installer PRIVATE
    TACETOS_DFU_PARTS_DIR="${TACETOS_DFU_PARTS_DIR}")

# The real USB access to an iPod in DFU mode (section 15.2 of the same spec):
# Linux only, and only when libusb 1.0 is there. Without it the build still
# works and has no Classic install over USB. No test runs this part.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
    endif()
    if(LIBUSB_FOUND)
        qt_add_library(tacet_installer_usb STATIC
            libusblink.cpp
            libusblink.h
        )
        target_link_libraries(tacet_installer_usb PUBLIC tacet_installer PkgConfig::LIBUSB)
        set(TACETOS_HAVE_LIBUSB ON CACHE INTERNAL "libusb found")
    else()
        message(STATUS "libusb not found: no USB access for the Classic install")
        set(TACETOS_HAVE_LIBUSB OFF CACHE INTERNAL "libusb found")
    endif()
endif()
