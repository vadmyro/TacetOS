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

// Section 8.2 rule 5: how long a normal mode reading is remembered for the
// same port, to carry its fields over to the DFU mode that follows.
inline constexpr qint64 kModeMemoryMs = 60 * 1000;

// Section 9.2: how long a new USB device may show no disk before the state
// becomes no-disk.
inline constexpr qint64 kDiskWaitMs = 10 * 1000;

// Section 9.4: a device with the same port path that appears within this
// time is the same iPod in another mode.
inline constexpr qint64 kSamePortWindowMs = 3 * 1000;

// Section 9.1: the polling rates used where no event source exists.
inline constexpr qint64 kPollUsbMs = 500;
inline constexpr qint64 kPollDisksMs = 1000;

// Section 9.1: the slow reading kept next to the event sources. A source can
// open and still hear nothing (udev in a sandbox), so events are never the
// only way to see a change.
inline constexpr qint64 kPollBackupMs = 2 * 1000;

// Section 7.3: the hidden folder of the firmware and the info file inside it.
inline constexpr QLatin1String kFirmwareFolder(".rockbox");
inline constexpr QLatin1String kFirmwareInfoFile("rockbox-info.txt");

// Section 7.3: the key of the info file line that names the distribution and
// its own version. Not named yet (v0.3, with the firmware patches): while it is
// empty, no info file can be taken for a TacetOS one.
inline constexpr QLatin1String kDistributionKey("");

} // namespace tacetos::device
