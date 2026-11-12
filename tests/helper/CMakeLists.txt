# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The helper program is started as the app starts it, on fake iPod images only.
qt_add_executable(tst_helper tst_helper.cpp)
target_link_libraries(tst_helper PRIVATE tacet_helper_client tacet_fakepod Qt6::Test)
target_compile_definitions(tst_helper PRIVATE
    TACETOS_HELPER_PROGRAM="$<TARGET_FILE:tacet-helper>")
add_dependencies(tst_helper tacet-helper)

add_test(NAME tst_helper COMMAND tst_helper)
