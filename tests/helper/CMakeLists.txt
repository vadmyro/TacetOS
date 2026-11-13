# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The helper program is started as the app starts it, on fake iPod images only.
qt_add_executable(tst_helper tst_helper.cpp)
target_link_libraries(tst_helper PRIVATE tacet_helper_client tacet_fakepod Qt6::Test)
target_compile_definitions(tst_helper PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_helper tacet-helper)

add_test(NAME tst_helper COMMAND tst_helper)

# The commands of the older iPods, from the client to the program to an image file.
qt_add_executable(tst_helper_commands tst_helper_commands.cpp)
target_link_libraries(tst_helper_commands PRIVATE tacet_helper_client tacet_fakepod Qt6::Test)
target_compile_definitions(tst_helper_commands PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_helper_commands tacet-helper)

add_test(NAME tst_helper_commands COMMAND tst_helper_commands)

# The Linux layer: recorded answers, a copy of the device tree, a fake runner for
# pkexec. Never udisks2, never pkexec.
qt_add_executable(tst_helper_linux tst_helper_linux.cpp)
target_link_libraries(tst_helper_linux PRIVATE tacet_helper_core Qt6::Test)
target_include_directories(tst_helper_linux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../device)
target_compile_definitions(tst_helper_linux PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml"
    TACETOS_UDEV_RULE_FILE="${PROJECT_SOURCE_DIR}/src/helper/70-tacetos-dfu.rules")

add_test(NAME tst_helper_linux COMMAND tst_helper_linux)

# The refusals of the write commands on real disks (docs/specs/helper.md sections 7
# and 15): the loop of the helper in this process, recorded answers of udisks2, a
# copy of the device tree, image files for the disks. Descriptors, so not Windows.
if(UNIX)
    qt_add_executable(tst_helper_refusals tst_helper_refusals.cpp)
    target_link_libraries(tst_helper_refusals PRIVATE tacet_helper_core tacet_fakepod Qt6::Test)
    target_include_directories(tst_helper_refusals PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../device)
    add_test(NAME tst_helper_refusals COMMAND tst_helper_refusals)
endif()

# A cut in the middle of each write command: the program with the kill points
# (docs/specs/helper.md section 15.2), SIGKILL, image files only.
if(UNIX)
    qt_add_executable(tst_helper_killpoints tst_helper_killpoints.cpp)
    target_link_libraries(tst_helper_killpoints PRIVATE tacet_helper_client tacet_fakepod Qt6::Test)
    target_compile_definitions(tst_helper_killpoints PRIVATE
        TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>"
        TACETOS_HELPER_KILLTEST_PROGRAM="$<TARGET_FILE:tacet-helper-killtest>")
    add_dependencies(tst_helper_killpoints tacet-helper tacet-helper-killtest)
    add_test(NAME tst_helper_killpoints COMMAND tst_helper_killpoints)
    set_tests_properties(tst_helper_killpoints PROPERTIES TIMEOUT 900)
endif()

# A full data folder (backup, install record, no room in the partition): the test
# program of the helper answers "no space left" in the folder (section 15.2).
if(UNIX)
    qt_add_executable(tst_helper_diskfull tst_helper_diskfull.cpp)
    target_link_libraries(tst_helper_diskfull PRIVATE tacet_helper_client tacet_fakepod Qt6::Test)
    target_compile_definitions(tst_helper_diskfull PRIVATE
        TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>"
        TACETOS_HELPER_KILLTEST_PROGRAM="$<TARGET_FILE:tacet-helper-killtest>")
    add_dependencies(tst_helper_diskfull tacet-helper tacet-helper-killtest)
    add_test(NAME tst_helper_diskfull COMMAND tst_helper_diskfull)
    set_tests_properties(tst_helper_diskfull PROPERTIES TIMEOUT 300)
endif()
