# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# Detection of the iPod, with no user interface. Tested on its own.
qt_add_library(tacet_device STATIC
    constants.h
    detected.h
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
)
target_include_directories(tacet_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_device PUBLIC Qt6::Core)

# The reader of a real Linux system: Linux only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tacet_device PRIVATE
        linuxsource.cpp
        linuxsource.h
    )
endif()
