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

# The palettes and the style controller.
qt_add_executable(tst_style tst_style.cpp)
target_link_libraries(tst_style PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_style COMMAND tst_style)
set_tests_properties(tst_style PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The home screen and the card of an iPod, with entries written by hand.
qt_add_executable(tst_home tst_home.cpp)
target_link_libraries(tst_home PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_home COMMAND tst_home)
set_tests_properties(tst_home PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The home screen wired to the device code, with a fake source, clock and disk actions.
qt_add_executable(tst_home_wiring tst_home_wiring.cpp)
target_link_libraries(tst_home_wiring PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_home_wiring COMMAND tst_home_wiring)
set_tests_properties(tst_home_wiring PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The window of the install wizard, with a runner whose steps are made by the test.
qt_add_executable(tst_wizardwindow tst_wizardwindow.cpp)
target_link_libraries(tst_wizardwindow PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_wizardwindow COMMAND tst_wizardwindow)
set_tests_properties(tst_wizardwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The page of the checks, with a runner whose step reads fake sources.
qt_add_executable(tst_diagnosepage tst_diagnosepage.cpp)
target_link_libraries(tst_diagnosepage PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_diagnosepage COMMAND tst_diagnosepage)
set_tests_properties(tst_diagnosepage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The pages of the backup and of the restore, with a runner whose steps are made by the test.
qt_add_executable(tst_backuppage tst_backuppage.cpp)
target_link_libraries(tst_backuppage PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_backuppage COMMAND tst_backuppage)
set_tests_properties(tst_backuppage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The page of the erase, with a runner whose steps are made by the test.
qt_add_executable(tst_formatpage tst_formatpage.cpp)
target_link_libraries(tst_formatpage PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_formatpage COMMAND tst_formatpage)
set_tests_properties(tst_formatpage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The DFU guide, with a runner whose step plays recorded events and a fake clock.
qt_add_executable(tst_dfututorial tst_dfututorial.cpp)
target_link_libraries(tst_dfututorial PRIVATE tacetos_ui Qt6::Test)

add_test(NAME tst_dfututorial COMMAND tst_dfututorial)
set_tests_properties(tst_dfututorial PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
