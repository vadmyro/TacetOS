# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The journal of what the app saw and did, with no user interface. Qt Core only.
qt_add_library(tacet_journal STATIC
    entry.cpp
    entry.h
    journal.cpp
    journal.h
    journalconstants.h
    journalfile.cpp
    journalfile.h
)
target_include_directories(tacet_journal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_journal PUBLIC Qt6::Core)
