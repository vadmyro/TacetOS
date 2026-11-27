# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

set(TACETOS_HEALTH_FIXTURES "${PROJECT_SOURCE_DIR}/tests/fixtures/health")

qt_add_executable(tst_health_parsing tst_health_parsing.cpp)
target_link_libraries(tst_health_parsing PRIVATE tacet_health Qt6::Test)
target_compile_definitions(tst_health_parsing PRIVATE
    TACETOS_HEALTH_FIXTURES="${TACETOS_HEALTH_FIXTURES}")
add_test(NAME tst_health_parsing COMMAND tst_health_parsing)

qt_add_executable(tst_health_blocks tst_health_blocks.cpp blockbuilder.h)
target_link_libraries(tst_health_blocks PRIVATE tacet_health Qt6::Test)
add_test(NAME tst_health_blocks COMMAND tst_health_blocks)

qt_add_executable(tst_health_disk tst_health_disk.cpp blockbuilder.h)
target_link_libraries(tst_health_disk PRIVATE tacet_health Qt6::Test)
target_compile_definitions(tst_health_disk PRIVATE
    TACETOS_HEALTH_FIXTURES="${TACETOS_HEALTH_FIXTURES}")
add_test(NAME tst_health_disk COMMAND tst_health_disk)

qt_add_executable(tst_health_battery tst_health_battery.cpp healthbuilder.h blockbuilder.h)
target_link_libraries(tst_health_battery PRIVATE tacet_health Qt6::Test)
add_test(NAME tst_health_battery COMMAND tst_health_battery)

qt_add_executable(tst_health_history tst_health_history.cpp healthbuilder.h blockbuilder.h)
target_link_libraries(tst_health_history PRIVATE tacet_health Qt6::Test)
add_test(NAME tst_health_history COMMAND tst_health_history)

# The fixtures are the output of their script, kept in the tree.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME health_fixtures
        COMMAND ${Python3_EXECUTABLE} ${TACETOS_HEALTH_FIXTURES}/make_health_fixtures.py
            --check ${TACETOS_HEALTH_FIXTURES})
endif()
