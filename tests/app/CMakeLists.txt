# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_mainwindow tst_mainwindow.cpp)
target_link_libraries(tst_mainwindow PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_mainwindow COMMAND tst_mainwindow)
# No display needed: the tests also run on CI machines without a screen.
set_tests_properties(tst_mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The drawings are checked against the model list, read from the source tree.
qt_add_executable(tst_drawings tst_drawings.cpp)
target_link_libraries(tst_drawings PRIVATE tacetos_ui tacet_device Qt6::Svg Qt6::Test)
target_compile_definitions(tst_drawings PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_drawings COMMAND tst_drawings)
