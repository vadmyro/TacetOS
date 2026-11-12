// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "format.h"

#include <QMap>

namespace tacetos::installer {

namespace {

constexpr int kSector = 512;
constexpr quint64 kGiB = 1024ull * 1024 * 1024;
// Section 20.3: the characters that a volume label may not hold.
constexpr QLatin1String kBadLabelChars("\"*+,./:;<=>?[\\]|");

void put16le(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 2; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xFF);
}

void put32le(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xFF);
}

// Section 20.2: one FAT must hold the 2 first entries and one entry per cluster.
bool fatHolds(qint64 total, qint64 spc, qint64 fatSize)
{
    const qint64 data = total - kFatReservedSectors - kFatCount * fatSize;
    if (data < 0)
        return false;
    return fatSize * (kSector / 4) >= data / spc + 2;
}

// The label as 11 bytes, upper case, padded with spaces. Empty when it is not a
// label (too long, a character a label may not hold).
QByteArray labelBytes(const QString &label)
{
    if (label.isEmpty() || label.size() > 11)
        return QByteArray();
    QByteArray out;
    for (const QChar ch : label.toUpper()) {
        const ushort c = ch.unicode();
        if (c < 0x20 || c > 0x7E || kBadLabelChars.contains(ch))
            return QByteArray();
        out.append(char(c));
    }
    return out.leftJustified(11, ' ');
}

QByteArray buildBootSector(const FatGeometry &g, quint32 hidden, const QByteArray &label,
                           quint32 serial, const FormatOptions &o)
{
    QByteArray s(kSector, '\0');
    s[0] = char(0xEB);
    s[1] = char(0x58);
    s[2] = char(0x90);
    s.replace(3, 8, "MSWIN4.1");
    put16le(s, 11, kSector);
    s[13] = char(g.sectorsPerCluster);
    put16le(s, 14, kFatReservedSectors);
    s[16] = char(kFatCount);
    s[21] = char(0xF8);
    put16le(s, 24, quint32(o.sectorsPerTrack));
    put16le(s, 26, quint32(o.heads));
    put32le(s, 28, hidden);
    put32le(s, 32, g.total);
    put32le(s, 36, g.fatSize);
    put32le(s, 44, 2);
    put16le(s, 48, 1);
    put16le(s, 50, kFatBootCopySector);
    s[64] = char(0x80);
    s[66] = char(0x29);
    put32le(s, 67, serial);
    s.replace(71, 11, label);
    s.replace(82, 8, "FAT32   ");
    s[510] = char(0x55);
    s[511] = char(0xAA);
    return s;
}

QByteArray buildFsInfo(const FatGeometry &g)
{
    QByteArray s(kSector, '\0');
    put32le(s, 0, kFsInfoSignature0);
    put32le(s, 484, kFsInfoSignature1);
    put32le(s, 488, g.clusters - 1);
    put32le(s, 492, 3);
    put32le(s, 508, kFsInfoSignature2);
    return s;
}

QByteArray buildFatStart()
{
    QByteArray s(kSector, '\0');
    put32le(s, 0, kFatFirstEntry0);
    put32le(s, 4, kFatFirstEntry1);
    put32le(s, 8, kFatFirstEntry2);
    return s;
}

// Writes and flushes, and counts what went to the disk.
struct Counter
{
    SectorDevice &dev;
    FormatResult &result;
    IoResult write(qint64 first, const QByteArray &data)
    {
        result.wroteAnything = true;
        IoResult io = dev.writeSectors(first, data);
        if (io)
            io = dev.flush();
        if (!io)
            return io;
        result.bytesWritten += data.size();
        ++result.writeCount;
        return IoResult::success();
    }
};

FormatResult failed(FormatResult r, const QString &why)
{
    r.ok = false;
    r.message = why;
    return r;
}

} // namespace

int clusterBytesFor(qint64 total)
{
    const quint64 bytes = quint64(total) * kSector;
    if (bytes <= 8 * kGiB)
        return 4 * 1024;
    if (bytes <= 16 * kGiB)
        return 8 * 1024;
    if (bytes <= 32 * kGiB)
        return 16 * 1024;
    return 32 * 1024;
}

FatGeometry computeFatGeometry(qint64 total)
{
    FatGeometry g;
    g.message = QStringLiteral("This partition size is not supported");
    if (total <= kFatReservedSectors || total > 0xFFFFFFFFll)
        return g;
    const qint64 spc = clusterBytesFor(total) / kSector;
    qint64 fat =
        (4 * (total - kFatReservedSectors) + (kSector * spc + 8) - 1) / (kSector * spc + 8);
    while (!fatHolds(total, spc, fat))
        ++fat;
    const qint64 clusters = (total - kFatReservedSectors - kFatCount * fat) / spc;
    if (clusters < kFatMinClusters || clusters > kFatMaxClusters)
        return g;
    g.ok = true;
    g.message.clear();
    g.total = quint32(total);
    g.sectorsPerCluster = quint32(spc);
    g.fatSize = quint32(fat);
    g.clusters = quint32(clusters);
    return g;
}

quint32 volumeSerialFor(const QDateTime &when)
{
    const QDate d = when.date();
    const QTime t = when.time();
    const quint32 low = (quint32(d.month() << 8) | quint32(d.day())) +
                        (quint32(t.second() << 8) | quint32(t.msec() / 10));
    const quint32 high = quint32(d.year()) + (quint32(t.hour() << 8) | quint32(t.minute()));
    const quint32 serial = ((high & 0xFFFF) << 16) | (low & 0xFFFF);
    return serial != 0 ? serial : 1;
}

FormatResult writeFat32(SectorDevice &dev, quint32 firstSector, quint32 total,
                        const FormatOptions &options)
{
    FormatResult r;
    r.slot.type = kDosTypeFat32;
    r.slot.firstSector = firstSector;
    r.slot.sectorCount = total;
    if (dev.sectorSize() != kSector)
        return failed(r, QStringLiteral("Only sectors of 512 bytes can be formatted"));
    if (qint64(firstSector) + total > dev.sectorCount())
        return failed(r, QStringLiteral("The partition runs past the end of the disk"));
    const QByteArray label = labelBytes(options.label);
    if (label.isEmpty())
        return failed(r, QStringLiteral("This volume label is not allowed"));
    r.geometry = computeFatGeometry(total);
    if (!r.geometry.ok)
        return failed(r, r.geometry.message);
    const FatGeometry &g = r.geometry;
    r.serial = volumeSerialFor(options.now);

    const QByteArray boot = buildBootSector(g, firstSector, label, r.serial, options);
    const QByteArray fsinfo = buildFsInfo(g);
    const QByteArray fatStart = buildFatStart();
    // Sections 20.3 and 20.4: what is not zero after the zeroing, by sector of the
    // partition.
    QMap<qint64, QByteArray> known;
    known.insert(kFatBootCopySector, boot);
    known.insert(1, fsinfo);
    known.insert(kFatFsInfoCopySector, fsinfo);
    known.insert(g.firstFatSector(), fatStart);
    known.insert(g.secondFatSector(), fatStart);
    known.insert(0, boot);
    const qint64 areaSectors = g.rootSector() + g.sectorsPerCluster;

    Counter out{dev, r};
    const qint64 first = firstSector;

    // Step 1: zero the reserved sectors, both FATs and the root cluster.
    const qint64 pieceSectors =
        qMax<qint64>(1, (options.pieceBytes > 0 ? options.pieceBytes : kPieceBytes) / kSector);
    for (qint64 at = 0; at < areaSectors; at += pieceSectors) {
        const qint64 n = qMin(pieceSectors, areaSectors - at);
        const IoResult io = out.write(first + at, QByteArray(int(n * kSector), '\0'));
        if (!io)
            return failed(r, QStringLiteral("The format did not finish: %1").arg(io.error));
    }
    // Steps 2 and 3: the first sectors of the FATs, the FSInfo blocks and the copy
    // of the boot sector, then the boot sector last.
    const qint64 order[] = {g.firstFatSector(),   g.secondFatSector(), 1,
                            kFatFsInfoCopySector, kFatBootCopySector,  0};
    for (const qint64 sector : order) {
        const IoResult io = out.write(first + sector, known.value(sector));
        if (!io)
            return failed(r, QStringLiteral("The format did not finish: %1").arg(io.error));
    }

    // Read everything back, in pieces.
    for (qint64 at = 0; at < areaSectors; at += pieceSectors) {
        const qint64 n = qMin(pieceSectors, areaSectors - at);
        QByteArray back;
        const IoResult io = dev.readSectors(first + at, n, back);
        if (!io)
            return failed(
                r,
                QStringLiteral("The format did not give a readable partition: %1").arg(io.error));
        QByteArray expected(int(n * kSector), '\0');
        for (auto it = known.lowerBound(at); it != known.end() && it.key() < at + n; ++it)
            expected.replace(int((it.key() - at) * kSector), kSector, it.value());
        if (back != expected)
            return failed(r, QStringLiteral("The format did not give a readable partition"));
    }
    r.ok = true;
    return r;
}

FormatPermission checkFormatAllowed(SectorDevice &dev, bool slotUnmounted)
{
    FormatPermission p;
    auto refuse = [&p](const QString &why) {
        p.allowed = false;
        p.message = why;
        return p;
    };
    p.recognition = recognise(dev);
    if (p.recognition.verdict != Verdict::Recognised)
        return refuse(p.recognition.message);
    p.firmware = readFirmware(dev, p.recognition);
    if (p.firmware.state != State::Clean && p.firmware.state != State::Installed)
        return refuse(
            QStringLiteral("This iPod's firmware partition is not in a state TacetOS knows"));
    const PartitionTable &t = p.recognition.table;
    if (t.kind != TableKind::Dos)
        return refuse(QStringLiteral(
            "Formatted for Mac: the music partition is only erased by the conversion to FAT32"));
    if (t.slotList.size() < 2)
        return refuse(QStringLiteral("There is no music partition on this iPod"));
    const Slot &music = t.slotList.at(1);
    if (music.type != kDosTypeFat32 && music.type != kDosTypeFat32Lba)
        return refuse(QStringLiteral("The music partition is not a FAT32 partition"));
    if (p.recognition.sectorSize != kSector || dev.sectorSize() != kSector)
        return refuse(QStringLiteral("Only iPods with sectors of 512 bytes can be erased"));
    // Never write over the firmware partition, or outside the disk (section 24, rule 3).
    const Slot &firmware = t.slotList.at(0);
    const qint64 musicEnd = qint64(music.firstSector) + music.sectorCount;
    const qint64 firmwareEnd = qint64(firmware.firstSector) + firmware.sectorCount;
    if (music.sectorCount == 0 || musicEnd > dev.sectorCount() ||
        (music.firstSector < firmwareEnd && musicEnd > firmware.firstSector))
        return refuse(QStringLiteral("The music partition does not fit on the disk"));
    if (!slotUnmounted)
        return refuse(QStringLiteral("Close the programs using the iPod"));
    p.allowed = true;
    p.slot = music;
    return p;
}

bool confirmationMatches(const QString &shown, const QString &typed)
{
    return !shown.trimmed().isEmpty() && typed.trimmed() == shown.trimmed();
}

FormatResult formatMusicPartition(SectorDevice &dev, const FormatRequest &request)
{
    FormatResult r;
    if (dev.isRealDisk() && !request.allowRealWrites)
        return failed(r, QStringLiteral("This build does not write to a real iPod"));
    if (!confirmationMatches(request.ipodName, request.typedName))
        return failed(r, QStringLiteral("Type the name of the iPod to confirm the erase"));
    const FormatPermission p = checkFormatAllowed(dev, request.slotUnmounted);
    if (!p.allowed)
        return failed(r, p.message);
    return writeFat32(dev, p.slot.firstSector, p.slot.sectorCount, request);
}

} // namespace tacetos::installer
