// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "recognise.h"
#include "sectordevice.h"

#include <QByteArray>
#include <QList>
#include <QString>

// The firmware partition in read only mode (docs/specs/bootloader-legacy.md,
// sections 7, 9 and 10): the directory, where the images are, and the state of
// the iPod. Nothing here writes to a disk.

namespace tacetos::installer {

// Section 7.2: the type of an image. A tag that is none of these is `Other`: the
// entry is kept, never modified.
enum class EntryKind { Osos, Rsrc, Aupd, Osbk, Hibe, Other };

// One entry of the directory, in the order of the list.
struct DirectoryEntry
{
    EntryKind kind = EntryKind::Other;
    // The image type as stored (backwards, 4 bytes), and the device tag.
    QByteArray typeTag;
    QByteArray deviceTag;
    quint32 imageNumber = 0;
    quint32 devOffset = 0;
    quint32 length = 0;
    quint32 loadAddress = 0;
    quint32 entryOffset = 0;
    quint32 checksum = 0;
    quint32 version = 0;
    quint32 flashAddress = 0;

    // Section 9: the length rounded up to a whole sector.
    qint64 paddedBytes(int sectorSize) const;
};

// Section 10.
enum class State {
    NotRead,      // the disk was not recognised, or could not be read
    Clean,        // no bootloader
    Installed,    // bootloader present (section 7.4 or 7.5)
    Inconsistent, // the entries contradict section 9 or section 11, no write is allowed
};

struct FirmwareState
{
    State state = State::NotRead;
    // For `Inconsistent`: the check of section 11 that the entries break (2 or 3),
    // and its message, "check N: ...". For `NotRead`, the message says why. Both
    // are empty otherwise.
    int failingCheck = 0;
    QString message;

    int sectorSize = 0;
    quint16 formatVersion = 0;
    QString target;
    // Where the directory starts, and where the image area starts, in bytes from
    // the start of the firmware partition (sections 7.2 and 7.3).
    qint64 directoryStart = 0;
    qint64 imageArea = 0;
    qint64 firmwareBytes = 0;
    QList<DirectoryEntry> entryList;
    // The index of the OSOS entry and of the OSBK entry in `entryList`, or -1.
    // The OSOS entry is the one of the model (section 5); with two of them, the first.
    int osIndex = -1;
    int osbkIndex = -1;

    bool isNano2g() const { return target == QLatin1String("ipodnano2g"); }
    // Where the bytes of an image start, from the start of the firmware partition
    // (section 7.3).
    qint64 imageStart(const DirectoryEntry &e) const { return imageArea + e.devOffset; }
};

// The text of a kind: `OSOS`, `RSRC`, `AUPD`, `OSBK`, `HIBE` or `other`.
QString entryKindName(EntryKind kind);

// Section 9: `length` rounded up to a whole sector (a power of two).
qint64 roundUpToSector(qint64 length, int sectorSize);

// Section 9: the data held in memory for one write, Apple's firmware padded to a
// sector, then the bootloader padded to a sector, must fit in 8 MiB.
bool writeDataFits(qint64 appleBytes, qint64 bootloaderBytes, int sectorSize);

// Section 10, after the recognition of section 6: reads the directory and gives
// the state. `recognition` must come from `recognise()` on the same disk; with
// any verdict but `Recognised` the state is `NotRead`.
FirmwareState readFirmware(SectorDevice &dev, const Recognition &recognition);

} // namespace tacetos::installer
