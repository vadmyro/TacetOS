// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>

// The one file of constants for the names that the firmware and the
// bootloader look for on the iPod (docs/specs/detection.md sections 7 and 11).
// These names are written here and nowhere else.

namespace tacetos::device {

// Section 7.1 rule 3: a marker file is read for at most this many bytes.
inline constexpr qint64 kMarkerMaxBytes = 64 * 1024;

// Section 7.2: the files of Apple's software, from the root of the music volume.
inline constexpr QLatin1String kAppleRootFolder("iPod_Control");
inline constexpr QLatin1String kAppleModelFile("iPod_Control/Device/SysInfo");
inline constexpr QLatin1String kAppleDatabase("iPod_Control/iTunes/iTunesDB");

// Section 8.4: the key of the model number string in the Apple model file.
inline constexpr QLatin1String kSysInfoModelKey("ModelNumStr");

// Section 8.4: a variant matches a disk when its size in GB (bytes / 10^9) is
// between these two factors times the capacity on the box.
inline constexpr double kCapacityLow = 0.90;
inline constexpr double kCapacityHigh = 1.05;

// Section 7.6: the key of the Apple model file that holds the build of Apple's
// firmware, as `0x01168000 (1.1.6)`: the version is the part in brackets.
inline constexpr QLatin1String kSysInfoFirmwareKey("visibleBuildID");

// Section 7.6: the only name strings that are ever tested. They are Apple's
// own; the strings of any other firmware are never compared to anything.
inline constexpr QLatin1String kUsbManufacturerApple("Apple Inc.");
inline constexpr QLatin1String kUsbProductApple("iPod");

// Section 7.6: a value of the distribution line may start with this name, which
// is then dropped so that the line does not read "TacetOS TacetOS 0.3".
inline constexpr QLatin1String kDistributionName("TacetOS");

// Section 8.2 rule 5: how long a normal mode reading is remembered for the
// same port, to carry its fields over to the DFU mode that follows.
inline constexpr qint64 kModeMemoryMs = 60 * 1000;

// Section 9.2: how long a new USB device may show no disk before the state
// becomes no-disk.
inline constexpr qint64 kDiskWaitMs = 10 * 1000;

// Section 9.2: once the disk is there and no volume is mounted, how long the
// wait lasts at most, counted from the moment the disk appeared. Then the
// state becomes no-volume (on Linux the system may not mount it by itself).
inline constexpr qint64 kMountGraceMs = 2 * 1000;

// Section 9.4: a device with the same port path that appears within this
// time is the same iPod in another mode. Measured: 8.1 s into DFU mode, 9.6 s
// out of it, 10.25 s for a restart of the firmware.
inline constexpr qint64 kSamePortWindowMs = 15 * 1000;

// Section 9.3: a state that goes down because the volume or the disk went away
// waits this long before it is shown, so that an unplug (volume, disk, then
// USB device, measured up to 1.16 s apart) shows nothing but the removal.
inline constexpr qint64 kDropSettleMs = 1500;

// Section 9.1: the polling rates used where no event source exists.
inline constexpr qint64 kPollUsbMs = 500;
inline constexpr qint64 kPollDisksMs = 1000;

// Section 9.1: the slow reading kept next to the event sources. A source can
// open and still hear nothing (udev in a sandbox), so events are never the
// only way to see a change.
inline constexpr qint64 kPollBackupMs = 2 * 1000;

// Section 2.5 of device-state.md: a finished operation is shown as `done` for
// this long, then dropped.
inline constexpr qint64 kDoneShownMs = 5 * 1000;

// Section 2.7 of device-state.md: a good eject is shown as `released` for this
// long (starting value).
inline constexpr qint64 kReleasedKeepMs = 30 * 1000;

// Section 7.3: the hidden folder of the firmware and the info file inside it.
inline constexpr QLatin1String kFirmwareFolder(".rockbox");
inline constexpr QLatin1String kFirmwareInfoFile("rockbox-info.txt");

// Section 7.3: the key of the info file line that names the distribution and
// its own version. Not named yet (v0.3, with the firmware patches): while it is
// empty, no info file can be taken for a TacetOS one.
inline constexpr QLatin1String kDistributionKey("");

} // namespace tacetos::device
