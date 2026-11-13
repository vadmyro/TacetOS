// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "convert.h"

#include <QDir>

namespace tacetos::installer {

namespace {

constexpr int kSector = 512;

void put32le(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xFF);
}

ConvertResult failed(ConvertResult r, const QString &why)
{
    r.ok = false;
    r.message = why;
    return r;
}

ConvertPermission refuse(ConvertPermission p, const QString &why)
{
    p.allowed = false;
    p.message = why;
    return p;
}

} // namespace

QByteArray buildDosSector0(const Slot &firmware, const Slot &music)
{
    QByteArray s(kSector, '\0');
    const Slot entries[kDosTableEntries] = {firmware, music, Slot(), Slot()};
    for (int i = 0; i < kDosTableEntries; ++i) {
        const int at = kDosTableOffset + i * kDosEntrySize;
        s[at + 4] = char(entries[i].type);
        put32le(s, at + 8, entries[i].firstSector);
        put32le(s, at + 12, entries[i].sectorCount);
    }
    s[kDosSignatureOffset] = char(kDosSignature0);
    s[kDosSignatureOffset + 1] = char(kDosSignature1);
    return s;
}

bool dosTableIsCoherent(const Slot &firmware, const Slot &music, qint64 diskSectors, QString &why)
{
    const qint64 firmwareEnd = qint64(firmware.firstSector) + firmware.sectorCount;
    const qint64 musicEnd = qint64(music.firstSector) + music.sectorCount;
    if (firmware.firstSector == 0 || firmware.sectorCount == 0)
        why = QStringLiteral("the firmware partition starts at sector 0, or is empty");
    else if (music.sectorCount == 0 || qint64(music.firstSector) < firmwareEnd)
        why = QStringLiteral("the music partition does not start after the firmware partition");
    else if (musicEnd > diskSectors)
        why = QStringLiteral("the music partition ends outside the disk");
    else
        return true;
    return false;
}

ConvertPermission checkConvertAllowed(SectorDevice &dev, bool slotUnmounted)
{
    ConvertPermission p;
    p.recognition = recognise(dev);
    if (p.recognition.verdict != Verdict::Recognised)
        return refuse(p, p.recognition.message);
    if (!p.recognition.isMac())
        return refuse(p, QStringLiteral("This iPod is formatted for Windows: there is nothing to "
                                        "convert"));
    p.firmware = readFirmware(dev, p.recognition);
    if (p.firmware.state != State::Clean)
        return refuse(p, QStringLiteral("This iPod's firmware partition is not in a state TacetOS "
                                        "converts"));
    if (p.recognition.sectorSize != kSector || dev.sectorSize() != kSector)
        return refuse(p, QStringLiteral("Only iPods with sectors of 512 bytes can be converted"));
    const QList<Slot> &slotList = p.recognition.table.slotList;
    if (slotList.size() != 2 || slotList.at(0).type != kDosTypeFirmware ||
        slotList.at(1).type != kTypeHfs)
        return refuse(p, QStringLiteral("The partition map of this iPod is not the one of a Mac "
                                        "iPod TacetOS knows"));
    const Slot firmware = slotList.at(0);
    Slot music = slotList.at(1);
    music.type = kDosTypeFat32;
    QString why;
    if (!dosTableIsCoherent(firmware, music, dev.sectorCount(), why))
        return refuse(p, QStringLiteral("The partition map does not fit on the disk"));
    p.geometry = computeFatGeometry(music.sectorCount);
    if (!p.geometry.ok)
        return refuse(p, p.geometry.message);
    if (!slotUnmounted)
        return refuse(p, QStringLiteral("Close the programs using the iPod"));
    p.allowed = true;
    p.firmwareSlot = firmware;
    p.musicSlot = music;
    return p;
}

ConvertResult convertMacToFat32(SectorDevice &dev, const ConvertRequest &request)
{
    ConvertResult r;
    if (dev.isRealDisk() && !request.allowRealWrites)
        return failed(r, QStringLiteral("This build does not write to a real iPod"));
    if (!confirmationMatches(request.ipodName, request.typedName))
        return failed(r, QStringLiteral("Type the name of the iPod to confirm the erase"));
    QString why;
    if (!diskMatchesScan(dev, request.scan, why))
        return failed(r, QStringLiteral("The iPod was unplugged"));
    const ConvertPermission p = checkConvertAllowed(dev, request.slotUnmounted);
    if (!p.allowed)
        return failed(r, p.message);

    // Step 1: the firmware partition and the map, in the data folder.
    const QDir dir(request.backupDir);
    if (request.backupDir.isEmpty() || !QDir().mkpath(dir.absolutePath()))
        return failed(r, QStringLiteral("Cannot save a backup in the data folder"));
    const QString name = backupFileName(p.recognition.target, request.now);
    const QString map =
        QStringLiteral("map-%1").arg(name.mid(int(QStringLiteral("firmware-").size())));
    const QString firmwarePath = freeFilePath(dir.absolutePath(), name);
    if (!copyDiskRange(dev, p.recognition.firmwareStart, p.recognition.firmwareBytes, firmwarePath,
                       why, request.backupFault))
        return failed(r, QStringLiteral("Cannot save a backup in the data folder"));
    const QString mapPath = freeFilePath(dir.absolutePath(), map);
    if (!copyDiskRange(dev, 0, p.recognition.firmwareStart, mapPath, why, request.backupFault))
        return failed(r, QStringLiteral("Cannot save a backup in the data folder"));
    r.backupPath = firmwarePath;
    r.mapBackupPath = mapPath;

    // Steps 2 to 4 are in the permission: the new sector 0 is built here.
    const QByteArray sector0 = buildDosSector0(p.firmwareSlot, p.musicSlot);

    // Step 5: the file system first, in slot 2.
    r.format = writeFat32(dev, p.musicSlot.firstSector, p.musicSlot.sectorCount, request);
    r.wroteAnything = r.format.wroteAnything;
    if (!r.format.ok)
        return failed(r, r.format.message);
    r.formatted = true;

    // Step 6: the table, last. From here the disk is a Windows iPod.
    r.wroteAnything = true;
    IoResult io = dev.writeSectors(0, sector0);
    if (io)
        io = dev.flush();
    if (!io)
        return failed(r, QStringLiteral("The conversion did not finish: %1").arg(io.error));
    QByteArray back;
    io = dev.readSectors(0, 1, back);
    if (!io || back != sector0)
        return failed(r,
                      QStringLiteral("The conversion did not finish: the table was not written"));
    r.tableWritten = true;

    // Step 7: recognised again, as a Windows iPod of the same model.
    r.after = recognise(dev);
    if (r.after.verdict != Verdict::Recognised || r.after.isMac() ||
        r.after.target != p.recognition.target ||
        r.after.firmwareStart != p.recognition.firmwareStart ||
        r.after.firmwareBytes != p.recognition.firmwareBytes)
        return failed(r, QStringLiteral("The conversion did not give a Windows iPod"));
    r.ok = true;
    return r;
}

} // namespace tacetos::installer
