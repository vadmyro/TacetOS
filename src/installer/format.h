// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "checks.h"
#include "constants.h"
#include "firmware.h"
#include "partitiontable.h"
#include "sectordevice.h"

#include <QDateTime>
#include <QString>

// Formatting the music partition as FAT32 (docs/specs/bootloader-legacy.md,
// section 20): when it is allowed, the sizes, what is written, and in which
// order. It writes slot 2 and nothing else. The conversion of a Mac iPod
// (section 21) is in convert.h. The typed confirmation of section 22 is here,
// because both operations need it: the API refuses without the typed name.

namespace tacetos::installer {

// Section 20.2: the sizes of a FAT32 file system for a partition of `total`
// sectors.
struct FatGeometry
{
    bool ok = false;
    // One short English message when not ok ("This partition size is not
    // supported").
    QString message;
    quint32 total = 0;
    quint32 sectorsPerCluster = 0;
    quint32 fatSize = 0;
    quint32 clusters = 0;

    // Sector of the first and of the second FAT, and of the root folder
    // (cluster 2), counted from the start of the partition.
    qint64 firstFatSector() const { return kFatReservedSectors; }
    qint64 secondFatSector() const { return kFatReservedSectors + fatSize; }
    qint64 rootSector() const { return kFatReservedSectors + qint64(kFatCount) * fatSize; }
};

// The cluster size in bytes for a partition of `total` sectors (table of 20.2).
int clusterBytesFor(qint64 total);

FatGeometry computeFatGeometry(qint64 total);

// Section 20.3: the serial number built from the date and time of the format,
// never 0.
quint32 volumeSerialFor(const QDateTime &when);

struct FormatOptions
{
    // Upper case and padded with spaces on the disk. Section 20.3 default: `IPOD`.
    QString label = kDefaultVolumeLabel;
    // The date and time of the format, for the serial number.
    QDateTime now = QDateTime::currentDateTime();
    // As reported by the OS, else 63 and 255 (section 20.3).
    int sectorsPerTrack = 63;
    int heads = 255;
    // Section 20.4: the largest piece of a write of zeros.
    qint64 pieceBytes = kPieceBytes;
};

struct FormatResult
{
    bool ok = false;
    // One short English message when not ok.
    QString message;
    // Set once the first write was asked for: the old content of the partition may
    // be gone (section 20.4 step 1).
    bool wroteAnything = false;

    FatGeometry geometry;
    quint32 serial = 0;
    // The partition that was (or would have been) formatted, as in the table.
    Slot slot;
    // Sections 20.4 and 26.4: what was written, for the report and the tests.
    qint64 bytesWritten = 0;
    int writeCount = 0;
};

// The write steps of 20.4 on the sectors `[firstSector, firstSector + total)` of
// `dev`, with no look at the partition table or at the state of the iPod: the
// caller has done 20.1. Sector size 512 only. `firstSector` goes whole (32 bits)
// into the hidden sectors field.
FormatResult writeFat32(SectorDevice &dev, quint32 firstSector, quint32 total,
                        const FormatOptions &options);

// Section 22: the button of the dialog is enabled only when the user has typed
// the name of the iPod shown in the dialog. `shown` is that name and `typed` what
// was typed. Leading and trailing spaces of the typed text do not count; the
// case does. An empty name never matches.
bool confirmationMatches(const QString &shown, const QString &typed);

struct FormatRequest : FormatOptions
{
    // Section 22 and section 24 rule 7: the name of the iPod shown in the dialog
    // and the text typed by the user. Nothing is read or written when they do
    // not match.
    QString ipodName;
    QString typedName;
    // Section 20.1 point 4: the helper has unmounted slot 2 (and refuses when it
    // cannot). Nothing is written otherwise.
    bool slotUnmounted = false;
    // Section 11 check 8: the default is the build option; the tests set it by hand.
    bool allowRealWrites = realWritesAllowed();
};

// Section 20.1 points 1 to 4: whether the button is shown. Reads the disk,
// writes nothing. The confirmation of point 5 is not part of it.
struct FormatPermission
{
    bool allowed = false;
    // One short English message when not allowed.
    QString message;
    // Slot 2, set when allowed.
    Slot slot;
    Recognition recognition;
    FirmwareState firmware;
};

FormatPermission checkFormatAllowed(SectorDevice &dev, bool slotUnmounted);

// Section 20.1, then 20.2 to 20.4 on slot 2 of the disk. Nothing is written when
// the format is refused (`wroteAnything` stays false).
FormatResult formatMusicPartition(SectorDevice &dev, const FormatRequest &request);

} // namespace tacetos::installer
