// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>

// The constants of the install wizard (docs/specs/install-wizard.md section 14).

namespace tacetos::wizard {

// Section 3.2: the version of the progress file.
inline constexpr int kProgressFormat = 1;

// Section 3.1: the folder of the progress files, in the data folder.
inline constexpr QLatin1String kProgressFolder("wizard");

// Section 3.4: a refused progress file is renamed with this suffix.
inline constexpr QLatin1String kRejectedSuffix(".rejected");

// Section 3.5: the lock of a flow, next to its progress file.
inline constexpr QLatin1String kLockSuffix(".lock");

// Section 4.2: the oldest udisks2 the helper works with (helper.md section 14).
inline constexpr QLatin1String kOldestUdisks("2.7.3");

// Section 4.2: the file of the DFU udev rule (helper.md section 14).
inline constexpr QLatin1String kUdevRuleFile("/etc/udev/rules.d/70-tacetos-dfu.rules");

// Section 4.3, check `apple-firmware`: the versions of Apple's firmware the
// bootloader of the Classic works with, by capacity (bootloader-classic.md
// section 3). The 160 GB was sold in three revisions.
struct ClassicAppleFirmware
{
    int capacityGb;
    const char *version;
};
inline constexpr ClassicAppleFirmware kClassicAppleFirmware[] = {
    {80, "1.1.2"}, {160, "1.1.2"}, {120, "2.0.1"}, {160, "2.0.4"}, {160, "2.0.5"},
};

} // namespace tacetos::wizard
