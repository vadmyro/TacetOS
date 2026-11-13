// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "checks.h"
#include "constants.h"

#include <QString>
#include <QStringList>

// Install, update and uninstall of the bootloader (docs/specs/bootloader-legacy.md,
// sections 13 to 17): appended inside the OSOS image for every model but the 2nd
// generation nano (sections 14, 14.1 and 16), and as an image of its own for the
// 2nd generation nano (sections 15 and 17). The steps are written in the order of
// the spec; each write is flushed and read back before the next one starts.

namespace tacetos::installer {

// The write step that was running when an operation failed, for the table of
// section 19. `None` when nothing was written. `Verify` is the final verification of
// section 18, which reads and writes nothing.
enum class WriteStep {
    None,
    RemoveBootloader, // section 14 step 7, section 15 step 6: the directory as after an uninstall
    MoveImages,       // section 14.1: the copy of the following images
    MoveDirectory,    // section 14 step 8: the directory sector with the new devOffset values
    WriteProgram,     // section 14 step 9, section 15 step 7
    WriteDirectory,   // section 14 step 10, section 15 step 8, section 16 step 4, section 17 step 3
    Verify,           // section 18
};

// What the app does next after a failure (section 19). `None` when the operation
// succeeded, or when it stopped before the first write.
enum class NextAction {
    None,
    OfferRetry,   // the iPod is clean, the backup is kept
    ReportState,  // the state read again is Clean or Installed: say which
    OfferRestore, // the state read again is Inconsistent: offer to write the backup back
    Restored,     // the images were half moved: the backup was written back right away
    RestoreFailed // the backup could not be written back: the iPod may not start
};

struct OperationRequest : CheckRequest
{
    // Section 14.1: the largest piece of the copy of the following images.
    qint64 pieceBytes = kPieceBytes;
    // Section 18: the folder of the install record. Empty: no record is written.
    // It is a folder of the computer, never on the iPod. For an uninstall, the
    // record of the install in this folder gives the entry to go back to
    // (section 16, step 2).
    QString recordDir;
    // Tests only: makes the write of the record fail (a full folder).
    FileFault recordFault;
    // Written in the record: the serial number of the iPod when the system gives
    // one, and the version of the bootloader file.
    QString serial;
    QString bootloaderVersion;
};

struct OperationResult
{
    bool ok = false;
    // One short English message when not ok. "check N: ..." when a check refused
    // and nothing was written.
    QString message;
    // The checks that ran just before the first write (section 11).
    CheckReport report;
    // Set once the first write was asked for, and the step that failed.
    bool wroteAnything = false;
    WriteStep failedStep = WriteStep::None;

    // Section 19, set when the operation failed after the first write: the state
    // read again on the disk (`NotRead` when it could not be read), and what the
    // app does next.
    State stateAfter = State::NotRead;
    NextAction next = NextAction::None;
    // Set when the backup was written back right away (move of the images).
    bool restoredBackup = false;

    // Section 18, set when the operation succeeded: the record written, empty when
    // none was asked for. `recordMessage` says why a record could not be written;
    // the operation is still a success then.
    QString recordPath;
    QString recordMessage;
    // Section 16 step 2: the uninstall went back to the entry saved in a record.
    bool usedRecord = false;
};

struct RestoreResult
{
    bool ok = false;
    // One short English message when not ok.
    QString message;
    // Set when the backup was refused for not being of this disk (nothing was
    // written).
    bool mismatch = false;
};

// Section 19, "writing the backup back": the disk is checked against the scan
// (sector 0 and the banner of the backup must match), then the whole firmware
// partition is written from the backup file in pieces, flushed, read back and
// compared. The backup file is never deleted.
RestoreResult restoreBackup(SectorDevice &dev, const ScanSnapshot &scan, const QString &backupPath,
                            qint64 pieceBytes = kPieceBytes);

// Runs the 8 checks of section 11 on the device, then the writes of the
// operation in `request.op`, then the verification of section 18 and the record.
// A failure after the first write is handled as in the table of section 19.
OperationResult runOperation(SectorDevice &dev, const OperationRequest &request);

} // namespace tacetos::installer
