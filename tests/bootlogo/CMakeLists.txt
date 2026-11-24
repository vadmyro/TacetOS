# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_bootlogo tst_bootlogo.cpp)
target_link_libraries(tst_bootlogo PRIVATE tacet_bootlogo Qt6::Test Qt6::Gui)
add_test(NAME tst_bootlogo COMMAND tst_bootlogo)
