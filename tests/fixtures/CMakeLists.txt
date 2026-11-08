# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# Replays the fake library generator and checks it against the recorded
# reference. The library code is compared to the same reference later.
find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    add_test(NAME fixtures_reference
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_reference.py
            --reference-dir ${CMAKE_CURRENT_SOURCE_DIR})
    # The script exits with 77 when ffmpeg is missing: CTest reports "skipped".
    set_tests_properties(fixtures_reference PROPERTIES SKIP_RETURN_CODE 77)
else()
    message(STATUS "python3 not found: the fixtures test is not built")
endif()
