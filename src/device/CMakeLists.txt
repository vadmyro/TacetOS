# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# Detection of the iPod, with no user interface. Tested on its own.
qt_add_library(tacet_device STATIC
    constants.h
    detected.h
    firmware.cpp
    firmware.h
    linuxdisks.cpp
    linuxdisks.h
    live.cpp
    live.h
    livedriver.cpp
    livedriver.h
    markers.cpp
    markers.h
    merge.cpp
    merge.h
    messages.cpp
    messages.h
    models.cpp
    models.h
    toml.cpp
    toml.h
    volume.cpp
    volume.h
    usblist.cpp
    usblist.h
    udisks.cpp
    udisks.h
    variant.cpp
    variant.h
)
target_include_directories(tacet_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_device PUBLIC Qt6::Core)

# The reader of a real Linux system: Linux only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tacet_device PRIVATE
        linuxsource.cpp
        linuxsource.h
        mountwatcher.cpp
        mountwatcher.h
    )
endif()

# The udev monitor (section 9.1 of the detection spec): Linux only, and only
# when libudev is there. Without it the build still works and the driver polls.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUDEV QUIET IMPORTED_TARGET libudev)
    endif()
    if(LIBUDEV_FOUND)
        target_sources(tacet_device PRIVATE
            udevmonitor.cpp
            udevmonitor.h
        )
        target_link_libraries(tacet_device PUBLIC PkgConfig::LIBUDEV)
        target_compile_definitions(tacet_device PUBLIC TACETOS_HAVE_UDEV=1)
        # For the tests, which are in another folder.
        set(TACETOS_HAVE_UDEV ON CACHE INTERNAL "libudev found")
    else()
        message(STATUS "libudev not found: the device list will use polling")
        set(TACETOS_HAVE_UDEV OFF CACHE INTERNAL "libudev found")
    endif()
endif()

# Mount, unmount and eject through the disk service (udisks2): Linux only, and
# only when Qt has its D-Bus part. The logic in udisks.cpp does not need D-Bus.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.8 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        target_sources(tacet_device PRIVATE
            udisksbackend.cpp
            udisksbackend.h
        )
        target_link_libraries(tacet_device PUBLIC Qt6::DBus)
        target_compile_definitions(tacet_device PUBLIC TACETOS_HAVE_DBUS=1)
        # For the tests, which are in another folder.
        set(TACETOS_HAVE_DBUS ON CACHE INTERNAL "Qt DBus found")
    else()
        message(STATUS "Qt DBus not found: no mount or eject on Linux")
        set(TACETOS_HAVE_DBUS OFF CACHE INTERNAL "Qt DBus found")
    endif()
endif()
