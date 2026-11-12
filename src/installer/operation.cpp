// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "operation.h"

#include "record.h"

#include <QFile>
#include <QFileInfo>
#include <functional>
#include <optional>

namespace tacetos::installer {

namespace {

// Section 4: the banner is in the first 256 bytes of the firmware partition.
constexpr int kBannerBytes = 256;
// Section 7.1: the volume header, before the directory at 0x200.
constexpr int kVolumeHeaderBytes = 512;

void put32le(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xFF);
}

quint32 get32le(const QByteArray &b, int at)
{
    quint32 v = 0;
    for (int i = 0; i < 4; ++i)
        v |= quint32(quint8(b[at + i])) << (8 * i);
    return v;
}

quint32 byteSum(const QByteArray &data, quint32 sum = 0)
{
    for (char c : data)
        sum += quint8(c);
    return sum;
}

// Section 13: every write is whole sectors, flushed, read back and compared.
class Writer
{
public:
    explicit Writer(SectorDevice &dev) : m_dev(dev) {}

    bool wrote() const { return m_wrote; }

    // `data` goes to byte `offset` of the disk. A partial sector at either end is
    // read whole, changed in memory and written whole (section 13.1), so the bytes
    // around the range stay as they were.
    IoResult write(qint64 offset, const QByteArray &data)
    {
        const int ss = m_dev.sectorSize();
        const qint64 first = offset / ss;
        const qint64 end = roundUpToSector(offset + data.size(), ss);
        const qint64 count = end / ss - first;
        QByteArray buffer;
        if (offset % ss == 0 && data.size() % ss == 0) {
            buffer = data;
        } else {
            const IoResult io = m_dev.readSectors(first, count, buffer);
            if (!io)
                return io;
            buffer.replace(int(offset - first * ss), data.size(), data);
        }
        m_wrote = true;
        IoResult io = m_dev.writeSectors(first, buffer);
        if (io)
            io = m_dev.flush();
        if (!io)
            return io;
        QByteArray back;
        io = m_dev.readSectors(first, count, back);
        if (!io)
            return io;
        if (back != buffer)
            return IoResult::failure(
                QStringLiteral("the data read back differs from the data written"));
        return IoResult::success();
    }

private:
    SectorDevice &m_dev;
    bool m_wrote = false;
};

// Section 13 rule 4: the directory sector is read, changed in memory, written
// whole. `change` gets the sector and the position of the first entry in it.
IoResult writeDirectory(SectorDevice &dev, Writer &w, const Recognition &rec,
                        const FirmwareState &fw,
                        const std::function<void(QByteArray &, int)> &change)
{
    const int ss = dev.sectorSize();
    const qint64 sectorStart = rec.firmwareStart + fw.directoryStart / ss * ss;
    QByteArray sector;
    const IoResult io = readBytes(dev, sectorStart, ss, sector);
    if (!io)
        return io;
    change(sector, int(fw.directoryStart % ss));
    return w.write(sectorStart, sector);
}

// The OSOS entry as it is when the bootloader is not in it (section 16, step 3):
// Apple's firmware with its padding, no entry offset.
void setCleanEntry(QByteArray &sector, int first, int osIndex, quint32 len, quint32 sum)
{
    const int at = first + osIndex * kDirectoryEntrySize;
    put32le(sector, at + kEntryLengthOffset, len);
    put32le(sector, at + kEntryEntryOffsetOffset, 0);
    put32le(sector, at + kEntryChecksumOffset, sum);
}

IoResult readFirmwareBytes(SectorDevice &dev, const Recognition &rec, qint64 offset, qint64 length,
                           QByteArray &out)
{
    return readBytes(dev, rec.firmwareStart + offset, length, out);
}

// Section 14.1: the block of the following images moves forward by `delta`, in
// pieces, starting from the end; then the new place is compared with the backup.
IoResult moveImages(SectorDevice &dev, Writer &w, const Recognition &rec, const RoomPlan &plan,
                    const QString &backupPath, qint64 pieceBytes)
{
    const qint64 piece = pieceBytes > 0 ? pieceBytes : kPieceBytes;
    for (qint64 end = plan.moveEnd; end > plan.moveStart;) {
        const qint64 n = qMin(piece, end - plan.moveStart);
        const qint64 from = end - n;
        QByteArray part;
        IoResult io = readFirmwareBytes(dev, rec, from, n, part);
        if (io)
            io = w.write(rec.firmwareStart + from + plan.moveDelta, part);
        if (!io)
            return io;
        end = from;
    }

    QFile backup(backupPath);
    if (!backup.open(QIODevice::ReadOnly))
        return IoResult::failure(QStringLiteral("the backup cannot be read"));
    for (qint64 done = plan.moveStart; done < plan.moveEnd;) {
        const qint64 n = qMin(kPieceBytes, plan.moveEnd - done);
        QByteArray expected;
        if (!backup.seek(done) || (expected = backup.read(n)).size() != n)
            return IoResult::failure(QStringLiteral("the backup cannot be read"));
        QByteArray moved;
        const IoResult io = readFirmwareBytes(dev, rec, done + plan.moveDelta, n, moved);
        if (!io)
            return io;
        if (moved != expected)
            return IoResult::failure(QStringLiteral("the moved images differ from the backup"));
        done += n;
    }
    return IoResult::success();
}

OperationResult failed(OperationResult r, const Writer &w, WriteStep step, const QString &text,
                       const IoResult &io)
{
    r.ok = false;
    r.wroteAnything = w.wrote();
    r.failedStep = step;
    r.message = QStringLiteral("%1: %2").arg(text, io.error);
    return r;
}

void setTag(QByteArray &sector, int entryAt, QLatin1String tag)
{
    sector.replace(entryAt + kEntryTypeOffset, 4, QByteArray(tag.data(), 4));
}

// Sections 15 and 17, the 2nd generation nano: the bootloader is an image of its
// own after the last one, and Apple's entry is the OSBK entry. The checks have
// passed and `res` holds their report.
OperationResult runNanoOperation(SectorDevice &dev, const OperationRequest &req,
                                 OperationResult res)
{
    const Recognition &rec = res.report.recognition;
    const FirmwareState &fw = res.report.firmware;
    const RoomPlan &plan = res.report.plan;
    const int osIndex = fw.osIndex;
    const int osbkIndex = fw.osbkIndex;
    Writer w(dev);

    // The directory sector as after an uninstall (section 17 step 3): the OSOS
    // entry is 40 zero bytes and the OSBK entry is OSOS again, in one write.
    const auto removeBootloader = [&](QByteArray &sector, int first) {
        const int at = first + osIndex * kDirectoryEntrySize;
        sector.replace(at, kDirectoryEntrySize, QByteArray(kDirectoryEntrySize, '\0'));
        setTag(sector, first + osbkIndex * kDirectoryEntrySize, kTagOsos);
    };

    if (req.op == Operation::Uninstall) {
        // Section 17 step 2: with another order, the list would end early.
        if (osbkIndex < 0 || osIndex != fw.entryList.size() - 1) {
            res.message = QStringLiteral("the directory is not in the state this operation needs");
            return res;
        }
        const IoResult io = writeDirectory(dev, w, rec, fw, removeBootloader);
        if (!io)
            return failed(res, w, WriteStep::WriteDirectory,
                          QStringLiteral("could not write the directory"), io);
        res.ok = true;
        res.wroteAnything = true;
        return res;
    }

    const QByteArray &program = res.report.file.program;
    const quint32 newLen = quint32(program.size() - kHashBlockBytes);
    const quint32 newSum = byteSum(program);

    // Step 6, only for an update: back to the clean state, Apple's firmware runs.
    if (req.op == Operation::Update) {
        const IoResult io = writeDirectory(dev, w, rec, fw, removeBootloader);
        if (!io)
            return failed(res, w, WriteStep::RemoveBootloader,
                          QStringLiteral("could not remove the old bootloader"), io);
    }

    // Step 7: the hash block, the program and the zero padding, built in memory.
    QByteArray data = program;
    data.append(QByteArray(int(plan.dataEnd - plan.dataStart) - program.size(), '\0'));
    IoResult io = w.write(rec.firmwareStart + plan.dataStart, data);
    if (!io)
        return failed(res, w, WriteStep::WriteProgram,
                      QStringLiteral("could not write the bootloader"), io);

    // Step 8: Apple's entry becomes OSBK and the new entry is added after the last
    // one, in the same sector write. On an update the sector holds the clean
    // directory of step 6: Apple's entry is the first and the new one takes the
    // place that was zeroed.
    const int appleIndex = req.op == Operation::Update ? osbkIndex : osIndex;
    const int newIndex = req.op == Operation::Update ? osIndex : fw.entryList.size();
    io = writeDirectory(dev, w, rec, fw, [&](QByteArray &sector, int first) {
        const int apple = first + appleIndex * kDirectoryEntrySize;
        const int added = first + newIndex * kDirectoryEntrySize;
        QByteArray entry = sector.mid(apple, kDirectoryEntrySize);
        setTag(sector, apple, kTagOsbk);
        sector.replace(added, kDirectoryEntrySize, entry);
        setTag(sector, added, kTagOsos);
        put32le(sector, added + kEntryDevOffsetOffset, quint32(plan.newDevOffset));
        put32le(sector, added + kEntryLengthOffset, newLen);
        put32le(sector, added + kEntryChecksumOffset, newSum);
    });
    if (!io)
        return failed(res, w, WriteStep::WriteDirectory,
                      QStringLiteral("could not write the directory"), io);

    res.ok = true;
    res.wroteAnything = true;
    return res;
}

// Section 14, 14.1 and 16: the layout of all models but the 2nd generation nano.
// `savedEntry` is the 40 bytes an uninstall goes back to, when an install record
// gives them (section 16, step 2).
OperationResult runAppendedOperation(SectorDevice &dev, const OperationRequest &req,
                                     OperationResult res,
                                     const std::optional<QByteArray> &savedEntry)
{
    const Recognition &rec = res.report.recognition;
    const FirmwareState &fw = res.report.firmware;
    const RoomPlan &plan = res.report.plan;
    const DirectoryEntry &os = fw.entryList.at(fw.osIndex);
    const qint64 osStart = fw.imageStart(os);
    Writer w(dev);

    // Reading, in memory only (section 14 steps 4 and 5, section 16 step 3).
    const qint64 appleBytes =
        req.op == Operation::Uninstall ? qint64(os.entryOffset) : plan.entryOffset;
    QByteArray apple;
    const IoResult read = readFirmwareBytes(dev, rec, osStart, appleBytes, apple);
    if (!read) {
        res.message = QStringLiteral("cannot read the firmware of the iPod: %1").arg(read.error);
        return res;
    }
    const quint32 appleSum = byteSum(apple);
    const int osIndex = fw.osIndex;

    if (req.op == Operation::Uninstall) {
        const IoResult io = writeDirectory(dev, w, rec, fw, [&](QByteArray &sector, int first) {
            if (savedEntry)
                sector.replace(first + osIndex * kDirectoryEntrySize, kDirectoryEntrySize,
                               *savedEntry);
            else
                setCleanEntry(sector, first, osIndex, quint32(appleBytes), appleSum);
        });
        if (!io)
            return failed(res, w, WriteStep::WriteDirectory,
                          QStringLiteral("could not write the directory"), io);
        res.ok = true;
        res.wroteAnything = true;
        return res;
    }

    const QByteArray &program = res.report.file.program;
    const quint32 entryOffset = quint32(plan.entryOffset);
    const quint32 newLen = entryOffset + quint32(program.size());
    const quint32 newSum = byteSum(program, appleSum);

    // Step 7, only for an update: back to a coherent clean state.
    if (req.op == Operation::Update) {
        const IoResult io = writeDirectory(dev, w, rec, fw, [&](QByteArray &sector, int first) {
            setCleanEntry(sector, first, osIndex, entryOffset, appleSum);
        });
        if (!io)
            return failed(res, w, WriteStep::RemoveBootloader,
                          QStringLiteral("could not remove the old bootloader"), io);
    }

    // Step 8, only when images must move.
    if (plan.moveDelta > 0) {
        IoResult io = moveImages(dev, w, rec, plan, res.report.backupPath, req.pieceBytes);
        if (!io)
            return failed(res, w, WriteStep::MoveImages,
                          QStringLiteral("could not move the images"), io);
        io = writeDirectory(dev, w, rec, fw, [&](QByteArray &sector, int first) {
            for (int i = osIndex + 1; i < fw.entryList.size(); ++i) {
                put32le(sector, first + i * kDirectoryEntrySize + kEntryDevOffsetOffset,
                        quint32(fw.entryList.at(i).devOffset + plan.moveDelta));
            }
        });
        if (!io)
            return failed(res, w, WriteStep::MoveDirectory,
                          QStringLiteral("could not write the directory after moving the images"),
                          io);
    }

    // Step 9: the program and its zero padding, built in memory.
    QByteArray data = program;
    data.append(QByteArray(int(plan.dataEnd - plan.dataStart) - program.size(), '\0'));
    IoResult io = w.write(rec.firmwareStart + plan.dataStart, data);
    if (!io)
        return failed(res, w, WriteStep::WriteProgram,
                      QStringLiteral("could not write the bootloader"), io);

    // Step 10: four fields of the OSOS entry.
    io = writeDirectory(dev, w, rec, fw, [&](QByteArray &sector, int first) {
        const int at = first + osIndex * kDirectoryEntrySize;
        put32le(sector, at + kEntryLengthOffset, newLen);
        put32le(sector, at + kEntryEntryOffsetOffset, entryOffset);
        put32le(sector, at + kEntryChecksumOffset, newSum);
        put32le(sector, at + kEntryFlashOffset, kFlashAddressUnused);
    });
    if (!io)
        return failed(res, w, WriteStep::WriteDirectory,
                      QStringLiteral("could not write the directory"), io);

    res.ok = true;
    res.wroteAnything = true;
    return res;
}

// Section 18, from the disk and not from memory: the state, then the sums.
IoResult verifyOperation(SectorDevice &dev, const OperationRequest &req, const OperationResult &res)
{
    const Recognition rec = recognise(dev);
    const FirmwareState fw = readFirmware(dev, rec);
    const State expected = req.op == Operation::Uninstall ? State::Clean : State::Installed;
    if (fw.state != expected)
        return IoResult::failure(QStringLiteral("the state read after the operation is not the "
                                                "one expected"));
    if (fw.osIndex < 0)
        return IoResult::failure(QStringLiteral("the directory has no OSOS entry"));
    const DirectoryEntry &os = fw.entryList.at(fw.osIndex);
    const RoomPlan &plan = res.report.plan;
    const bool nano = fw.isNano2g();

    // The 2nd generation nano keeps the hash block in front of the entry's image.
    const bool newImage = nano && req.op != Operation::Uninstall;
    const qint64 start = newImage ? plan.dataStart : fw.imageStart(os);
    const qint64 length = newImage ? res.report.file.program.size() : qint64(os.length);
    if (newImage && fw.imageStart(os) - kHashBlockBytes != start)
        return IoResult::failure(QStringLiteral("the new image is not where it was written"));
    QByteArray image;
    const IoResult io = readFirmwareBytes(dev, rec, start, length, image);
    if (!io)
        return io;
    if (byteSum(image) != os.checksum)
        return IoResult::failure(QStringLiteral("the checksum of the image is not the one of the "
                                                "directory"));
    if (req.op == Operation::Uninstall)
        return IoResult::success();
    const QByteArray &program = res.report.file.program;
    const qint64 programAt = nano ? 0 : qint64(os.entryOffset);
    if (!nano && programAt != plan.entryOffset)
        return IoResult::failure(
            QStringLiteral("the entry offset is not the one that was written"));
    if (image.mid(int(programAt)) != program)
        return IoResult::failure(QStringLiteral("the image does not hold the bootloader"));
    return IoResult::success();
}

// Section 16 step 2: the entry saved by the install record of this iPod, only if
// Apple's firmware on the disk still gives its checksum and the `devOffset` matches.
std::optional<QByteArray> usableSavedEntry(SectorDevice &dev, const OperationRequest &req,
                                           const Recognition &rec, const FirmwareState &fw)
{
    if (req.recordDir.isEmpty())
        return std::nullopt;
    const std::optional<InstallRecord> r =
        findInstallRecord(req.recordDir, fw.target, req.serial, dev.sizeBytes());
    if (!r || r->osEntryBefore.size() != kDirectoryEntrySize)
        return std::nullopt;
    const QByteArray &e = r->osEntryBefore;
    const DirectoryEntry &os = fw.entryList.at(fw.osIndex);
    const quint32 len = get32le(e, kEntryLengthOffset);
    if (e.mid(kEntryTypeOffset, 4) != QByteArray(kTagOsos.data(), 4) ||
        get32le(e, kEntryDevOffsetOffset) != os.devOffset ||
        get32le(e, kEntryEntryOffsetOffset) != 0 || len == 0 || len > os.entryOffset)
        return std::nullopt;
    QByteArray apple;
    if (!readFirmwareBytes(dev, rec, fw.imageStart(os), len, apple))
        return std::nullopt;
    if (byteSum(apple) != get32le(e, kEntryChecksumOffset))
        return std::nullopt;
    return e;
}

QString operationName(Operation op)
{
    switch (op) {
    case Operation::Install:
        return QStringLiteral("install");
    case Operation::Update:
        return QStringLiteral("update");
    case Operation::Uninstall:
        break;
    }
    return QStringLiteral("uninstall");
}

// Section 19: after a failed step, reads the state again and decides what the
// app does next. The images that were half moved are put back at once.
void settleAfterFailure(SectorDevice &dev, const OperationRequest &req, OperationResult &res)
{
    if (res.failedStep == WriteStep::MoveImages || res.failedStep == WriteStep::MoveDirectory) {
        // The copy overwrites the old place of the images, so the directory does
        // not describe the disk any more, whether or not its own write went through.
        const RestoreResult back =
            restoreBackup(dev, req.scan, res.report.backupPath, req.pieceBytes);
        res.restoredBackup = back.ok;
        res.next = back.ok ? NextAction::Restored : NextAction::RestoreFailed;
        if (back.ok) {
            res.message += QStringLiteral("; the backup was written back");
        } else {
            res.message += QStringLiteral("; the backup could not be written back (%1). The iPod "
                                          "may not start: use Disk Mode and keep the backup file")
                               .arg(back.message);
        }
    }
    const FirmwareState now = readFirmware(dev, recognise(dev));
    res.stateAfter = now.state;
    if (res.next != NextAction::None)
        return;
    const bool coherent = now.state == State::Clean || now.state == State::Installed;
    const bool dataStep =
        res.failedStep == WriteStep::WriteProgram ||
        (req.op == Operation::Update && res.failedStep != WriteStep::RemoveBootloader &&
         res.failedStep != WriteStep::Verify);
    if (coherent && now.state == State::Clean && dataStep)
        res.next = NextAction::OfferRetry;
    else if (coherent)
        res.next = NextAction::ReportState;
    else
        res.next = NextAction::OfferRestore;
}

} // namespace

RestoreResult restoreBackup(SectorDevice &dev, const ScanSnapshot &scan, const QString &backupPath,
                            qint64 pieceBytes)
{
    const auto refuse = [](const QString &why) { return RestoreResult{false, why}; };
    if (!scan.valid || dev.sectorSize() != scan.sectorSize)
        return refuse(QStringLiteral("the disk is not the one that was scanned"));
    QByteArray sector0;
    if (!readBytes(dev, 0, scan.sector0.size(), sector0) || sector0 != scan.sector0)
        return refuse(QStringLiteral("sector 0 of the disk is not the one that was scanned"));
    QFile backup(backupPath);
    if (!backup.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("the backup cannot be read"));
    if (backup.size() != scan.firmwareBytes)
        return refuse(
            QStringLiteral("the backup does not have the size of the firmware partition"));
    // The banner and the volume header (the first 512 bytes of the partition: the
    // directory starts after them and is the only thing an operation writes there)
    // of the backup, the disk and the scan must all be the same.
    const int headBytes = qMin<int>(kVolumeHeaderBytes, scan.firmwareSector.size());
    const QByteArray head = backup.read(headBytes);
    QByteArray onDisk;
    if (!bannerMatches(head.left(kBannerBytes)) ||
        !readBytes(dev, scan.firmwareStart, headBytes, onDisk) || onDisk != head ||
        head != scan.firmwareSector.left(headBytes))
        return refuse(QStringLiteral("the banner of the backup is not the one of the disk"));

    const int ss = dev.sectorSize();
    const qint64 piece = qMax<qint64>(ss, (pieceBytes > 0 ? pieceBytes : kPieceBytes) / ss * ss);
    Writer w(dev);
    for (qint64 done = 0; done < scan.firmwareBytes;) {
        const qint64 n = qMin(piece, scan.firmwareBytes - done);
        if (!backup.seek(done))
            return refuse(QStringLiteral("the backup cannot be read"));
        const QByteArray part = backup.read(n);
        if (part.size() != n)
            return refuse(QStringLiteral("the backup cannot be read"));
        const IoResult io = w.write(scan.firmwareStart + done, part);
        if (!io)
            return refuse(io.error);
        done += n;
    }
    return {true, QString()};
}

OperationResult runOperation(SectorDevice &dev, const OperationRequest &req)
{
    OperationResult res;
    // Section 12: nothing is installed on a Mac iPod, the conversion of section 21
    // comes first (section 24, rule 6).
    if (req.scan.valid && recognise(dev).isMac()) {
        res.message = QStringLiteral("Formatted for Mac: convert the iPod to FAT32 first");
        return res;
    }
    res.report = runChecks(dev, req);
    if (!res.report.passed) {
        res.message = res.report.message;
        return res;
    }
    const Recognition &rec = res.report.recognition;
    const FirmwareState &fw = res.report.firmware;

    // The entry as it is before the operation, for the record (section 18).
    QByteArray entryBefore;
    const IoResult beforeRead =
        readFirmwareBytes(dev, rec, fw.directoryStart + qint64(fw.osIndex) * kDirectoryEntrySize,
                          kDirectoryEntrySize, entryBefore);
    if (!beforeRead) {
        res.message =
            QStringLiteral("cannot read the directory of the iPod: %1").arg(beforeRead.error);
        return res;
    }

    std::optional<QByteArray> saved;
    if (req.op == Operation::Uninstall && !fw.isNano2g())
        saved = usableSavedEntry(dev, req, rec, fw);
    res = fw.isNano2g() ? runNanoOperation(dev, req, res)
                        : runAppendedOperation(dev, req, res, saved);
    res.usedRecord = saved.has_value();
    if (!res.ok) {
        res.usedRecord = false;
        settleAfterFailure(dev, req, res);
        return res;
    }

    const IoResult verified = verifyOperation(dev, req, res);
    if (!verified) {
        res.ok = false;
        res.usedRecord = false;
        res.failedStep = WriteStep::Verify;
        res.message = QStringLiteral("the check after the writes failed: %1").arg(verified.error);
        settleAfterFailure(dev, req, res);
        return res;
    }
    res.stateAfter = req.op == Operation::Uninstall ? State::Clean : State::Installed;

    if (!req.recordDir.isEmpty()) {
        InstallRecord record;
        record.date = req.now;
        record.operation = operationName(req.op);
        record.target = fw.target;
        record.serial = req.serial;
        record.diskBytes = dev.sizeBytes();
        record.osEntryBefore = entryBefore;
        if (req.op != Operation::Uninstall) {
            record.bootloaderVersion = req.bootloaderVersion;
            record.bootloaderChecksum = res.report.file.headerChecksum;
        }
        record.delta = res.report.plan.moveDelta;
        record.backupFile = QFileInfo(res.report.backupPath).fileName();
        if (!saveRecord(record, req.recordDir, res.recordPath, res.recordMessage))
            res.recordPath.clear();
    }
    return res;
}

} // namespace tacetos::installer
