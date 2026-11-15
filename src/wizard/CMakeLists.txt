# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The logic of the install wizard (docs/specs/install-wizard.md), with no user
# interface. Qt Core only.
qt_add_library(tacet_wizard STATIC
    backup.cpp
    backup.h
    diagnose.cpp
    diagnose.h
    flow.cpp
    flow.h
    progress.cpp
    progress.h
    runner.cpp
    runner.h
    step.h
    wizardconstants.h
    wizarderrors.h
)
target_include_directories(tacet_wizard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_wizard PUBLIC Qt6::Core tacet_device tacet_installer tacet_helper_client
    tacet_journal)
