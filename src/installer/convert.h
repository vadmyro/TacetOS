// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "checks.h"
#include "format.h"

#include <QByteArray>
#include <QString>

// Converting a Mac iPod to FAT32 (docs/specs/bootloader-legacy.md, section 21):
// the music partition is formatted first, then a DOS table replaces the
// Apple Partition Map in sector 0, which is the single moment when the disk turns
// into a Windows iPod. Sector 0 and slot 2 are the only places written (section
// 24, rule 3). The typed confirmation of section 22 is asked by the request.

namespace tacetos::installer {

// Section 21 step 2: the new sector 0, all zeros, then the 4 entries of the DOS
// table (firmware partition of type 0, music partition of type 0x0B, two empty
// entries) and the signature.
QByteArray buildDosSector0(const Slot &firmware, const Slot &music);

// Section 21 step 3: the new table is coherent on a disk of `diskSectors` sectors.
// `why` says what is wrong.
bool dosTableIsCoherent(const Slot &firmware, const Slot &music, qint64 diskSectors, QString &why);

// Section 21, "allowed only when": reads the disk, writes nothing.
struct ConvertPermission
{
    bool allowed = false;
    // One short English message when not allowed.
    QString message;
    Recognition recognition;
    FirmwareState firmware;
    // Slots 1 and 2 of the map, set when allowed.
    Slot firmwareSlot;
    Slot musicSlot;
    // Section 21 step 4, set when allowed.
    FatGeometry geometry;
};

ConvertPermission checkConvertAllowed(SectorDevice &dev, bool slotUnmounted);

struct ConvertRequest : FormatRequest
{
    // What the scan saw: section 24 rule 1 and 11, the disk is the one that was
    // scanned.
    ScanSnapshot scan;
    // Section 21 step 1: the folder of the backups, on the computer, never on the
    // iPod. It is created when it does not exist.
    QString backupDir;
    // Tests only: makes the write of the backups fail (a full folder).
    FileFault backupFault;
};

struct ConvertResult
{
    bool ok = false;
    // One short English message when not ok.
    QString message;
    // Set once the first write was asked for.
    bool wroteAnything = false;
    // The music partition was formatted, and the table was written. When the
    // first is set and not the second, the disk is still a Mac iPod.
    bool formatted = false;
    bool tableWritten = false;
    // Section 21 step 1: the copy of the firmware partition, and the copy of the
    // sectors before it (the map). Empty when no backup was made.
    QString backupPath;
    QString mapBackupPath;
    FormatResult format;
    // Section 21 step 7: the recognition after the table was written.
    Recognition after;
};

// Section 21 in full. Nothing is read or written when the typed name does not
// match (section 22), when the build does not write to a real iPod (section 11
// check 8) or when the disk is not the one that was scanned.
ConvertResult convertMacToFat32(SectorDevice &dev, const ConvertRequest &request);

} // namespace tacetos::installer
