# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# A console tool for development: prints each iPod that appears, changes or
# goes away. Built with the rest, never installed. It reads the real Linux
# system, so it only exists on Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_executable(tacet-watch
        main.cpp
        watchformat.cpp
    )
    target_include_directories(tacet-watch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tacet-watch PRIVATE tacet_device)
    target_compile_definitions(tacet-watch PRIVATE
        TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")
endif()
