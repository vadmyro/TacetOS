# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_archive tst_archive.cpp)
target_link_libraries(tst_archive PRIVATE tacet_archive Qt6::Test)
add_test(NAME tst_archive COMMAND tst_archive)
