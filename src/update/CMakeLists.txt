# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

# The update channel (docs/specs/update-channel.md): versions, the index, the
# address and the reads. Qt Core and Qt Network only.
qt_add_library(tacet_update STATIC
    address.cpp
    address.h
    check.cpp
    check.h
    index.cpp
    index.h
    reader.cpp
    reader.h
    version.cpp
    version.h
)
target_include_directories(tacet_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tacet_update PUBLIC Qt6::Core Qt6::Network tacet_device tacet_journal)

# Section 4: the packaged builds of the beta always read the default address.
if(TACETOS_PACKAGE_BUILD)
    target_compile_definitions(tacet_update PRIVATE TACETOS_PACKAGE_BUILD=1)
endif()
