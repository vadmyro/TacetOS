# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_wizard_flow tst_wizard_flow.cpp)
target_link_libraries(tst_wizard_flow PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_flow COMMAND tst_wizard_flow)

qt_add_executable(tst_wizard_runner tst_wizard_runner.cpp)
target_link_libraries(tst_wizard_runner PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_runner COMMAND tst_wizard_runner)

qt_add_executable(tst_wizard_diagnose tst_wizard_diagnose.cpp)
target_link_libraries(tst_wizard_diagnose PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_diagnose COMMAND tst_wizard_diagnose)

# The backups and their restore (section 5): temporary folders, files of the
# fixtures, and the helper program on an image file from end to end.
qt_add_executable(tst_wizard_backup tst_wizard_backup.cpp)
target_link_libraries(tst_wizard_backup PRIVATE tacet_wizard tacet_fakepod Qt6::Test)
target_compile_definitions(tst_wizard_backup PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>"
    TACETOS_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
add_dependencies(tst_wizard_backup tacet-helper)
if(UNIX)
    target_compile_definitions(tst_wizard_backup PRIVATE
        TACETOS_HELPER_KILLTEST_PROGRAM="$<TARGET_FILE:tacet-helper-killtest>")
    add_dependencies(tst_wizard_backup tacet-helper-killtest)
endif()
add_test(NAME tst_wizard_backup COMMAND tst_wizard_backup)

# The format and the conversion (section 6): the runner, the helper program on
# image files, and recorded detection events.
qt_add_executable(tst_wizard_format tst_wizard_format.cpp)
target_link_libraries(tst_wizard_format PRIVATE tacet_wizard tacet_fakepod Qt6::Test)
target_compile_definitions(tst_wizard_format PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_wizard_format tacet-helper)
if(UNIX)
    target_compile_definitions(tst_wizard_format PRIVATE
        TACETOS_HELPER_KILLTEST_PROGRAM="$<TARGET_FILE:tacet-helper-killtest>")
    add_dependencies(tst_wizard_format tacet-helper-killtest)
endif()
add_test(NAME tst_wizard_format COMMAND tst_wizard_format)

qt_add_executable(tst_wizard_package tst_wizard_package.cpp testzip.h)
target_link_libraries(tst_wizard_package PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_package COMMAND tst_wizard_package)

qt_add_executable(tst_wizard_firmware tst_wizard_firmware.cpp testzip.h)
target_link_libraries(tst_wizard_firmware PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_firmware COMMAND tst_wizard_firmware)

# The bootloader of the older iPods (section 8): the runner, the helper program
# on image files of every family.
qt_add_executable(tst_wizard_bootloader tst_wizard_bootloader.cpp testpackage.h testzip.h)
target_link_libraries(tst_wizard_bootloader PRIVATE tacet_wizard tacet_fakepod Qt6::Test)
target_compile_definitions(tst_wizard_bootloader PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_wizard_bootloader tacet-helper)
add_test(NAME tst_wizard_bootloader COMMAND tst_wizard_bootloader)
