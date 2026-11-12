// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "sectordevice.h"

#include <QList>
#include <QString>

// The two kinds of partition table and the one description of them
// (docs/specs/bootloader-legacy.md, section 2). Read only.

namespace tacetos::installer {

enum class TableKind {
    Dos, // Windows iPod
    Apm, // Mac iPod
};

// One slot of section 2.3. `type` is the DOS type, or kTypeHfs for an HFS
// partition of a Mac iPod. A DOS table gives 4 slots by position, empty ones
// all zero; an Apple Partition Map gives the kept entries, 4 at most.
struct Slot
{
    quint32 type = 0;
    quint32 firstSector = 0;
    quint32 sectorCount = 0;

    bool isEmpty() const { return type == 0 && firstSector == 0 && sectorCount == 0; }
    bool operator==(const Slot &o) const
    {
        return type == o.type && firstSector == o.firstSector && sectorCount == o.sectorCount;
    }
    // Start and size in bytes, for a disk with sectors of `sectorSize` bytes.
    qint64 startBytes(int sectorSize) const { return qint64(firstSector) * sectorSize; }
    qint64 sizeBytes(int sectorSize) const { return qint64(sectorCount) * sectorSize; }
};

enum class TableStatus {
    Ok,
    UnknownTable, // neither the DOS signature nor `ER` (section 2, step 3)
    BadMap,       // an Apple map that cannot be read safely (section 2.2)
    ReadError,    // the disk did not answer
};

struct PartitionTable
{
    TableStatus status = TableStatus::UnknownTable;
    TableKind kind = TableKind::Dos;
    QList<Slot> slotList;
    // In English: why the table was not read, or what the disk said.
    QString message;
};

// Reads sector 0 and, for a Mac iPod, the map. Slots are filled only when the
// status is Ok.
PartitionTable readPartitionTable(SectorDevice &dev);

} // namespace tacetos::installer
