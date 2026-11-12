// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "recognise.h"

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

quint16 le16(const QByteArray &b, int at)
{
    return quint16(quint8(b.at(at)) | (quint8(b.at(at + 1)) << 8));
}

Recognition stop(Recognition r, Verdict v, const QString &message)
{
    r.verdict = v;
    r.message = message;
    return r;
}

const QString kLayoutMessage = QStringLiteral("partition layout is not the one of an iPod");
const QString kBadDirectoryMessage = QStringLiteral("bad firmware directory");

struct ModelRow
{
    quint32 value;
    const char *target;
};

// Section 5. Kept in step with `bootloader_target` in models/models.toml.
const ModelRow kModelRows[] = {
    {0x01, "ipod1g2g"},  {0x02, "ipod3g"},     {0x40, "ipodmini1g"},
    {0x50, "ipod4g"},    {0x60, "ipodcolor"},  {0x70, "ipodmini2g"},
    {0xB0, "ipodvideo"}, {0xC0, "ipodnano1g"}, {0x100, "ipodnano2g"},
};

} // namespace

bool passesPartitionTest(const QList<Slot> &slotList, bool forScan)
{
    if (slotList.size() < 2)
        return false;
    const Slot &fw = slotList.at(0);
    const Slot &music = slotList.at(1);
    if (fw.type != kDosTypeFirmware || fw.sectorCount == 0)
        return false;
    if (music.type != kDosTypeFat32 && music.type != kDosTypeFat32Lba && music.type != kTypeHfs)
        return false;
    return !(forScan && fw.firstSector == 0);
}

bool bannerMatches(const QByteArray &firstBytes)
{
    return firstBytes.size() >= kBannerLength && firstBytes.left(kBannerLength) == bannerBytes();
}

QString targetForModelValue(quint32 value)
{
    for (const ModelRow &row : kModelRows) {
        if (row.value == value)
            return QString::fromLatin1(row.target);
    }
    return QString();
}

Recognition recognise(SectorDevice &dev, bool forScan)
{
    Recognition r;
    r.sectorSize = dev.sectorSize();
    // Section 9: the padding arithmetic of the whole spec needs a power of two.
    if (r.sectorSize <= 0 || (r.sectorSize & (r.sectorSize - 1)) != 0)
        return stop(r, Verdict::BadSectorSize,
                    QStringLiteral("the sector size is not a power of two"));

    r.table = readPartitionTable(dev);
    switch (r.table.status) {
    case TableStatus::Ok:
        break;
    case TableStatus::UnknownTable:
        return stop(r, Verdict::UnknownTable, r.table.message);
    case TableStatus::BadMap:
        return stop(r, Verdict::PartitionTestFailed, kLayoutMessage);
    case TableStatus::ReadError:
        return stop(r, Verdict::ReadError, r.table.message);
    }

    if (!passesPartitionTest(r.table.slotList, forScan))
        return stop(r, Verdict::PartitionTestFailed, kLayoutMessage);

    // Section 4. A partition that does not lie inside the disk cannot be read.
    const Slot &fw = r.table.slotList.at(0);
    r.firmwareStart = fw.startBytes(r.sectorSize);
    r.firmwareBytes = fw.sizeBytes(r.sectorSize);
    if (r.firmwareStart + r.firmwareBytes > dev.sizeBytes())
        return stop(r, Verdict::PartitionTestFailed, kLayoutMessage);

    const QString noBanner =
        QStringLiteral("firmware partition does not carry the expected banner");
    if (r.firmwareBytes < kBannerLength)
        return stop(r, Verdict::NoBanner, noBanner);
    QByteArray head;
    const qint64 headBytes = qMin<qint64>(kFormatVersionOffset + 2, r.firmwareBytes);
    IoResult io = readBytes(dev, r.firmwareStart, headBytes, head);
    if (!io)
        return stop(r, Verdict::ReadError, io.error);
    if (!bannerMatches(head))
        return stop(r, Verdict::NoBanner, noBanner);

    // Section 7.1: the volume header and the place of the directory.
    if (head.size() < kFormatVersionOffset + 2 ||
        head.mid(kVolumeMagicOffset, 4) != QByteArray(kVolumeMagic.data(), 4))
        return stop(r, Verdict::BadDirectory, kBadDirectoryMessage);
    r.formatVersion = le16(head, kFormatVersionOffset);
    qint64 directory = qint64(le32(head, kDirectoryPointerOffset)) + kDirectoryPointerBase;
    if (directory + kDirectoryEntrySize > r.firmwareBytes)
        return stop(r, Verdict::BadDirectory, kBadDirectoryMessage);

    // Section 7.2: a zero first byte means the directory starts at the next sector.
    QByteArray first;
    io = readBytes(dev, r.firmwareStart + directory, 1, first);
    if (!io)
        return stop(r, Verdict::ReadError, io.error);
    if (first.at(0) == 0) {
        directory = (directory / r.sectorSize + 1) * r.sectorSize;
        if (directory + kDirectoryEntrySize > r.firmwareBytes)
            return stop(r, Verdict::BadDirectory, kBadDirectoryMessage);
    }
    r.directoryStart = directory;
    QByteArray entries;
    const qint64 length = qMin<qint64>(kDirectoryMaxBytes, r.firmwareBytes - directory);
    io = readBytes(dev, r.firmwareStart + directory, length, entries);
    if (!io)
        return stop(r, Verdict::ReadError, io.error);

    // Section 5: the model comes from the version of the OSOS entry.
    bool foundOsos = false;
    for (int i = 0; i < kDirectoryMaxEntries; ++i) {
        const int at = i * kDirectoryEntrySize;
        if (at + kDirectoryEntrySize > entries.size())
            break;
        const QByteArray device = entries.mid(at, 4);
        if (device != QByteArray(kDeviceTagAta.data(), 4) &&
            device != QByteArray(kDeviceTagNan.data(), 4))
            break;
        if (entries.mid(at + kEntryTypeOffset, 4) == QByteArray(kTagOsos.data(), 4)) {
            r.osVersion = le32(entries, at + kEntryVersionOffset);
            foundOsos = true;
            break;
        }
    }
    if (!foundOsos)
        return stop(r, Verdict::BadDirectory, kBadDirectoryMessage);

    r.modelValue = r.osVersion >> 8;
    r.target = targetForModelValue(r.modelValue);
    if (r.target.isEmpty())
        return stop(r, Verdict::UnknownGeneration, QStringLiteral("an iPod we do not support"));
    r.verdict = Verdict::Recognised;
    return r;
}

} // namespace tacetos::installer
