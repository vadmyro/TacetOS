// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "checks.h"

#include "constants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#ifndef TACETOS_ALLOW_REAL_WRITES
#define TACETOS_ALLOW_REAL_WRITES 0
#endif

namespace tacetos::installer {

namespace {

constexpr qint64 kChunkBytes = 1024 * 1024;

QString msgCheck(int n, const QString &text)
{
    return QStringLiteral("check %1: %2").arg(n).arg(text);
}

CheckReport fail(CheckReport r, int check, const QString &text, const QString &detail = QString())
{
    r.passed = false;
    r.failingCheck = check;
    r.message = check ? msgCheck(check, text) : text;
    r.detail = detail;
    return r;
}

// A failure whose message is already complete ("check N: ...").
CheckReport failWith(CheckReport r, int check, const QString &message)
{
    r.passed = false;
    r.failingCheck = check;
    r.message = message;
    return r;
}

bool needsFile(Operation op)
{
    return op != Operation::Uninstall;
}

// The state each operation starts from (section 12).
State requiredState(Operation op)
{
    return op == Operation::Install ? State::Clean : State::Installed;
}

// Check 1: the disk is the one of the scan.
bool sameDisk(SectorDevice &dev, const ScanSnapshot &scan, QString &why)
{
    if (!scan.valid || dev.sectorSize() != scan.sectorSize) {
        why = QStringLiteral("no scan, or another sector size");
        return false;
    }
    QByteArray sector0;
    QByteArray firmwareSector;
    IoResult io = dev.readSectors(0, 1, sector0);
    if (io)
        io = readBytes(dev, scan.firmwareStart, scan.sectorSize, firmwareSector);
    if (!io) {
        why = io.error;
        return false;
    }
    if (sector0 != scan.sector0 || firmwareSector != scan.firmwareSector) {
        why = QStringLiteral("sector 0 or the first firmware sector differs from the scan");
        return false;
    }
    const Recognition now = recognise(dev);
    if (now.verdict != Verdict::Recognised || now.firmwareStart != scan.firmwareStart ||
        now.firmwareBytes != scan.firmwareBytes || now.target != scan.target ||
        now.sectorSize != scan.sectorSize) {
        why = QStringLiteral("the disk is not recognised as it was at the scan");
        return false;
    }
    return true;
}

// Section 11 check 2, the part about the 2nd generation nano: an install adds one
// entry, and the directory holds 10 at most (section 7.2).
bool nanoDirectoryIsFull(const FirmwareState &s, Operation op)
{
    return op == Operation::Install && s.isNano2g() && s.osbkIndex < 0 &&
           s.entryList.size() >= kDirectoryMaxEntries;
}

// Check 7: the byte sum of Apple's firmware as it is on the iPod. `ok` is false
// when the disk cannot be read. Not called for an installed iPod or the nano.
bool firmwareSum(SectorDevice &dev, const Recognition &rec, const FirmwareState &fw, quint32 &sum,
                 QString &why)
{
    const DirectoryEntry &os = fw.entryList.at(fw.osIndex);
    const qint64 start = rec.firmwareStart + fw.imageStart(os);
    sum = 0;
    for (qint64 done = 0; done < os.length; done += kChunkBytes) {
        QByteArray part;
        const IoResult io =
            readBytes(dev, start + done, qMin<qint64>(kChunkBytes, os.length - done), part);
        if (!io) {
            why = io.error;
            return false;
        }
        for (char c : part)
            sum += quint8(c);
    }
    return true;
}

// Check 6: copies `total` bytes of the disk, from byte `start`, to `path`, reads
// the copy back and compares it with the disk.
bool copyRange(SectorDevice &dev, qint64 start, qint64 total, const QString &path, QString &why)
{
    const QStorageInfo storage(QFileInfo(path).absolutePath());
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < total) {
        why = QStringLiteral("only %1 bytes free").arg(storage.bytesAvailable());
        return false;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        why = out.errorString();
        return false;
    }
    for (qint64 done = 0; done < total; done += kChunkBytes) {
        QByteArray part;
        const IoResult io = readBytes(dev, start + done, qMin(kChunkBytes, total - done), part);
        if (!io) {
            why = io.error;
            out.close();
            QFile::remove(path);
            return false;
        }
        if (out.write(part) != part.size()) {
            why = out.errorString();
            out.close();
            QFile::remove(path);
            return false;
        }
    }
    if (!out.flush()) {
        why = out.errorString();
        out.close();
        QFile::remove(path);
        return false;
    }
    out.close();

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly) || in.size() != total) {
        why = QStringLiteral("the copy cannot be read back");
        QFile::remove(path);
        return false;
    }
    for (qint64 done = 0; done < total; done += kChunkBytes) {
        const qint64 n = qMin(kChunkBytes, total - done);
        QByteArray fromDisk;
        const IoResult io = readBytes(dev, start + done, n, fromDisk);
        if (!io || in.read(n) != fromDisk) {
            why = io ? QStringLiteral("the copy differs from the firmware partition") : io.error;
            in.close();
            QFile::remove(path);
            return false;
        }
    }
    return true;
}

// A name that does not exist yet in `dir`.
QString freeBackupPath(const QDir &dir, const QString &name)
{
    QString candidate = dir.filePath(name);
    if (!QFile::exists(candidate))
        return candidate;
    const QFileInfo info(name);
    for (int i = 2;; ++i) {
        candidate = dir.filePath(
            QStringLiteral("%1-%2.%3").arg(info.completeBaseName()).arg(i).arg(info.suffix()));
        if (!QFile::exists(candidate))
            return candidate;
    }
}

} // namespace

bool copyDiskRange(SectorDevice &dev, qint64 start, qint64 bytes, const QString &path, QString &why)
{
    return copyRange(dev, start, bytes, path, why);
}

QString freeFilePath(const QString &dir, const QString &name)
{
    return freeBackupPath(QDir(dir), name);
}

bool diskMatchesScan(SectorDevice &dev, const ScanSnapshot &scan, QString &why)
{
    return sameDisk(dev, scan, why);
}

bool realWritesAllowed()
{
    return TACETOS_ALLOW_REAL_WRITES != 0;
}

ScanSnapshot takeScanSnapshot(SectorDevice &dev, const Recognition &scan)
{
    ScanSnapshot s;
    if (scan.verdict != Verdict::Recognised)
        return s;
    s.sectorSize = scan.sectorSize;
    s.firmwareStart = scan.firmwareStart;
    s.firmwareBytes = scan.firmwareBytes;
    s.target = scan.target;
    IoResult io = dev.readSectors(0, 1, s.sector0);
    if (io)
        io = readBytes(dev, scan.firmwareStart, scan.sectorSize, s.firmwareSector);
    s.valid = bool(io);
    return s;
}

RoomResult checkRoom(const FirmwareState &s, Operation op, qint64 programBytes)
{
    RoomResult res;
    RoomPlan &p = res.plan;
    const QString noRoom = QStringLiteral("not enough room in the firmware partition");
    const auto refuse = [&](const QString &text) {
        res.ok = false;
        res.message = msgCheck(5, text);
        return res;
    };
    p.programBytes = programBytes;
    const int ss = s.sectorSize;

    if (s.isNano2g()) {
        // Section 15, steps 2 and 4: a new image after the last one, written
        // `kHashBlockBytes` earlier than its `devOffset`.
        if (programBytes <= kHashBlockBytes)
            return refuse(QStringLiteral("the bootloader file is too short"));
        const DirectoryEntry &last = s.entryList.last();
        p.newDevOffset =
            op == Operation::Update
                ? s.entryList.at(s.osIndex).devOffset
                : roundUpToSector(qint64(last.devOffset) + last.length, ss) + kHashBlockBytes;
        p.dataStart = s.imageArea + p.newDevOffset - kHashBlockBytes;
        p.dataEnd = p.dataStart + roundUpToSector(programBytes, ss);
        if (p.dataEnd > s.firmwareBytes)
            return refuse(noRoom);
        res.ok = true;
        return res;
    }

    // Section 14, steps 2, 3 and 6.
    const DirectoryEntry &os = s.entryList.at(s.osIndex);
    p.entryOffset =
        op == Operation::Update ? qint64(os.entryOffset) : roundUpToSector(os.length, ss);
    if (!writeDataFits(p.entryOffset, programBytes, ss))
        return refuse(QStringLiteral("the data to write does not fit in 8 MiB"));
    p.dataStart = s.imageStart(os) + p.entryOffset;
    p.dataEnd = p.dataStart + roundUpToSector(programBytes, ss);
    if (s.osIndex + 1 >= s.entryList.size()) {
        if (p.dataEnd > s.firmwareBytes)
            return refuse(noRoom);
        res.ok = true;
        return res;
    }
    const qint64 nextStart = s.imageStart(s.entryList.at(s.osIndex + 1));
    if (p.dataEnd > nextStart) {
        // Section 14.1: all the following images move together, by whole sectors.
        p.moveDelta = roundUpToSector(p.dataEnd - nextStart, ss);
        p.moveStart = nextStart;
        for (int i = s.osIndex + 1; i < s.entryList.size(); ++i) {
            const DirectoryEntry &e = s.entryList.at(i);
            p.moveEnd = qMax(p.moveEnd, s.imageStart(e) + e.paddedBytes(ss));
        }
        if (p.moveEnd + p.moveDelta > s.firmwareBytes)
            return refuse(noRoom);
    }
    res.ok = true;
    return res;
}

QString backupFileName(const QString &target, const QDateTime &when)
{
    return QStringLiteral("firmware-%1-%2.bin")
        .arg(target, when.toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

CheckReport runChecks(SectorDevice &dev, const CheckRequest &req)
{
    CheckReport r;
    if (!req.scan.valid)
        return fail(r, 0, QStringLiteral("there is no recognised iPod to check"));

    // Check 1.
    QString why;
    if (!sameDisk(dev, req.scan, why))
        return fail(r, 1, QStringLiteral("the disk is not the one that was scanned"), why);

    r.recognition = recognise(dev);
    r.firmware = readFirmware(dev, r.recognition);
    const FirmwareState &fw = r.firmware;
    if (fw.state == State::NotRead)
        return fail(r, 2, QStringLiteral("the directory cannot be read"), fw.message);

    // Checks 2 and 3. The state read now must be one the operation can start from.
    if (fw.state == State::Inconsistent && fw.failingCheck == 2)
        return failWith(r, 2, fw.message);
    if (nanoDirectoryIsFull(fw, req.op))
        return fail(r, 2, QStringLiteral("the directory has no room for one more entry"));
    if (fw.state == State::Inconsistent)
        return failWith(r, 3, fw.message);
    if (fw.state != requiredState(req.op))
        return fail(r, 0, QStringLiteral("the iPod is not in the state this operation needs"));

    // Check 4.
    if (needsFile(req.op)) {
        r.file = checkBootloaderFile(req.bootloaderFile, fw.target);
        if (!r.file.ok())
            return failWith(r, 4, r.file.message);
        // Check 5.
        const RoomResult room = checkRoom(fw, req.op, r.file.program.size());
        r.plan = room.plan;
        if (!room.ok)
            return failWith(r, 5, room.message);
    }

    // Check 6.
    const QDir dir(req.backupDir);
    if (req.backupDir.isEmpty() || !QDir().mkpath(dir.absolutePath())) {
        return fail(r, 6,
                    QStringLiteral("cannot save a backup in the data folder (%1 bytes needed)")
                        .arg(r.recognition.firmwareBytes),
                    QStringLiteral("the folder cannot be created"));
    }
    const QString path = freeBackupPath(dir, backupFileName(fw.target, req.now));
    if (!copyRange(dev, r.recognition.firmwareStart, r.recognition.firmwareBytes, path, why)) {
        return fail(r, 6,
                    QStringLiteral("cannot save a backup in the data folder (%1 bytes needed)")
                        .arg(r.recognition.firmwareBytes),
                    why);
    }
    r.backupPath = path;

    // Check 7: a warning, never a refusal. Skipped for the 2nd generation nano
    // (the sum is the one of the data before encryption) and for an installed
    // iPod, where the directory sum also covers the bootloader and the spec does
    // not say what to compare (section 27).
    if (!fw.isNano2g() && fw.state == State::Clean) {
        quint32 sum = 0;
        if (!firmwareSum(dev, r.recognition, fw, sum, why))
            return fail(r, 7, QStringLiteral("the firmware cannot be read"), why);
        if (sum != fw.entryList.at(fw.osIndex).checksum)
            r.warnings.append(
                msgCheck(7, QStringLiteral("the firmware checksum differs from the directory")));
    }

    // Check 8.
    if (dev.isRealDisk() && !req.allowRealWrites) {
        return fail(r, 8, QStringLiteral("this build does not write to a real iPod"));
    }

    r.passed = true;
    return r;
}

} // namespace tacetos::installer
