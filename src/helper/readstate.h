// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "sectordevice.h"

#include <QJsonObject>
#include <QString>
#include <functional>

// The command `read-state` (docs/specs/helper.md section 6.2): recognises the
// iPod and reads its state. Reads only.

namespace tacetos::helper {

struct CommandOutcome
{
    bool ok = false;
    QJsonObject data;
    // When not ok: one code of section 13, and the reason in English.
    QString error;
    QString detail;
};

// `log(level, text)` is called for the lines of the journal, `progress(stage)`
// at each change of stage.
using LogFn = std::function<void(const QString &level, const QString &text)>;
using StageFn = std::function<void(const QString &stage)>;

// Section 5.3: made from what was read. The helper does not keep it; a later
// command reads the same values again and compares the strings. For a real disk
// the USB bus and device numbers are added (`usbBus`, `usbDevice`, -1 for none).
QString makeScanToken(qint64 sizeBytes, int sectorSize, const QByteArray &sector0,
                      const QByteArray &firmwareSector, int usbBus = -1, int usbDevice = -1);

CommandOutcome readState(installer::SectorDevice &dev, const LogFn &log, const StageFn &stage);

} // namespace tacetos::helper
