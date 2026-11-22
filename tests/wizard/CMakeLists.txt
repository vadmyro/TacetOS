# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_wizard_flow tst_wizard_flow.cpp)
target_link_libraries(tst_wizard_flow PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_flow COMMAND tst_wizard_flow)

qt_add_executable(tst_wizard_runner tst_wizard_runner.cpp)
target_link_libraries(tst_wizard_runner PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_runner COMMAND tst_wizard_runner)
# Alone: it waits 5 seconds at most for small files, and the disk image tests that run
# at the same time can hold the disk longer than that.
set_tests_properties(tst_wizard_runner PROPERTIES RUN_SERIAL TRUE)

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
if(UNIX)
    target_compile_definitions(tst_wizard_bootloader PRIVATE
        TACETOS_HELPER_KILLTEST_PROGRAM="$<TARGET_FILE:tacet-helper-killtest>")
    add_dependencies(tst_wizard_bootloader tacet-helper-killtest)
endif()
add_test(NAME tst_wizard_bootloader COMMAND tst_wizard_bootloader)

# The bootloader of the iPod Classic (section 9): the runner, `classic-dfu` run
# in this process on the fake system layer and the fake iPod in DFU mode of the
# installer tests, and recorded detection events.
qt_add_executable(tst_wizard_classicdfu tst_wizard_classicdfu.cpp testpackage.h testzip.h)
target_link_libraries(tst_wizard_classicdfu PRIVATE
    tacet_wizard tacet_helper_core tacet_installer_testing Qt6::Test)
target_include_directories(tst_wizard_classicdfu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../device)
add_test(NAME tst_wizard_classicdfu COMMAND tst_wizard_classicdfu)

# The uninstall (section 10): older iPods on images with the helper program,
# the Classic with `classic-dfu` run in this process on the fake system layer.
qt_add_executable(tst_wizard_uninstall tst_wizard_uninstall.cpp)
target_link_libraries(tst_wizard_uninstall PRIVATE
    tacet_wizard tacet_fakepod tacet_helper_core tacet_installer_testing Qt6::Test)
target_include_directories(tst_wizard_uninstall PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../device)
target_compile_definitions(tst_wizard_uninstall PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_wizard_uninstall tacet-helper)
if(UNIX)
    target_compile_definitions(tst_wizard_uninstall PRIVATE
        TACETOS_HELPER_KILLTEST_PROGRAM="$<TARGET_FILE:tacet-helper-killtest>")
    add_dependencies(tst_wizard_uninstall tacet-helper-killtest)
endif()
add_test(NAME tst_wizard_uninstall COMMAND tst_wizard_uninstall)

# The end of a flow (section 11): `verify` and `finish` on a folder that plays
# the volume, with a package made by the test.
qt_add_executable(tst_wizard_verify tst_wizard_verify.cpp testpackage.h testzip.h)
target_link_libraries(tst_wizard_verify PRIVATE tacet_wizard Qt6::Test)
add_test(NAME tst_wizard_verify COMMAND tst_wizard_verify)

# The real sources of Linux and the factory of steps: the disk service answers
# from memory, the helper is not started.
qt_add_executable(tst_wizard_sources tst_wizard_sources.cpp)
target_link_libraries(tst_wizard_sources PRIVATE tacet_wizard Qt6::Test)
target_compile_definitions(tst_wizard_sources PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_wizard_sources tacet-helper)
add_test(NAME tst_wizard_sources COMMAND tst_wizard_sources)

# The whole journey through the real factory of steps (section 2 to 10): older
# iPods on images with the helper program, the Classic with `classic-dfu` run in
# this process on the fake system layer, the disk service answering from memory.
qt_add_executable(tst_wizard_e2e tst_wizard_e2e.cpp testpackage.h testzip.h)
target_link_libraries(tst_wizard_e2e PRIVATE
    tacet_wizard tacet_fakepod tacet_helper_core tacet_installer_testing Qt6::Test)
target_include_directories(tst_wizard_e2e PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../device)
target_compile_definitions(tst_wizard_e2e PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_wizard_e2e tacet-helper)
if(UNIX)
    target_compile_definitions(tst_wizard_e2e PRIVATE
        TACETOS_HELPER_KILLTEST_PROGRAM="$<TARGET_FILE:tacet-helper-killtest>")
    add_dependencies(tst_wizard_e2e tacet-helper-killtest)
endif()
add_test(NAME tst_wizard_e2e COMMAND tst_wizard_e2e)
