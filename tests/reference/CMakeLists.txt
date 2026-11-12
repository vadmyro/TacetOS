# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# Tests that run a reference tool beside our own code. The tool is built outside
# the tree by tests/reference/build-reference-tools.sh. Without
# TACETOS_REFERENCE_TOOLS_DIR the tests are skipped, never failed.
set(TACETOS_REFERENCE_TOOLS_DIR "$ENV{TACETOS_REFERENCE_TOOLS_DIR}" CACHE PATH
    "Directory that holds the reference tools (partition-tool). Empty: the comparison tests are skipped.")

qt_add_executable(tst_reference_partition tst_reference_partition.cpp)
target_link_libraries(tst_reference_partition PRIVATE tacet_fakepod Qt6::Test)
target_compile_definitions(tst_reference_partition PRIVATE
    TACETOS_REFERENCE_TOOLS_DIR="${TACETOS_REFERENCE_TOOLS_DIR}")

add_test(NAME tst_reference_partition COMMAND tst_reference_partition)

qt_add_executable(tst_installer_reference tst_installer_reference.cpp)
target_link_libraries(tst_installer_reference PRIVATE tacet_installer tacet_fakepod Qt6::Test)
target_compile_definitions(tst_installer_reference PRIVATE
    TACETOS_REFERENCE_TOOLS_DIR="${TACETOS_REFERENCE_TOOLS_DIR}")

add_test(NAME tst_installer_reference COMMAND tst_installer_reference)

# The generated parts of the Classic DFU image and the reference tool that builds
# images from the same programs (tests/reference/build-dfu-parts.sh). The same
# variable as src/installer. Without it the Classic comparison is skipped.
qt_add_executable(tst_dfu_reference tst_dfu_reference.cpp)
target_link_libraries(tst_dfu_reference PRIVATE tacet_installer Qt6::Test)
target_compile_definitions(tst_dfu_reference PRIVATE
    TACETOS_DFU_PARTS_DIR="${TACETOS_DFU_PARTS_DIR}")

add_test(NAME tst_dfu_reference COMMAND tst_dfu_reference)
