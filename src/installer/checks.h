// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "bootfile.h"
#include "firmware.h"
#include "recognise.h"
#include "sectordevice.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

// The 8 checks before any write (docs/specs/bootloader-legacy.md, section 11),
// in the order of the spec, each with its message. The checks write nothing to
// the disk: the only thing they create is the backup file of check 6, in a
// folder the caller gives, never on the iPod.

namespace tacetos::installer {

// What the user asked for (section 12). It decides which state is expected, and
// whether a bootloader file is needed (checks 4 and 5).
enum class Operation { Install, Update, Uninstall };

// What the scan saw (check 1): the recognised disk, sector 0 and the first
// sector of the firmware partition.
struct ScanSnapshot
{
    bool valid = false;
    int sectorSize = 0;
    qint64 firmwareStart = 0;
    qint64 firmwareBytes = 0;
    QString target;
    QByteArray sector0;
    QByteArray firmwareSector;
};

// Reads the snapshot of a disk that `recognise()` has recognised. Not valid when
// the verdict is not `Recognised` or when the disk cannot be read.
ScanSnapshot takeScanSnapshot(SectorDevice &dev, const Recognition &scan);

// Section 11 check 5 and sections 14.1 and 15: where the new data goes, and how
// far the following images must move. All positions are in bytes from the start
// of the firmware partition.
struct RoomPlan
{
    // The layout of all models but the 2nd generation nano: where the program
    // starts inside the OSOS image (section 14, step 2). 0 for the nano.
    qint64 entryOffset = 0;
    // The program without its 8 byte header (for the nano, with its hash block).
    qint64 programBytes = 0;
    // Where the bytes of the new data start (for the nano, the hash block comes
    // first, section 7.5) and where they end, padding included.
    qint64 dataStart = 0;
    qint64 dataEnd = 0;
    // The 2nd generation nano: the `devOffset` of the new OSOS entry.
    qint64 newDevOffset = 0;
    // Section 14.1: the distance the following images move, 0 when they stay,
    // and the block that moves.
    qint64 moveDelta = 0;
    qint64 moveStart = 0;
    qint64 moveEnd = 0;
};

struct RoomResult
{
    bool ok = false;
    // "check 5: ...", in English, when not ok.
    QString message;
    RoomPlan plan;
};

// Check 5, on a state that passed checks 2 and 3. `programBytes` is the size of
// the program of a bootloader file that passed check 4.
RoomResult checkRoom(const FirmwareState &state, Operation op, qint64 programBytes);

// The text of the backup file for a model at a given time, without folder.
QString backupFileName(const QString &target, const QDateTime &when);

// Check 1: the disk is still the one of the scan (sector 0, the first sector of
// the firmware partition, and the recognition). `why` says what differs.
bool diskMatchesScan(SectorDevice &dev, const ScanSnapshot &scan, QString &why);

// Check 6, for any range: copies `bytes` bytes of the disk from byte `start` to
// the file `path`, reads the copy back and compares it. The file is removed when
// the copy fails. `why` says what went wrong.
bool copyDiskRange(SectorDevice &dev, qint64 start, qint64 bytes, const QString &path,
                   QString &why);

// A file name that does not exist yet in `dir` (`-2`, `-3`... before the suffix).
QString freeFilePath(const QString &dir, const QString &name);

// Whether this build may write to a real disk (the CMake option
// `TACETOS_ALLOW_REAL_WRITES`, off until the beta).
bool realWritesAllowed();

struct CheckRequest
{
    Operation op = Operation::Install;
    ScanSnapshot scan;
    // The file as downloaded, header included. Not read for an uninstall.
    QByteArray bootloaderFile;
    // The folder where check 6 puts its copy of the firmware partition. It is
    // created when it does not exist.
    QString backupDir;
    QDateTime now = QDateTime::currentDateTime();
    // Check 8. The default is the build option; the tests set it by hand.
    bool allowRealWrites = realWritesAllowed();
};

struct CheckReport
{
    bool passed = false;
    // The first check that failed, 1 to 8. 0 when every check passed, or when the
    // request cannot be checked at all (no recognised iPod, wrong state for the
    // operation): the message says which.
    int failingCheck = 0;
    // One short English message when not passed, "check N: ...".
    QString message;
    // What goes to the log: the reason behind the message.
    QString detail;
    // Warnings that do not refuse (check 7), "check 7: ...".
    QStringList warnings;

    // Read again on the disk, as the write steps need them.
    Recognition recognition;
    FirmwareState firmware;
    BootFileCheck file;
    RoomPlan plan;
    // The copy of the firmware partition of check 6.
    QString backupPath;
};

// The 8 checks in order, stopping at the first that fails. Nothing is written to
// the disk, whatever the result.
CheckReport runChecks(SectorDevice &dev, const CheckRequest &request);

} // namespace tacetos::installer
