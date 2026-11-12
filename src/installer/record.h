// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

// The install record (docs/specs/bootloader-legacy.md, section 18): a small file
// in a folder of the computer that the caller gives, one per iPod and per
// operation, written after a successful verification. It is never written on the
// iPod and never needed to read the state of one.

namespace tacetos::installer {

struct InstallRecord
{
    QDateTime date;
    // "install", "update" or "uninstall".
    QString operation;
    QString target;
    // The serial number of the iPod when the system gives one, else empty.
    QString serial;
    qint64 diskBytes = 0;
    // The 40 bytes of the OSOS entry before the operation.
    QByteArray osEntryBefore;
    // Of the bootloader written. Empty and 0 for an uninstall.
    QString bootloaderVersion;
    quint32 bootloaderChecksum = 0;
    // How far the following images moved (section 14.1).
    qint64 delta = 0;
    // The name of the backup file, without folder.
    QString backupFile;
};

// The file name of a record: operation, target and date, and the size of the disk.
QString recordFileName(const InstallRecord &record);

// Writes the record in `dir` (created when missing) and gives its path. A file
// of the same name is never replaced: a number is added.
bool saveRecord(const InstallRecord &record, const QString &dir, QString &path, QString &why);

// Reads one record file. Nothing when the file is not a record of this format.
std::optional<InstallRecord> loadRecord(const QString &path);

// The most recent record of an install of this iPod (same target, serial and
// disk size) in `dir`, the one whose `osEntryBefore` is the entry Apple wrote.
// A record of an update holds the entry of our own bootloader and is not looked
// at (section 16, step 2).
std::optional<InstallRecord> findInstallRecord(const QString &dir, const QString &target,
                                               const QString &serial, qint64 diskBytes);

} // namespace tacetos::installer
