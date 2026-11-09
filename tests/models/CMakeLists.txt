# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_models tst_models.cpp)
target_link_libraries(tst_models PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_models PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_models COMMAND tst_models)
