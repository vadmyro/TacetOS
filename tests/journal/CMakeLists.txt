# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_journal tst_journal.cpp)
target_link_libraries(tst_journal PRIVATE tacet_journal Qt6::Test)

add_test(NAME tst_journal COMMAND tst_journal)
