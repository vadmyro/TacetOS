// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "partitiontable.h"
#include "sectordevice.h"

#include <QByteArray>
#include <QList>
#include <QString>

// Does this disk look like an iPod, which firmware partition does it have and
// which model is it (docs/specs/bootloader-legacy.md, sections 3, 4 and 5).
// Read only: nothing here writes to a disk. What is read of the directory is
// just enough to find the OSOS entry; the whole directory is the job of the
// firmware partition reader.

namespace tacetos::installer {

// How far the reading went. The tests compare it with the table of fixtures.
enum class Verdict {
    UnknownTable,        // section 2: no DOS signature, no `ER`
    PartitionTestFailed, // section 3
    NoBanner,            // section 4
    BadDirectory,        // section 7.1: no `]ih[`, or a directory outside the partition
    UnknownGeneration,   // section 5: looks like an iPod, not one we handle
    BadSectorSize,       // section 9: not a power of two
    ReadError,           // the disk did not answer
    Recognised,
};

struct Recognition
{
    Verdict verdict = Verdict::UnknownTable;
    // One short English message for every verdict but `Recognised`.
    QString message;
    // The slots of section 2.3. Empty when the sector size or the table was refused.
    PartitionTable table;
    int sectorSize = 0;
    // Slot 1, in bytes from the start of the disk. Set from the partition test on.
    qint64 firmwareStart = 0;
    qint64 firmwareBytes = 0;
    // Set when the verdict is `Recognised` or `UnknownGeneration`.
    quint16 formatVersion = 0;
    // Where the directory starts (section 7.1 and 7.2), in bytes from the start
    // of the firmware partition, after the "one sector late" rule. Set from the
    // reading of the directory on.
    qint64 directoryStart = 0;
    quint32 osVersion = 0;
    // Section 5: the version shifted right by 8 bits, and its target name
    // (empty for an unknown generation).
    quint32 modelValue = 0;
    QString target;

    bool isMac() const { return table.kind == TableKind::Apm; }
};

// Section 3. `forScan`: slot 1 must not start at sector 0.
bool passesPartitionTest(const QList<Slot> &slotList, bool forScan = true);

// Section 4: the first 256 bytes of the firmware partition.
bool bannerMatches(const QByteArray &firstBytes);

// Section 5: the target name of a value, or an empty string.
QString targetForModelValue(quint32 value);

// Sections 2 to 5 in order, on an opened disk.
Recognition recognise(SectorDevice &dev, bool forScan = true);

} // namespace tacetos::installer
