// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "diskcmds.h"
#include "writecmds.h"

#include <QJsonObject>
#include <memory>

// The two commands of the iPod Classic (docs/specs/helper.md sections 6.9 and
// 6.10): the transfer of the install or uninstall image in DFU mode, and the
// resume of the programs a crash left paused. The work is `runClassicDfu` of
// `tacet_installer`; this file checks the request, builds the image, opens the
// gate and gives the flow the layer of the system.

namespace tacetos::helper {

// Section 6.9 on a real disk that passed step 4 of section 7.1.
CommandOutcome runClassicDfuOnDisk(const QJsonObject &args, SystemLayer &system,
                                   const RealDisk &disk, CommandHooks &hooks);

// Section 6.10.
CommandOutcome runResumePaused(const QJsonObject &args, SystemLayer &system);

// The layer of Linux: udisks2 for the eject, libusb for the bus. Nothing is ever
// paused on Linux. Null in a build without libusb.
std::unique_ptr<installer::ClassicSystemLayer> makeLinuxClassicLayer(SystemLayer &system,
                                                                     const QString &blockPath);

} // namespace tacetos::helper
