# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# A console tool for development: lists the iPods in DFU mode on the USB bus,
# and sends nothing. Built with the rest, never installed. It needs Linux and
# libusb (see src/installer).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TACETOS_HAVE_LIBUSB)
    qt_add_executable(tacet-dfu
        main.cpp
    )
    target_link_libraries(tacet-dfu PRIVATE tacet_installer_usb)
endif()
