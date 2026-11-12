// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "fakepod.h"

#include <QFile>
#include <QList>
#include <utility>

namespace tacetos::fakepod {

namespace {

// Section 5 (values), 7.1 (format version), 7.5 and 15 (sector size). The
// Classic is in no row of section 5 and has no firmware partition: its image
// is a single FAT32 partition in slot 1 on sectors of 4096 bytes, which fails
// the partition test of section 3, the right answer for an iPod whose install
// method is not `partition`. Its value, 0xFF, and format version are not used.
const ModelRow kModels[] = {
    {"ipod-1g2g", 0x01, 3, 512, true},  {"ipod-3g", 0x02, 2, 512, true},
    {"ipod-4g", 0x50, 3, 512, true},    {"ipod-color", 0x60, 3, 512, true},
    {"mini-1g", 0x40, 3, 512, true},    {"mini-2g", 0x70, 3, 512, true},
    {"ipod-video", 0xB0, 3, 512, true}, {"nano-1g", 0xC0, 3, 512, true},
    {"nano-2g", 0x100, 3, 2048, false}, {"classic-6g", 0xFF, 3, 4096, false, false},
};

constexpr int kEntrySize = 40;
constexpr int kHashBlock = 0x800;
constexpr int kDefaultBootloaderLength = 4000;
// Fixed values of the FAT32 format (section 20.3). The serial number is fixed
// so that two builds give the same bytes.
constexpr quint32 kSerial = 0x54414345;
constexpr int kReservedSectors = 32;
constexpr int kFatCount = 2;
constexpr qint64 kMinClusters = 65525;

void put16le(QByteArray &b, int at, quint32 v)
{
    b[at] = char(v & 0xFF);
    b[at + 1] = char((v >> 8) & 0xFF);
}

void put32le(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xFF);
}

void put16be(QByteArray &b, int at, quint32 v)
{
    b[at] = char((v >> 8) & 0xFF);
    b[at + 1] = char(v & 0xFF);
}

void put32be(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((v >> (8 * (3 - i))) & 0xFF);
}

void putText(QByteArray &b, int at, const char *text)
{
    b.replace(at, int(qstrlen(text)), text);
}

qint64 roundUp(qint64 n, qint64 unit)
{
    return (n + unit - 1) / unit * unit;
}

// A deterministic block that is not all zeros, starting with a readable tag.
QByteArray synthetic(int length, const char *tag, int seed)
{
    QByteArray b(length, '\0');
    for (int i = 0; i < length; ++i)
        b[i] = char((i * 131 + seed * 17 + (i >> 8)) & 0xFF);
    b.replace(0, int(qstrlen(tag)), tag);
    return b;
}

struct Entry
{
    // The 4 letters as stored in the file (backwards).
    const char *tag;
    // Where the data starts, in bytes from the start of the firmware partition.
    qint64 start;
    quint32 len;
    quint32 address;
    quint32 entryOffset;
    quint32 checksum;
    quint32 version;
    quint32 flash;
};

QByteArray entryBytes(const Entry &e, qint64 imageArea)
{
    QByteArray b(kEntrySize, '\0');
    putText(b, 0, "!ATA");
    b.replace(4, 4, e.tag, 4);
    put32le(b, 8, 0);
    put32le(b, 12, quint32(e.start - imageArea));
    put32le(b, 16, e.len);
    put32le(b, 20, e.address);
    put32le(b, 24, e.entryOffset);
    put32le(b, 28, e.checksum);
    put32le(b, 32, e.version);
    put32le(b, 36, e.flash);
    return b;
}

bool put(QFile &f, qint64 offset, const QByteArray &data)
{
    return f.seek(offset) && f.write(data) == data.size();
}

// The boot sector of section 20.3, with the sizes chosen for a tiny image.
struct Fat32
{
    int bytesPerSector = 512;
    int sectorsPerCluster = 1;
    quint32 total = 0;
    quint32 fatSize = 0;
    quint32 hidden = 0;
    quint32 clusters = 0;
};

quint32 clustersFor(quint32 total, quint32 fatSize, int spc)
{
    return quint32((qint64(total) - kReservedSectors - kFatCount * qint64(fatSize)) / spc);
}

// The smallest FAT size that holds an entry for every cluster plus the 2
// first ones (section 20.2).
quint32 fatSizeFor(quint32 total, int bps, int spc)
{
    qint64 fat = (4 * (qint64(total) - kReservedSectors) + (qint64(bps) * spc + 8) - 1) /
                 (qint64(bps) * spc + 8);
    while ((qint64(clustersFor(total, quint32(fat), spc)) + 2) * 4 > fat * bps)
        ++fat;
    return quint32(fat);
}

// Not the cluster table of section 20.2: an image of 64 MiB holds only
// 65 525 clusters with the smallest cluster, so the largest cluster that still
// gives that many is taken. A disk of 2048-byte sectors cannot reach it at
// all; it gets one sector per cluster and fewer clusters (section 20.1 says
// formatting is for 512-byte sectors only, so no rule exists for it).
Fat32 planFat32(quint32 total, int bps, quint32 hidden)
{
    Fat32 f;
    f.bytesPerSector = bps;
    f.total = total;
    f.hidden = hidden;
    f.sectorsPerCluster = 1;
    for (int spc = 128; spc >= 1; spc /= 2) {
        quint32 fat = fatSizeFor(total, bps, spc);
        if (qint64(clustersFor(total, fat, spc)) >= kMinClusters) {
            f.sectorsPerCluster = spc;
            break;
        }
    }
    f.fatSize = fatSizeFor(total, bps, f.sectorsPerCluster);
    f.clusters = clustersFor(total, f.fatSize, f.sectorsPerCluster);
    return f;
}

bool writeFat32(QFile &f, qint64 start, const Fat32 &p)
{
    const int bps = p.bytesPerSector;
    QByteArray boot(bps, '\0');
    boot[0] = char(0xEB);
    boot[1] = char(0x58);
    boot[2] = char(0x90);
    putText(boot, 3, "MSWIN4.1");
    put16le(boot, 11, quint32(bps));
    boot[13] = char(p.sectorsPerCluster);
    put16le(boot, 14, kReservedSectors);
    boot[16] = char(kFatCount);
    boot[21] = char(0xF8);
    put16le(boot, 24, 63);
    put16le(boot, 26, 255);
    put32le(boot, 28, p.hidden);
    put32le(boot, 32, p.total);
    put32le(boot, 36, p.fatSize);
    put32le(boot, 44, 2);
    put16le(boot, 48, 1);
    put16le(boot, 50, 6);
    boot[64] = char(0x80);
    boot[66] = char(0x29);
    put32le(boot, 67, kSerial);
    putText(boot, 71, "IPOD       ");
    putText(boot, 82, "FAT32   ");
    boot[510] = char(0x55);
    boot[511] = char(0xAA);

    QByteArray info(bps, '\0');
    put32le(info, 0, 0x41615252);
    put32le(info, 484, 0x61417272);
    put32le(info, 488, p.clusters - 1);
    put32le(info, 492, 3);
    put32le(info, 508, 0xAA550000);

    QByteArray fat(bps, '\0');
    put32le(fat, 0, 0x0FFFFFF8);
    put32le(fat, 4, 0x0FFFFFFF);
    put32le(fat, 8, 0x0FFFFFFF);

    // The data is written in the order of the spec, the boot sector last.
    return put(f, start + qint64(kReservedSectors) * bps, fat) &&
           put(f, start + (qint64(kReservedSectors) + p.fatSize) * bps, fat) &&
           put(f, start + 1 * qint64(bps), info) && put(f, start + 7 * qint64(bps), info) &&
           put(f, start + 6 * qint64(bps), boot) && put(f, start, boot);
}

// The volume header of HFS+, at 1024 bytes into the partition, and nothing
// else: it is a header for the readers of the tests, not a volume that a
// system can mount. All numbers are big-endian.
bool writeHfsHeader(QFile &f, qint64 start, qint64 bytes)
{
    constexpr quint32 kBlockSize = 4096;
    constexpr quint32 kDate = 3600000000u; // seconds since 1904, a fixed value
    QByteArray h(512, '\0');
    put16be(h, 0, 0x482B); // "H+"
    put16be(h, 2, 4);      // version
    put32be(h, 4, 0x100);  // attributes: cleanly unmounted
    putText(h, 8, "10.0"); // last mounted version
    for (int at : {16, 20, 24, 28})
        put32be(h, at, kDate);
    put32be(h, 40, kBlockSize);
    const quint32 blocks = quint32(bytes / kBlockSize);
    put32be(h, 44, blocks);
    put32be(h, 48, blocks - 1);
    put32be(h, 56, kBlockSize);
    put32be(h, 60, kBlockSize);
    put32be(h, 64, 16); // next catalog id: the first one free for a user
    return put(f, start + 1024, h);
}

QByteArray apmEntry(int number, int count, quint32 first, quint32 blocks, const char *name,
                    const char *type)
{
    QByteArray b(512, '\0');
    putText(b, 0, "PM");
    put32be(b, 4, quint32(count));
    put32be(b, 8, first);
    put32be(b, 12, blocks);
    putText(b, 16, name);
    putText(b, 48, type);
    put32be(b, 84, blocks); // size of the data, as in the partitions of a real map
    put32be(b, 88, number == 1 ? 3 : 0x33);
    return b;
}

} // namespace

uint32_t byteSum(const QByteArray &data)
{
    uint32_t sum = 0;
    for (char c : data)
        sum += uint8_t(c);
    return sum;
}

QStringList FakePod::modelIds()
{
    QStringList ids;
    for (const ModelRow &m : kModels)
        ids << QString::fromLatin1(m.id);
    return ids;
}

const ModelRow *FakePod::model(const QString &id)
{
    for (const ModelRow &m : kModels) {
        if (id == QLatin1String(m.id))
            return &m;
    }
    return nullptr;
}

QByteArray FakePod::defaultBootloader()
{
    return synthetic(kDefaultBootloaderLength, "FAKEBOOT", 5);
}

// Section 4: 16 rows of 16 characters, the last byte being zero. 7 rows of
// stop sign, each starting with "{{~~", the copyright text across the next 3
// rows, then dashes. The rows are those of the table of the section.
QByteArray FakePod::banner()
{
    static const char *const rows[16] = {
        "{{~~  /-----\\   ", "{{~~ /       \\  ", "{{~~|         | ",  "{{~~| S T O P | ",
        "{{~~|         | ",  "{{~~ \\       /  ", "{{~~  \\-----/   ", "Copyright(C) 200",
        "1 Apple Computer",  ", Inc.----------",  "----------------",  "----------------",
        "----------------",  "----------------",  "----------------",  "---------------",
    };
    QByteArray b;
    for (const char *row : rows)
        b.append(row, qstrlen(row));
    b.append('\0');
    return b;
}

namespace {

// The image of a model with no firmware partition (the Classic): the DOS
// table has one FAT32 partition, in slot 1, from the same byte offset as the
// firmware partition of the other images.
FakePodResult buildSinglePartition(const FakePodSpec &spec, const ModelRow &m)
{
    FakePodResult r;
    if (spec.bootloaderPresent || spec.withRsrc || spec.withAupd || spec.directoryLate) {
        r.error = QStringLiteral("%1 has no firmware partition").arg(QLatin1String(m.id));
        return r;
    }
    const qint64 S = m.sectorSize;
    const quint32 first = quint32(FakePod::kFirmwareStartSector * 512 / S);
    const quint32 sectors = quint32(FakePod::kImageBytes / S) - first;
    r.sectorSize = int(S);
    r.fileSize = FakePod::kImageBytes;
    r.musicStart = first * S;
    r.musicBytes = FakePod::kImageBytes - r.musicStart;

    QFile f(spec.path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || !f.resize(r.fileSize)) {
        r.error = QStringLiteral("cannot write %1: %2").arg(spec.path, f.errorString());
        return r;
    }
    QByteArray table(512, '\0');
    table[0x1BE + 4] = char(0x0B);
    put32le(table, 0x1BE + 8, first);
    put32le(table, 0x1BE + 12, sectors);
    table[510] = char(0x55);
    table[511] = char(0xAA);
    if (!put(f, 0, table) || !writeFat32(f, r.musicStart, planFat32(sectors, int(S), first)) ||
        !f.flush()) {
        r.error = QStringLiteral("write error on %1: %2").arg(spec.path, f.errorString());
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace

FakePodResult FakePod::build(const FakePodSpec &spec)
{
    FakePodResult r;
    const ModelRow *m = model(spec.modelId);
    if (!m) {
        r.error = QStringLiteral("unknown model id: %1").arg(spec.modelId);
        return r;
    }
    if (spec.table == TableKind::Apm && !m->hasMac) {
        r.error = QStringLiteral("no Mac version for %1").arg(spec.modelId);
        return r;
    }
    const bool nano2g = m->osValue == 0x100;
    const qint64 S = m->sectorSize;
    const bool mac = spec.table == TableKind::Apm;

    if (!m->hasFirmware)
        return buildSinglePartition(spec, *m);

    // ---- the firmware partition ------------------------------------------
    QList<Entry> entries;
    QByteArray fw; // from the start of the partition, up to the end of the data
    auto place = [&fw](qint64 at, const QByteArray &data) {
        // QByteArray::resize leaves the new bytes as they are: zero them.
        if (fw.size() < at + data.size())
            fw.append(QByteArray(int(at + data.size()) - fw.size(), '\0'));
        fw.replace(int(at), data.size(), data);
    };

    const quint32 version = (m->osValue << 8) | 0x05;
    const QByteArray program = spec.bootloader.isEmpty() ? defaultBootloader() : spec.bootloader;

    QByteArray osos = synthetic(kOsosLength, "FAKEOSOS", int(m->osValue & 0xFF));
    Entry ososEntry{"soso", kOsosStart, quint32(osos.size()), 0x10000000, 0, 0, version, 0};
    if (spec.bootloaderPresent && !nano2g) {
        // Section 7.4: Apple's firmware padded to a sector, then the bootloader.
        const int padded = int(roundUp(osos.size(), S));
        osos.append(QByteArray(padded - osos.size(), '\0'));
        ososEntry.entryOffset = quint32(padded);
        osos.append(program);
        ososEntry.len = quint32(osos.size());
        ososEntry.flash = 0xFFFFFFFF;
    }
    ososEntry.checksum = byteSum(osos);
    place(kOsosStart, osos);
    qint64 end = kOsosStart + roundUp(osos.size(), S);
    entries.append(ososEntry);

    auto extra = [&](const char *tag, const char *text, int length, int seed) {
        const QByteArray data = synthetic(length, text, seed);
        Entry e{tag, end, quint32(length), 0, 0, byteSum(data), version, 0xFFFFFFFF};
        place(end, data);
        end += roundUp(length, S);
        entries.append(e);
    };
    if (spec.withRsrc)
        extra("crsr", "FAKERSRC", kRsrcLength, 3);
    if (spec.withAupd)
        extra("dpua", "FAKEAUPD", kAupdLength, 4);

    if (spec.bootloaderPresent && nano2g) {
        // Section 7.5: Apple's entry becomes OSBK, the bootloader is a new OSOS
        // image after the last one, with a hash block in front of it.
        Entry backup = entries.first();
        const Entry last = entries.last();
        backup.tag = "kbso";
        entries.first() = backup;
        Entry boot = backup;
        boot.tag = "soso";
        boot.start = roundUp(last.start + last.len, S) + kHashBlock;
        boot.len = quint32(program.size());
        boot.checksum = byteSum(program);
        place(boot.start - kHashBlock, synthetic(kHashBlock, "FAKEHASH", 6));
        place(boot.start, program);
        entries.append(boot);
    }

    // The 3rd generation puts the image area at the start of the partition
    // when there is more than one image (section 7.3).
    const bool areaAtStart = m->formatVersion == 2 && entries.size() > 1;
    const qint64 imageArea = areaAtStart ? 0 : S;

    // Section 7.1 and 7.2: the pointer at 0x104 is 0, so the directory is at
    // 0x200; a late directory is the next sector after the zero byte there.
    const qint64 computed = 0x200;
    const qint64 directory = spec.directoryLate ? (computed / S + 1) * S : computed;
    QByteArray dir;
    for (const Entry &e : std::as_const(entries))
        dir.append(entryBytes(e, imageArea));

    QByteArray head(S, '\0');
    head.replace(0, 256, banner());
    putText(head, 0x100, "]ih[");
    put32le(head, 0x104, 0);
    put16le(head, 0x10A, m->formatVersion);
    fw.replace(0, int(S), head);
    // The entries may start inside the first sector (a directory at 0x200 of a
    // 2048-byte sector), so they go in after the header.
    place(directory, dir);

    // ---- the disk ----------------------------------------------------------
    r.sectorSize = int(S);
    r.fileSize = kImageBytes;
    r.firmwareStart = kFirmwareStartSector * S;
    r.firmwareBytes = kFirmwareBytes;
    r.musicStart = r.firmwareStart + r.firmwareBytes;
    r.musicBytes = kImageBytes - r.musicStart;
    r.directoryOffset = directory;
    r.imageArea = imageArea;
    if (fw.size() > r.firmwareBytes) {
        r.error = QStringLiteral("the images do not fit in the firmware partition");
        return r;
    }

    QFile f(spec.path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || !f.resize(kImageBytes)) {
        r.error = QStringLiteral("cannot write %1: %2").arg(spec.path, f.errorString());
        return r;
    }

    const quint32 fwFirst = quint32(kFirmwareStartSector);
    const quint32 fwSectors = quint32(r.firmwareBytes / S);
    const quint32 musicFirst = fwFirst + fwSectors;
    const quint32 musicSectors = quint32(r.musicBytes / S);

    QByteArray table(512, '\0');
    if (!mac) {
        put32le(table, 0x1BE + 8, fwFirst);
        put32le(table, 0x1BE + 12, fwSectors);
        table[0x1BE + 16 + 4] = char(0x0B);
        put32le(table, 0x1BE + 16 + 8, musicFirst);
        put32le(table, 0x1BE + 16 + 12, musicSectors);
        table[510] = char(0x55);
        table[511] = char(0xAA);
    } else {
        // Section 2.2: the header "ER" with the block size, then the map itself,
        // the firmware partition and the HFS partition.
        putText(table, 0, "ER");
        put16be(table, 2, 512);
        put32be(table, 4, quint32(kImageBytes / 512));
    }

    bool ok = put(f, r.firmwareStart, fw) && put(f, 0, table);
    if (mac) {
        ok = ok && put(f, 512, apmEntry(1, 3, 1, fwFirst - 1, "Apple", "Apple_partition_map")) &&
             put(f, 1024, apmEntry(2, 3, fwFirst, fwSectors, "Firmware", "Apple_MDFW")) &&
             put(f, 1536, apmEntry(3, 3, musicFirst, musicSectors, "iPod", "Apple_HFS")) &&
             writeHfsHeader(f, r.musicStart, r.musicBytes);
    } else {
        ok = ok && writeFat32(f, r.musicStart, planFat32(musicSectors, int(S), musicFirst));
    }
    if (!ok || !f.flush()) {
        r.error = QStringLiteral("write error on %1: %2").arg(spec.path, f.errorString());
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace tacetos::fakepod
