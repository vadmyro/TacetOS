// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "firmware.h"

#include "constants.h"

namespace tacetos::installer {

namespace {

quint32 le32(const QByteArray &b, int at)
{
    quint32 v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | quint8(b.at(at + i));
    return v;
}

EntryKind kindOf(const QByteArray &tag)
{
    if (tag == QByteArray(kTagOsos.data(), 4))
        return EntryKind::Osos;
    if (tag == QByteArray(kTagRsrc.data(), 4))
        return EntryKind::Rsrc;
    if (tag == QByteArray(kTagAupd.data(), 4))
        return EntryKind::Aupd;
    if (tag == QByteArray(kTagOsbk.data(), 4))
        return EntryKind::Osbk;
    if (tag == QByteArray(kTagHibe.data(), 4))
        return EntryKind::Hibe;
    return EntryKind::Other;
}

FirmwareState notRead(FirmwareState s, const QString &why)
{
    s.state = State::NotRead;
    s.message = why;
    return s;
}

FirmwareState inconsistent(FirmwareState s, int check, const char *text)
{
    s.state = State::Inconsistent;
    s.failingCheck = check;
    s.message = QStringLiteral("check %1: %2").arg(check).arg(QString::fromLatin1(text));
    return s;
}

// Check 2 of section 11, on the entries alone. Null text when it passes.
const char *structureProblem(const FirmwareState &s)
{
    if (s.formatVersion != 2 && s.formatVersion != 3)
        return "the format version is not 2 or 3";

    const bool nano = s.isNano2g();
    const bool hasBackup = s.osbkIndex >= 0;
    // An install on the 2nd generation nano adds one entry.
    const qint64 count = s.entryList.size() + (nano && !hasBackup ? 1 : 0);
    const qint64 directoryBytes = count * kDirectoryEntrySize;
    if (s.directoryStart + directoryBytes > s.firmwareBytes)
        return "the directory is not inside the firmware partition";
    // Every change of the directory must be one sector write (section 13, rule 4).
    if (s.directoryStart % s.sectorSize + directoryBytes > s.sectorSize)
        return "the directory does not fit in one sector";

    int osos = 0;
    int osbk = 0;
    for (const DirectoryEntry &e : s.entryList) {
        osos += e.kind == EntryKind::Osos;
        osbk += e.kind == EntryKind::Osbk;
    }
    if (osos == 0)
        return "the directory holds no OSOS entry";
    if (osos > 1)
        return "the directory holds more than one OSOS entry";
    if (osbk > 1)
        return "the directory holds more than one OSBK entry";

    const int last = s.entryList.size() - 1;
    if (!nano) {
        if (hasBackup)
            return "the directory holds an OSBK entry on a model that does not use one";
        if (s.osIndex != 0)
            return "the OSOS entry is not the first one";
    } else if (!hasBackup) {
        if (s.osIndex != 0)
            return "the OSOS entry is not the first one";
    } else {
        if (s.osbkIndex != 0)
            return "the OSBK entry is not the first one";
        if (s.osIndex != last)
            return "the OSOS entry is not the last one";
    }
    return nullptr;
}

// Check 3 of section 11, and the offsets of section 9.
const char *entryProblem(const FirmwareState &s)
{
    for (const DirectoryEntry &e : s.entryList) {
        if (s.imageStart(e) + e.paddedBytes(s.sectorSize) > s.firmwareBytes)
            return "an entry ends outside the firmware partition";
    }
    for (int i = 1; i < s.entryList.size(); ++i) {
        const DirectoryEntry &before = s.entryList.at(i - 1);
        const DirectoryEntry &e = s.entryList.at(i);
        if (e.devOffset < before.devOffset)
            return "the entries are not in increasing order";
        if (qint64(e.devOffset) < qint64(before.devOffset) + before.paddedBytes(s.sectorSize))
            return "entries overlap";
    }
    // Section 9: the bootloader of an appended install starts on a sector, inside
    // the image. That it is not lower than the length of Apple's firmware cannot be
    // read from the directory: the length of Apple's firmware is not stored.
    const DirectoryEntry &os = s.entryList.at(s.osIndex);
    if (os.entryOffset != 0 && os.entryOffset >= os.length)
        return "entryOffset is not lower than len";
    if (os.entryOffset % quint32(s.sectorSize) != 0)
        return "entryOffset is not a multiple of the sector size";
    return nullptr;
}

} // namespace

qint64 DirectoryEntry::paddedBytes(int sectorSize) const
{
    return roundUpToSector(length, sectorSize);
}

QString entryKindName(EntryKind kind)
{
    switch (kind) {
    case EntryKind::Osos:
        return QStringLiteral("OSOS");
    case EntryKind::Rsrc:
        return QStringLiteral("RSRC");
    case EntryKind::Aupd:
        return QStringLiteral("AUPD");
    case EntryKind::Osbk:
        return QStringLiteral("OSBK");
    case EntryKind::Hibe:
        return QStringLiteral("HIBE");
    case EntryKind::Other:
        break;
    }
    return QStringLiteral("other");
}

qint64 roundUpToSector(qint64 length, int sectorSize)
{
    return (length + sectorSize - 1) / sectorSize * sectorSize;
}

bool writeDataFits(qint64 appleBytes, qint64 bootloaderBytes, int sectorSize)
{
    if (appleBytes < 0 || bootloaderBytes < 0 || sectorSize <= 0)
        return false;
    return roundUpToSector(appleBytes, sectorSize) + roundUpToSector(bootloaderBytes, sectorSize) <=
           kMaxWriteBytes;
}

FirmwareState readFirmware(SectorDevice &dev, const Recognition &rec)
{
    FirmwareState s;
    if (rec.verdict != Verdict::Recognised)
        return notRead(s, rec.message);
    s.sectorSize = rec.sectorSize;
    s.formatVersion = rec.formatVersion;
    s.target = rec.target;
    s.directoryStart = rec.directoryStart;
    s.firmwareBytes = rec.firmwareBytes;

    // Section 7.2: at most 10 entries, none further than 400 bytes from the start.
    const qint64 length = qMin<qint64>(kDirectoryMaxBytes, rec.firmwareBytes - rec.directoryStart);
    QByteArray raw;
    const IoResult io = readBytes(dev, rec.firmwareStart + rec.directoryStart, length, raw);
    if (!io)
        return notRead(s, io.error);
    for (int i = 0; i < kDirectoryMaxEntries; ++i) {
        const int at = i * kDirectoryEntrySize;
        if (at + kDirectoryEntrySize > raw.size())
            break;
        const QByteArray device = raw.mid(at, 4);
        if (device != QByteArray(kDeviceTagAta.data(), 4) &&
            device != QByteArray(kDeviceTagNan.data(), 4))
            break;
        DirectoryEntry e;
        e.deviceTag = device;
        e.typeTag = raw.mid(at + kEntryTypeOffset, 4);
        e.kind = kindOf(e.typeTag);
        e.imageNumber = le32(raw, at + 8);
        e.devOffset = le32(raw, at + 12);
        e.length = le32(raw, at + 16);
        e.loadAddress = le32(raw, at + 20);
        e.entryOffset = le32(raw, at + 24);
        e.checksum = le32(raw, at + 28);
        e.version = le32(raw, at + kEntryVersionOffset);
        e.flashAddress = le32(raw, at + 36);
        if (e.kind == EntryKind::Osos && s.osIndex < 0)
            s.osIndex = s.entryList.size();
        if (e.kind == EntryKind::Osbk && s.osbkIndex < 0)
            s.osbkIndex = s.entryList.size();
        s.entryList.append(e);
    }

    // Section 7.3: one sector after the start of the partition, except for a 3rd
    // generation partition with more than one image.
    s.imageArea = s.formatVersion == 2 && s.entryList.size() > 1 ? 0 : s.sectorSize;

    if (const char *why = structureProblem(s))
        return inconsistent(s, 2, why);
    if (const char *why = entryProblem(s))
        return inconsistent(s, 3, why);

    // Section 7.4 and 7.5: how to tell that the bootloader is there.
    const bool installed =
        s.isNano2g() ? s.osbkIndex >= 0 : s.entryList.at(s.osIndex).entryOffset != 0;
    s.state = installed ? State::Installed : State::Clean;
    return s;
}

} // namespace tacetos::installer
