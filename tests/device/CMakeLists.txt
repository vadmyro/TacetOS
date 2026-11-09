# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

qt_add_executable(tst_device_models tst_device_models.cpp)
target_link_libraries(tst_device_models PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_models PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_device_models COMMAND tst_device_models)

qt_add_executable(tst_device_usb tst_device_usb.cpp)
target_link_libraries(tst_device_usb PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_usb PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml"
    TACETOS_USB_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME tst_device_usb COMMAND tst_device_usb)

qt_add_executable(tst_device_disks tst_device_disks.cpp)
target_link_libraries(tst_device_disks PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_disks PRIVATE
    TACETOS_DEVICE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME tst_device_disks COMMAND tst_device_disks)

qt_add_executable(tst_device_markers tst_device_markers.cpp)
target_link_libraries(tst_device_markers PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_markers PRIVATE
    TACETOS_DEVICE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME tst_device_markers COMMAND tst_device_markers)

qt_add_executable(tst_device_merge tst_device_merge.cpp)
target_link_libraries(tst_device_merge PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_merge PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_device_merge COMMAND tst_device_merge)

qt_add_executable(tst_device_live tst_device_live.cpp)
target_link_libraries(tst_device_live PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_live PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_device_live COMMAND tst_device_live)

qt_add_executable(tst_device_livedriver tst_device_livedriver.cpp)
target_link_libraries(tst_device_livedriver PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_livedriver PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml")

add_test(NAME tst_device_livedriver COMMAND tst_device_livedriver)

qt_add_executable(tst_device_udisks tst_device_udisks.cpp)
target_link_libraries(tst_device_udisks PRIVATE tacet_device Qt6::Test)

add_test(NAME tst_device_udisks COMMAND tst_device_udisks)

# The Linux source only exists on Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_executable(tst_device_linuxsource tst_device_linuxsource.cpp)
    target_link_libraries(tst_device_linuxsource PRIVATE tacet_device Qt6::Test)
    target_compile_definitions(tst_device_linuxsource PRIVATE
        TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml"
        TACETOS_DEVICE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

    add_test(NAME tst_device_linuxsource COMMAND tst_device_linuxsource)

    qt_add_executable(tst_device_mountwatcher tst_device_mountwatcher.cpp)
    target_link_libraries(tst_device_mountwatcher PRIVATE tacet_device Qt6::Test)
    target_compile_definitions(tst_device_mountwatcher PRIVATE
        TACETOS_DEVICE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

    add_test(NAME tst_device_mountwatcher COMMAND tst_device_mountwatcher)
endif()

# The udev monitor needs libudev, found by src/device.
if(TACETOS_HAVE_UDEV)
    qt_add_executable(tst_device_udevmonitor tst_device_udevmonitor.cpp)
    target_link_libraries(tst_device_udevmonitor PRIVATE tacet_device Qt6::Test)

    add_test(NAME tst_device_udevmonitor COMMAND tst_device_udevmonitor)
endif()

# The D-Bus side of the mount and eject code needs the Qt D-Bus part.
if(TACETOS_HAVE_DBUS)
    qt_add_executable(tst_device_udisksdbus tst_device_udisksdbus.cpp)
    target_link_libraries(tst_device_udisksdbus PRIVATE tacet_device Qt6::Test)

    add_test(NAME tst_device_udisksdbus COMMAND tst_device_udisksdbus)
endif()

qt_add_executable(tst_device_variant tst_device_variant.cpp)
target_link_libraries(tst_device_variant PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_variant PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml"
    TACETOS_DEVICE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME tst_device_variant COMMAND tst_device_variant)

qt_add_executable(tst_device_firmware tst_device_firmware.cpp)
target_link_libraries(tst_device_firmware PRIVATE tacet_device Qt6::Test)
target_compile_definitions(tst_device_firmware PRIVATE
    TACETOS_MODELS_TOML="${PROJECT_SOURCE_DIR}/models/models.toml"
    TACETOS_DEVICE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME tst_device_firmware COMMAND tst_device_firmware)
