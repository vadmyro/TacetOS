# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_update_version tst_update_version.cpp)
target_link_libraries(tst_update_version PRIVATE tacet_update Qt6::Test)
add_test(NAME tst_update_version COMMAND tst_update_version)

qt_add_executable(tst_update_index tst_update_index.cpp)
target_link_libraries(tst_update_index PRIVATE tacet_update Qt6::Test)
add_test(NAME tst_update_index COMMAND tst_update_index)

# A fake HTTP server on 127.0.0.1, started by the test: never the real channel.
qt_add_executable(tst_update_channel tst_update_channel.cpp)
target_link_libraries(tst_update_channel PRIVATE tacet_update Qt6::Test)
add_test(NAME tst_update_channel COMMAND tst_update_channel)
