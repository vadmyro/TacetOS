# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# A device that can fail at the n-th write, for the tests of the writes.
qt_add_library(tacet_installer_testing STATIC
    faultdevice.cpp
    faultdevice.h
)
target_include_directories(tacet_installer_testing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_installer_testing PUBLIC tacet_installer)

qt_add_executable(tst_installer_device tst_installer_device.cpp)
target_link_libraries(tst_installer_device PRIVATE tacet_installer_testing Qt6::Test)

add_test(NAME tst_installer_device COMMAND tst_installer_device)

qt_add_executable(tst_installer_recognise tst_installer_recognise.cpp)
target_link_libraries(tst_installer_recognise PRIVATE
    tacet_installer_testing tacet_fakepod tacet_device Qt6::Test)
target_compile_definitions(tst_installer_recognise PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_installer_recognise COMMAND tst_installer_recognise)

qt_add_executable(tst_installer_firmware tst_installer_firmware.cpp)
target_link_libraries(tst_installer_firmware PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_firmware COMMAND tst_installer_firmware)

qt_add_executable(tst_installer_bootfile tst_installer_bootfile.cpp)
target_link_libraries(tst_installer_bootfile PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_bootfile COMMAND tst_installer_bootfile)

qt_add_executable(tst_installer_checks tst_installer_checks.cpp)
target_link_libraries(tst_installer_checks PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_checks COMMAND tst_installer_checks)

qt_add_executable(tst_installer_write tst_installer_write.cpp)
target_link_libraries(tst_installer_write PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_write COMMAND tst_installer_write)

qt_add_executable(tst_installer_write_nano tst_installer_write_nano.cpp)
target_link_libraries(tst_installer_write_nano PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_write_nano COMMAND tst_installer_write_nano)

qt_add_executable(tst_installer_failure tst_installer_failure.cpp)
target_link_libraries(tst_installer_failure PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_failure COMMAND tst_installer_failure)

qt_add_executable(tst_installer_format tst_installer_format.cpp)
target_link_libraries(tst_installer_format PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_format COMMAND tst_installer_format)

qt_add_executable(tst_installer_convert tst_installer_convert.cpp support.h)
target_link_libraries(tst_installer_convert PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_convert COMMAND tst_installer_convert)

qt_add_executable(tst_installer_rules tst_installer_rules.cpp support.h)
target_link_libraries(tst_installer_rules PRIVATE
    tacet_installer_testing tacet_fakepod Qt6::Test)

add_test(NAME tst_installer_rules COMMAND tst_installer_rules)

qt_add_executable(tst_installer_dfuimage tst_installer_dfuimage.cpp)
target_link_libraries(tst_installer_dfuimage PRIVATE tacet_installer Qt6::Test)

add_test(NAME tst_installer_dfuimage COMMAND tst_installer_dfuimage)
