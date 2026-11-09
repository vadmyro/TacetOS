# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# A console tool for development: mounts, unmounts or ejects the plugged iPod
# through the disk service. Built with the rest, never installed. It needs
# Linux and the D-Bus part of Qt (see src/device).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TACETOS_HAVE_DBUS)
    qt_add_executable(tacet-disk
        main.cpp
    )
    target_link_libraries(tacet-disk PRIVATE tacet_device)
    target_compile_definitions(tacet-disk PRIVATE
        TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")
endif()
