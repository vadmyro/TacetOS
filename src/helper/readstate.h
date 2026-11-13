// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "checks.h"
#include "recognise.h"
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

// The token of a disk that was recognised as `rec` with the snapshot `snap`. Empty,
// with `error` set, when sector 0 cannot be read.
QString scanTokenFor(installer::SectorDevice &dev, const installer::Recognition &rec,
                     const installer::ScanSnapshot &snap, QString *error, int usbBus = -1,
                     int usbDevice = -1);

// `usbBus` and `usbDevice` are those of a real disk (section 5.3), -1 for an image.
CommandOutcome readState(installer::SectorDevice &dev, const LogFn &log, const StageFn &stage,
                         int usbBus = -1, int usbDevice = -1);

} // namespace tacetos::helper
