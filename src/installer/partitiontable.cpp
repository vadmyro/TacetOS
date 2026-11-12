// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "partitiontable.h"

#include "constants.h"

namespace tacetos::installer {

namespace {

quint32 be16(const QByteArray &b, int at)
{
    return (quint32(quint8(b.at(at))) << 8) | quint8(b.at(at + 1));
}

quint32 be32(const QByteArray &b, int at)
{
    return (be16(b, at) << 16) | be16(b, at + 2);
}

quint32 le32(const QByteArray &b, int at)
{
    quint32 v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | quint8(b.at(at + i));
    return v;
}

PartitionTable fail(TableStatus status, const QString &message)
{
    PartitionTable t;
    t.status = status;
    t.message = message;
    return t;
}

PartitionTable badMap(const QString &why)
{
    return fail(TableStatus::BadMap, QStringLiteral("partition map: %1").arg(why));
}

PartitionTable readDos(const QByteArray &sector0)
{
    PartitionTable t;
    t.status = TableStatus::Ok;
    t.kind = TableKind::Dos;
    for (int i = 0; i < kDosTableEntries; ++i) {
        const int at = kDosTableOffset + i * kDosEntrySize;
        Slot s;
        s.type = quint8(sector0.at(at + 4));
        s.firstSector = le32(sector0, at + 8);
        s.sectorCount = le32(sector0, at + 12);
        t.slotList.append(s);
    }
    return t;
}

PartitionTable readApm(SectorDevice &dev, const QByteArray &sector0)
{
    const qint64 blockSize = be16(sector0, kApmBlockSizeOffset);
    // Section 2.2: a block holds at least the 80 bytes of an entry, and 512 on
    // every iPod seen so far.
    if (blockSize < 512)
        return badMap(QStringLiteral("the block size is %1").arg(blockSize));
    const int sector = dev.sectorSize();

    PartitionTable t;
    t.status = TableStatus::Ok;
    t.kind = TableKind::Apm;
    quint32 entryCount = 0;
    for (quint32 n = 1; n <= entryCount || n == 1; ++n) {
        const qint64 at = qint64(n) * blockSize;
        if (at + kApmEntryMinBytes > dev.sizeBytes())
            return badMap(QStringLiteral("entry %1 is outside the disk").arg(n));
        QByteArray e;
        const IoResult r = readBytes(dev, at, kApmEntryMinBytes, e);
        if (!r)
            return fail(TableStatus::ReadError, r.error);
        if (e.left(2) != QByteArray(kApmEntryMagic.data(), 2))
            break;
        if (n == 1) {
            entryCount = be32(e, kApmEntryCountOffset);
            if (entryCount == 0 || entryCount > quint32(kApmMaxBlocks))
                return badMap(QStringLiteral("the map says it has %1 entries").arg(entryCount));
        }
        // The type text, up to the zero padding: `Apple_HFSX` is not `Apple_HFS`.
        QByteArray type = e.mid(kApmTypeOffset, kApmTypeLength);
        const int end = type.indexOf('\0');
        if (end >= 0)
            type.truncate(end);
        Slot s;
        if (type == kApmTypeFirmware.data())
            s.type = kDosTypeFirmware;
        else if (type == kApmTypeHfs.data())
            s.type = kTypeHfs;
        else
            continue;
        // Through bytes: the map block is not always the sector.
        const qint64 firstBytes = qint64(be32(e, kApmFirstBlockOffset)) * blockSize;
        const qint64 sizeBytes = qint64(be32(e, kApmBlockCountOffset)) * blockSize;
        if (firstBytes % sector != 0 || sizeBytes % sector != 0 ||
            firstBytes / sector > 0xFFFFFFFFLL || sizeBytes / sector > 0xFFFFFFFFLL)
            return badMap(QStringLiteral("entry %1 does not fit the sectors").arg(n));
        s.firstSector = quint32(firstBytes / sector);
        s.sectorCount = quint32(sizeBytes / sector);
        if (t.slotList.size() < kMaxSlots)
            t.slotList.append(s);
    }
    return t;
}

} // namespace

PartitionTable readPartitionTable(SectorDevice &dev)
{
    QByteArray sector0;
    const IoResult r = readBytes(dev, 0, 512, sector0);
    if (!r)
        return fail(TableStatus::ReadError, r.error);
    if (quint8(sector0.at(kDosSignatureOffset)) == kDosSignature0 &&
        quint8(sector0.at(kDosSignatureOffset + 1)) == kDosSignature1)
        return readDos(sector0);
    if (sector0.left(2) == QByteArray(kApmHeaderMagic.data(), 2))
        return readApm(dev, sector0);
    return fail(TableStatus::UnknownTable, QStringLiteral("unknown partition table"));
}

} // namespace tacetos::installer
