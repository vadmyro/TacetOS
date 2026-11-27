// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "disk.h"

#include "constants.h"

namespace tacetos::health {

DiskVerdict diskVerdict(DiskKind kind, const SmartBlock &smart)
{
    // On flash the attributes mean something else for each maker.
    if (kind == DiskKind::Flash || !smart.ok)
        return DiskVerdict::Unknown;

    const quint32 reallocated = smart.raw(kReallocatedId).value_or(0);
    const quint32 pending = smart.raw(kPendingId).value_or(0);
    const quint32 uncorrectable = smart.raw(kUncorrectableId).value_or(0);
    const quint32 spinRetries = smart.raw(kSpinRetriesId).value_or(0);
    if (reallocated >= device::kReallocatedFailing || pending >= device::kPendingFailing ||
        uncorrectable >= device::kUncorrectableFailing)
        return DiskVerdict::Failing;
    if (reallocated > 0 || pending > 0 || uncorrectable > 0 || spinRetries > 0)
        return DiskVerdict::Watch;
    return DiskVerdict::Good;
}

DiskReading readDisk(const HealthFile &file, QStringList *log)
{
    const QString name(device::kHealthFile);
    DiskReading disk;
    disk.kind = file.diskKind;
    disk.smartState = file.smart;
    disk.smartError = file.smartError;

    if (!file.identifyData.isEmpty()) {
        disk.identify = decodeIdentify(file.identifyData);
        if (!disk.identify.ok && log)
            log->append(
                QStringLiteral("%1: identify block ignored: %2").arg(name, disk.identify.error));
    }

    if (file.smart == SmartState::Ok) {
        if (file.smartData.isEmpty()) {
            disk.smart.error = QStringLiteral("no smart_data line");
        } else {
            disk.smart = decodeSmart(file.smartData);
        }
        if (!disk.smart.ok && log)
            log->append(QStringLiteral("%1: SMART block ignored: %2").arg(name, disk.smart.error));
        else if (!disk.smart.checksumOk && log)
            log->append(
                QStringLiteral("%1: the SMART checksum is wrong, the block is used").arg(name));
    }

    disk.verdict = diskVerdict(disk.kind, disk.smart);
    return disk;
}

} // namespace tacetos::health
