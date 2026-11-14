# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

find_package(Qt6 REQUIRED COMPONENTS Test)

# QSKIP("why") leaves out the optional second argument of the Qt macro, which recent
# Clang reports with -Wpedantic before C++20 (and the CI makes warnings errors).
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wvariadic-macro-arguments-omitted TACETOS_HAS_VARIADIC_MACRO_WARNING)
if(TACETOS_HAS_VARIADIC_MACRO_WARNING)
    add_compile_options(-Wno-variadic-macro-arguments-omitted)
endif()

add_subdirectory(app)
add_subdirectory(device)
add_subdirectory(fakepod)
add_subdirectory(journal)
add_subdirectory(models)
add_subdirectory(fixtures)
add_subdirectory(helper)
add_subdirectory(installer)
add_subdirectory(reference)
