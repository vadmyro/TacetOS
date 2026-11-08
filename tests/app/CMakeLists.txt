# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_mainwindow tst_mainwindow.cpp)
target_link_libraries(tst_mainwindow PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_mainwindow COMMAND tst_mainwindow)
# No display needed: the tests also run on CI machines without a screen.
set_tests_properties(tst_mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
