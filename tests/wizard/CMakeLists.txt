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
