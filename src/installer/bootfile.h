// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QString>

// The bootloader file the build gives for each model
// (docs/specs/bootloader-legacy.md, section 8 and check 4 of section 11).

namespace tacetos::installer {

// One row of the table of section 8.
struct BootFileModel
{
    const char *target;
    // The model tag of the header, 4 characters.
    const char *tag;
    // The model number added to the checksum.
    quint32 number;
};

// The row of a target name, or null.
const BootFileModel *bootFileModel(const QString &target);

// Section 8: the model number plus the sum of all bytes of `program` (as unsigned
// 8-bit values), modulo 2^32.
quint32 bootFileChecksum(quint32 modelNumber, const QByteArray &program);

enum class BootFileStatus {
    Ok,
    UnknownTarget, // the target has no row in the table
    TooShort,      // fewer than 8 bytes of header plus one byte of program
    WrongModel,    // the tag is not the one of the target
    BadChecksum,
};

struct BootFileCheck
{
    BootFileStatus status = BootFileStatus::UnknownTarget;
    // "check 4: ...", in English, for every status but `Ok`.
    QString message;
    // Set when the header could be read.
    quint32 headerChecksum = 0;
    QByteArray tag;
    // The program, without the header; set when the status is `Ok`.
    QByteArray program;

    bool ok() const { return status == BootFileStatus::Ok; }
};

// Check 4 of section 11 for a file offered for `target` (the target recognised
// from the iPod, section 5): length, then model tag, then checksum, so that a file
// of another model with a checksum that passes is still refused.
BootFileCheck checkBootloaderFile(const QByteArray &file, const QString &target);

} // namespace tacetos::installer
