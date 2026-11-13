// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "readstate.h"
#include "system.h"

#include <QJsonObject>
#include <memory>

// The commands on a real disk and the one of the system (docs/specs/helper.md
// sections 6.2, 6.3, 6.11 and 11): the disk is found again and judged, opened by
// udisks2 for reading only, and read.

namespace tacetos::helper {

// What step 4 of section 7.1 gives for a real disk.
struct RealDisk
{
    bool ok = false;
    QString code;
    QString detail;
    // What the lock of section 8 is taken on.
    QString lockKey;
    QString blockPath;
    qint64 sizeBytes = 0;
    int usbBus = -1;
    int usbDevice = -1;
};

// `target` has passed `checkTargetShape` and is of kind `disk`. Opens nothing.
RealDisk checkRealDisk(const QJsonObject &target, const QString &dataDir, SystemLayer &system);

// Section 6.2 and 6.3 on a real disk: opened for reading only, never for writing.
CommandOutcome readStateOnDisk(SystemLayer &system, const RealDisk &disk, const LogFn &log,
                               const StageFn &stage);
CommandOutcome readSheetOnDisk(SystemLayer &system, const RealDisk &disk, const LogFn &log);

// Section 6.11.
CommandOutcome runInstallUdevRule(SystemLayer *system);

} // namespace tacetos::helper
