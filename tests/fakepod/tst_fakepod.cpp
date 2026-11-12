// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The images of the fake iPod library, checked byte by byte at the offsets of
// docs/specs/bootloader-legacy.md. The reader below does not use the builder's
// layout: it finds the partitions from the table in the image, like an
// installer would, and every offset is written out.

#include "fakepod.h"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::fakepod;

namespace {

QByteArray readAt(QFile &f, qint64 offset, qint64 length)
{
    f.seek(offset);
    const QByteArray b = f.read(length);
    return b.size() == length ? b : QByteArray();
}

quint32 le32(const QByteArray &b, int at)
{
    quint32 v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | quint8(b.at(at + i));
    return v;
}

quint32 le16(const QByteArray &b, int at)
{
    return quint8(b.at(at)) | (quint32(quint8(b.at(at + 1))) << 8);
}

quint32 be32(const QByteArray &b, int at)
{
    quint32 v = 0;
    for (int i = 0; i < 4; ++i)
        v = (v << 8) | quint8(b.at(at + i));
    return v;
}

quint32 be16(const QByteArray &b, int at)
{
    return (quint32(quint8(b.at(at))) << 8) | quint8(b.at(at + 1));
}

bool allZero(const QByteArray &b)
{
    return b.count('\0') == b.size();
}

qint64 roundUp(qint64 n, qint64 unit)
{
    return (n + unit - 1) / unit * unit;
}

// Slots of section 2.3, in sectors.
struct Slot
{
    quint32 type = 0;
    qint64 first = 0;
    qint64 count = 0;
};

struct Image
{
    QFile file;
    int sector = 512;
    bool apm = false;
    Slot slot[2];
    qint64 fwStart() const { return slot[0].first * sector; }
    qint64 musicStart() const { return slot[1].first * sector; }
};

// Section 2: the DOS table first, then the Apple Partition Map.
bool openImage(Image &img, const QString &path, int sector)
{
    img.file.setFileName(path);
    img.sector = sector;
    if (!img.file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray s0 = readAt(img.file, 0, 512);
    if (s0.size() != 512)
        return false;
    if (quint8(s0.at(510)) == 0x55 && quint8(s0.at(511)) == 0xAA) {
        for (int i = 0; i < 2; ++i) {
            const int e = 0x1BE + 16 * i;
            img.slot[i].type = quint8(s0.at(e + 4));
            img.slot[i].first = le32(s0, e + 8);
            img.slot[i].count = le32(s0, e + 12);
        }
        return true;
    }
    if (s0.at(0) != 'E' || s0.at(1) != 'R')
        return false;
    img.apm = true;
    const int block = int(be16(s0, 2));
    int kept = 0;
    for (int n = 1; n <= 8 && kept < 2; ++n) {
        const QByteArray e = readAt(img.file, qint64(n) * block, 512);
        if (e.size() != 512 || e.left(2) != "PM")
            break;
        const QByteArray type = QByteArray(e.constData() + 48, 32).split('\0').first();
        if (type == "Apple_MDFW" || type == "Apple_HFS") {
            img.slot[kept].type = type == "Apple_MDFW" ? 0 : 0xFFFF;
            img.slot[kept].first = qint64(be32(e, 8)) * block / sector;
            img.slot[kept].count = qint64(be32(e, 12)) * block / sector;
            ++kept;
        }
    }
    return kept == 2;
}

// One directory entry as read by section 7.2.
struct DirEntry
{
    QByteArray type; // as stored
    quint32 devOffset, len, address, entryOffset, checksum, version, flash;
};

QList<DirEntry> readDirectory(Image &img, qint64 *areaOut = nullptr, qint64 *dirOut = nullptr)
{
    const qint64 p = img.fwStart();
    const QByteArray head = readAt(img.file, p, img.sector);
    const quint32 format = le16(head, 0x10A);
    qint64 dir = qint64(le32(head, 0x104)) + 0x200;
    // Section 7.2: a zero first byte means the real directory is one sector on.
    QByteArray first = readAt(img.file, p + dir, 1);
    if (first.at(0) == 0)
        dir = (dir / img.sector + 1) * img.sector;
    QList<DirEntry> list;
    for (int i = 0; i < 10; ++i) {
        const QByteArray e = readAt(img.file, p + dir + 40 * i, 40);
        if (e.left(4) != "!ATA" && e.left(4) != "DNAN")
            break;
        list.append({e.mid(4, 4), le32(e, 12), le32(e, 16), le32(e, 20), le32(e, 24), le32(e, 28),
                     le32(e, 32), le32(e, 36)});
    }
    const qint64 area = (format == 2 && list.size() > 1) ? 0 : img.sector;
    if (areaOut)
        *areaOut = area;
    if (dirOut)
        *dirOut = dir;
    return list;
}

QByteArray imageData(Image &img, qint64 area, const DirEntry &e, qint64 length = -1)
{
    return readAt(img.file, img.fwStart() + area + e.devOffset, length < 0 ? e.len : length);
}

} // namespace

class TestFakePod : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void idsAreTheTenOfModelsToml();
    void bannerFollowsTheDescription();
    void classicHasOnePartitionOnly();
    void defaultBootloaderIsStable();

    void tableAndSlots_data();
    void tableAndSlots();
    void firmwareHeader_data();
    void firmwareHeader();
    void ososEntry_data();
    void ososEntry();
    void fatMusicPartition_data();
    void fatMusicPartition();
    void hfsMusicPartition_data();
    void hfsMusicPartition();
    void sparseAndSmall();

    void bootloaderAlreadyThere_data();
    void bootloaderAlreadyThere();
    void nanoTwoWithBootloader();
    void customBootloader();
    void extraEntries_data();
    void extraEntries();
    void thirdGenerationImageArea();
    void lateDirectory_data();
    void lateDirectory();

    void refusals();
    void sameBytesTwice();
    void toolWritesTheSameImage();

private:
    QString path(const QString &name) { return m_dir.filePath(name); }
    FakePodResult build(const QString &id, TableKind table = TableKind::Dos,
                        const QString &name = QStringLiteral("image.img"));
    QTemporaryDir m_dir;
};

FakePodResult TestFakePod::build(const QString &id, TableKind table, const QString &name)
{
    FakePodSpec spec;
    spec.modelId = id;
    spec.path = path(name);
    spec.table = table;
    return FakePod::build(spec);
}

void TestFakePod::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TestFakePod::idsAreTheTenOfModelsToml()
{
    QFile f(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    QStringList ids;
    QRegularExpression re(QStringLiteral("^id = \"([a-z0-9-]+)\"$"),
                          QRegularExpression::MultilineOption);
    for (auto it = re.globalMatch(text); it.hasNext();)
        ids << it.next().captured(1);
    QCOMPARE(ids.size(), 10);
    QStringList mine = FakePod::modelIds();
    ids.sort();
    mine.sort();
    QCOMPARE(mine, ids);
}

void TestFakePod::bannerFollowsTheDescription()
{
    const QByteArray b = FakePod::banner();
    QCOMPARE(b.size(), 256);
    // 16 rows of 16 characters, 255 visible characters, then a zero byte.
    QCOMPARE(b.at(255), '\0');
    QCOMPARE(b.left(255).count('\0'), 0);
    for (int row = 0; row < 7; ++row)
        QCOMPARE(b.mid(row * 16, 4), QByteArray("{{~~"));
    // The rows of the stop sign, with single backslashes.
    QCOMPARE(b.mid(0, 16), QByteArray("{{~~  /-----\\   "));
    QCOMPARE(b.mid(16, 16), QByteArray("{{~~ /       \\  "));
    QCOMPARE(b.mid(48, 16), QByteArray("{{~~| S T O P | "));
    QCOMPARE(b.mid(80, 16), QByteArray("{{~~ \\       /  "));
    QCOMPARE(b.mid(96, 16), QByteArray("{{~~  \\-----/   "));
    QVERIFY(b.left(112).contains("S T O P"));
    // The copyright text runs across rows 7 to 9, then dashes to the row end.
    QCOMPARE(b.mid(112, 38), QByteArray("Copyright(C) 2001 Apple Computer, Inc."));
    QCOMPARE(b.mid(150, 10), QByteArray(10, '-'));
    // The other rows are dashes.
    QCOMPARE(b.mid(160, 95), QByteArray(95, '-'));
}

void TestFakePod::classicHasOnePartitionOnly()
{
    FakePodSpec spec;
    spec.modelId = QStringLiteral("classic-6g");
    spec.path = path(QStringLiteral("classic.img"));
    const FakePodResult r = FakePod::build(spec);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.sectorSize, 4096);
    QCOMPARE(QFile(spec.path).size(), qint64(64 * 1024 * 1024));

    QFile f(spec.path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray s0 = readAt(f, 0, 512);
    QCOMPARE(quint8(s0.at(510)), quint8(0x55));
    QCOMPARE(quint8(s0.at(511)), quint8(0xAA));
    // Slot 1: FAT32, from byte 32768, up to the end. The other 3 slots are empty.
    QCOMPARE(quint8(s0.at(0x1BE + 4)), quint8(0x0B));
    QCOMPARE(le32(s0, 0x1BE + 8), 8u);
    QCOMPARE(le32(s0, 0x1BE + 12), 16384u - 8u);
    for (int i = 1; i < 4; ++i)
        QVERIFY(allZero(s0.mid(0x1BE + 16 * i, 16)));
    // The FAT32 boot sector, with sectors of 4096 bytes; no banner anywhere.
    const QByteArray boot = readAt(f, 8 * 4096, 512);
    QCOMPARE(boot.mid(82, 8), QByteArray("FAT32   "));
    QCOMPARE(le16(boot, 11), 4096u);
    QCOMPARE(le32(boot, 28), 8u);
    QVERIFY(!boot.startsWith("{{~~"));

    // No firmware images to ask for, and no Mac version.
    spec.bootloaderPresent = true;
    QVERIFY(!FakePod::build(spec).ok);
    spec.bootloaderPresent = false;
    spec.withAupd = true;
    QVERIFY(!FakePod::build(spec).ok);
}

void TestFakePod::defaultBootloaderIsStable()
{
    const QByteArray a = FakePod::defaultBootloader();
    QCOMPARE(a.size(), 4000);
    QCOMPARE(a, FakePod::defaultBootloader());
    QVERIFY(a.startsWith("FAKEBOOT"));
}

static void allModels(bool macToo, bool macOnly = false, bool skipNano2g = false)
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<bool>("mac");
    QTest::addColumn<int>("sector");
    QTest::addColumn<int>("value");
    QTest::addColumn<int>("format");
    for (const QString &id : FakePod::modelIds()) {
        const ModelRow *m = FakePod::model(id);
        if (!m->hasFirmware || (skipNano2g && m->osValue == 0x100))
            continue; // the Classic has its own test
        if (!macOnly)
            QTest::newRow(qPrintable(id + "/windows"))
                << id << false << m->sectorSize << int(m->osValue) << int(m->formatVersion);
        if (macToo && m->hasMac)
            QTest::newRow(qPrintable(id + "/mac"))
                << id << true << m->sectorSize << int(m->osValue) << int(m->formatVersion);
    }
}

void TestFakePod::tableAndSlots_data()
{
    allModels(true);
}

void TestFakePod::tableAndSlots()
{
    QFETCH(QString, id);
    QFETCH(bool, mac);
    QFETCH(int, sector);
    const FakePodResult r = build(id, mac ? TableKind::Apm : TableKind::Dos);
    QVERIFY2(r.ok, qPrintable(r.error));

    Image img;
    QVERIFY(openImage(img, path(QStringLiteral("image.img")), sector));
    QCOMPARE(img.apm, mac);
    QCOMPARE(img.slot[0].type, 0u);
    QCOMPARE(img.slot[1].type, mac ? 0xFFFFu : 0x0Bu);
    QVERIFY(img.slot[0].first > 0); // section 3, rule 3
    QVERIFY(img.slot[0].count > 0);
    // The music partition follows the firmware partition and both fit the file.
    QCOMPARE(img.slot[1].first, img.slot[0].first + img.slot[0].count);
    QVERIFY((img.slot[1].first + img.slot[1].count) * sector <= 64 * 1024 * 1024);
    // The sector size of the model: 2048 for the 2nd generation nano only.
    QCOMPARE(sector, id == QLatin1String("nano-2g") ? 2048 : 512);

    if (!mac) {
        // Bytes of section 2.1, one by one.
        const QByteArray s0 = readAt(img.file, 0, 512);
        QCOMPARE(quint8(s0.at(510)), quint8(0x55));
        QCOMPARE(quint8(s0.at(511)), quint8(0xAA));
        QCOMPARE(quint8(s0.at(0x1BE + 4)), quint8(0x00));
        QCOMPARE(quint8(s0.at(0x1BE + 16 + 4)), quint8(0x0B));
        QVERIFY(allZero(s0.mid(0x1BE + 16 * 2, 32)));
        QVERIFY(allZero(s0.left(0x1BE)));
        for (int i = 0; i < 2; ++i) {
            const int e = 0x1BE + 16 * i;
            QCOMPARE(quint8(s0.at(e)), quint8(0)); // boot flag
            QVERIFY(allZero(s0.mid(e + 1, 3)));
            QVERIFY(allZero(s0.mid(e + 5, 3)));
        }
    } else {
        // Section 2.2: header, then the three entries of the map.
        const QByteArray s0 = readAt(img.file, 0, 512);
        QCOMPARE(s0.left(2), QByteArray("ER"));
        QCOMPARE(be16(s0, 2), 0x0200u);
        QVERIFY(!(quint8(s0.at(510)) == 0x55 && quint8(s0.at(511)) == 0xAA));
        const char *const types[3] = {"Apple_partition_map", "Apple_MDFW", "Apple_HFS"};
        for (int n = 1; n <= 3; ++n) {
            const QByteArray e = readAt(img.file, n * 512, 512);
            QCOMPARE(e.left(2), QByteArray("PM"));
            QCOMPARE(be32(e, 4), 3u);
            QCOMPARE(QByteArray(e.constData() + 48, 32).split('\0').first(),
                     QByteArray(types[n - 1]));
            QVERIFY(be32(e, 12) > 0);
        }
        // The map entry covers the blocks between the header and the firmware.
        const QByteArray e1 = readAt(img.file, 512, 512);
        QCOMPARE(be32(e1, 8), 1u);
        QCOMPARE(be32(e1, 8) + be32(e1, 12), quint32(img.slot[0].first));
        QCOMPARE(readAt(img.file, 4 * 512, 512).left(2) == "PM", false);
    }
}

void TestFakePod::firmwareHeader_data()
{
    allModels(true);
}

void TestFakePod::firmwareHeader()
{
    QFETCH(QString, id);
    QFETCH(bool, mac);
    QFETCH(int, sector);
    QFETCH(int, format);
    QVERIFY(build(id, mac ? TableKind::Apm : TableKind::Dos).ok);
    Image img;
    QVERIFY(openImage(img, path(QStringLiteral("image.img")), sector));
    const qint64 p = img.fwStart();

    // Section 4: the banner, then section 7.1.
    QCOMPARE(readAt(img.file, p, 256), FakePod::banner());
    QCOMPARE(readAt(img.file, p + 0x100, 4), QByteArray("]ih["));
    const QByteArray head = readAt(img.file, p, sector);
    QCOMPARE(le32(head, 0x104), 0u);
    QCOMPARE(le16(head, 0x10A), quint32(format));
    QCOMPARE(format, id == QLatin1String("ipod-3g") ? 2 : 3);
    // Nothing else in the first sector but the directory.
    QVERIFY(allZero(head.mid(0x10C, 0x200 - 0x10C)));
    QVERIFY(allZero(head.mid(0x108, 2)));
}

void TestFakePod::ososEntry_data()
{
    allModels(true);
}

void TestFakePod::ososEntry()
{
    QFETCH(QString, id);
    QFETCH(bool, mac);
    QFETCH(int, sector);
    QFETCH(int, value);
    QVERIFY(build(id, mac ? TableKind::Apm : TableKind::Dos).ok);
    Image img;
    QVERIFY(openImage(img, path(QStringLiteral("image.img")), sector));

    qint64 area = 0, dir = 0;
    const QList<DirEntry> list = readDirectory(img, &area, &dir);
    QCOMPARE(list.size(), 1);
    QCOMPARE(dir, qint64(0x200));
    QCOMPARE(area, qint64(sector));
    const DirEntry &e = list.first();
    QCOMPARE(e.type, QByteArray("soso"));
    // Section 5: the model is the version shifted right by 8 bits.
    QCOMPARE(int(e.version >> 8), value);
    QCOMPARE(e.entryOffset, 0u);
    QCOMPARE(e.len, quint32(0x8064));
    QVERIFY(qint64(e.devOffset) + area >= 0x400); // clear of the directory
    QCOMPARE(qint64(e.devOffset) + area, qint64(0x4000));
    QVERIFY(e.flash != 0xFFFFFFFF); // no bootloader: the flash field is not set
    const QByteArray data = imageData(img, area, e);
    QCOMPARE(data.size(), 0x8064);
    QVERIFY(data.startsWith("FAKEOSOS"));
    quint32 sum = 0;
    for (char c : data)
        sum += quint8(c);
    QCOMPARE(e.checksum, sum);
    // The padding up to the sector is zero, and nothing follows it.
    const qint64 padEnd = roundUp(e.len, sector);
    QVERIFY(allZero(imageData(img, area, e, padEnd).mid(e.len)));
    QVERIFY(allZero(readAt(img.file, img.fwStart() + 0x4000 + padEnd, 4096)));
    // The bytes of the entry itself, after it: the end of the list.
    QCOMPARE(readAt(img.file, img.fwStart() + dir + 40, 4) == "!ATA", false);
}

void TestFakePod::fatMusicPartition_data()
{
    allModels(false);
}

void TestFakePod::fatMusicPartition()
{
    QFETCH(QString, id);
    QFETCH(int, sector);
    QVERIFY(build(id).ok);
    Image img;
    QVERIFY(openImage(img, path(QStringLiteral("image.img")), sector));
    const qint64 m = img.musicStart();
    const qint64 S = sector;

    const QByteArray boot = readAt(img.file, m, S);
    // Section 20.3, byte by byte.
    QCOMPARE(boot.left(3), QByteArray("\xEB\x58\x90", 3));
    QCOMPARE(boot.mid(3, 8), QByteArray("MSWIN4.1"));
    QCOMPARE(le16(boot, 11), quint32(sector));
    const quint32 spc = quint8(boot.at(13));
    QVERIFY(spc >= 1 && (spc & (spc - 1)) == 0);
    QCOMPARE(le16(boot, 14), 32u);
    QCOMPARE(quint8(boot.at(16)), quint8(2));
    QCOMPARE(le16(boot, 17), 0u);
    QCOMPARE(le16(boot, 19), 0u);
    QCOMPARE(quint8(boot.at(21)), quint8(0xF8));
    QCOMPARE(le16(boot, 22), 0u);
    QCOMPARE(le16(boot, 24), 63u);
    QCOMPARE(le16(boot, 26), 255u);
    // The whole 32-bit value, and it is past 65 535 only on larger layouts.
    QCOMPARE(qint64(le32(boot, 28)), img.slot[1].first);
    const quint32 total = le32(boot, 32);
    QCOMPARE(qint64(total), img.slot[1].count);
    const quint32 fatSize = le32(boot, 36);
    QCOMPARE(le16(boot, 40), 0u);
    QCOMPARE(le16(boot, 42), 0u);
    QCOMPARE(le32(boot, 44), 2u);
    QCOMPARE(le16(boot, 48), 1u);
    QCOMPARE(le16(boot, 50), 6u);
    QCOMPARE(quint8(boot.at(64)), quint8(0x80));
    QCOMPARE(quint8(boot.at(66)), quint8(0x29));
    QVERIFY(le32(boot, 67) != 0); // the serial number is not 0
    QCOMPARE(boot.mid(71, 11), QByteArray("IPOD       "));
    QCOMPARE(boot.mid(82, 8), QByteArray("FAT32   "));
    QCOMPARE(quint8(boot.at(510)), quint8(0x55));
    QCOMPARE(quint8(boot.at(511)), quint8(0xAA));
    // All other bytes are zero.
    for (auto range : {std::pair<int, int>{20, 1}, {52, 12}, {65, 1}, {90, 420}})
        QVERIFY(allZero(boot.mid(range.first, range.second)));
    QVERIFY(allZero(boot.mid(512, int(S) - 512)));

    // The copy of the boot sector at sector 6.
    QCOMPARE(readAt(img.file, m + 6 * S, S), boot);

    // The size of the FAT: the smallest that holds every cluster plus 2.
    const qint64 clusters = (qint64(total) - 32 - 2 * qint64(fatSize)) / spc;
    QVERIFY((clusters + 2) * 4 <= qint64(fatSize) * S);
    const qint64 clustersLess = (qint64(total) - 32 - 2 * (qint64(fatSize) - 1)) / spc;
    QVERIFY((clustersLess + 2) * 4 > (qint64(fatSize) - 1) * S);
    if (sector == 512) {
        QVERIFY(clusters >= 65525);
        QVERIFY(clusters <= 0x0FFFFFF5);
        // The cluster is the largest that still gives 65 525 clusters.
        const qint64 spc2 = spc * 2;
        const qint64 fat2 = (4 * (qint64(total) - 32) + (S * spc2 + 8) - 1) / (S * spc2 + 8);
        QVERIFY((qint64(total) - 32 - 2 * (fat2 + 1)) / spc2 < 65525 || spc == 128);
    } else {
        // 64 MiB of 2048-byte sectors cannot hold 65 525 clusters.
        QVERIFY(clusters < 65525);
        QCOMPARE(spc, 1u);
    }

    // FSInfo at sector 1 and its copy at sector 7.
    QByteArray info(int(S), '\0');
    info[0] = 'R';
    info[1] = 'R';
    info[2] = 'a';
    info[3] = 'A';
    info[484] = 'r';
    info[485] = 'r';
    info[486] = 'A';
    info[487] = 'a';
    for (int i = 0; i < 4; ++i)
        info[488 + i] = char((quint32(clusters - 1) >> (8 * i)) & 0xFF);
    info[492] = 3;
    info[510] = char(0x55);
    info[511] = char(0xAA);
    QCOMPARE(readAt(img.file, m + S, S), info);
    QCOMPARE(readAt(img.file, m + 7 * S, S), info);

    // Both FATs: 3 entries, then zeros, and the root folder (cluster 2) is empty.
    const char fat[12] = {char(0xF8), char(0xFF), char(0xFF), 0x0F,       char(0xFF), char(0xFF),
                          char(0xFF), 0x0F,       char(0xFF), char(0xFF), char(0xFF), 0x0F};
    for (qint64 first : {qint64(32), qint64(32) + fatSize}) {
        const QByteArray s = readAt(img.file, m + first * S, S);
        QCOMPARE(s.left(12), QByteArray(fat, 12));
        QVERIFY(allZero(s.mid(12)));
        QVERIFY(allZero(readAt(img.file, m + (first + 1) * S, S)));
    }
    const qint64 root = m + (32 + 2 * qint64(fatSize)) * S;
    QVERIFY(allZero(readAt(img.file, root, spc * S)));
    // Between the FSInfo copy and the FATs, nothing.
    QVERIFY(allZero(readAt(img.file, m + 8 * S, 24 * S)));
    QVERIFY(allZero(readAt(img.file, m + 2 * S, 4 * S)));
}

void TestFakePod::hfsMusicPartition_data()
{
    allModels(true, true);
}

void TestFakePod::hfsMusicPartition()
{
    QFETCH(QString, id);
    QFETCH(bool, mac);
    QFETCH(int, sector);
    QVERIFY(mac);
    QVERIFY(build(id, TableKind::Apm).ok);
    Image img;
    QVERIFY(openImage(img, path(QStringLiteral("image.img")), sector));
    const qint64 m = img.musicStart();

    // Before the header, 1024 zero bytes.
    QVERIFY(allZero(readAt(img.file, m, 1024)));
    const QByteArray h = readAt(img.file, m + 1024, 512);
    QCOMPARE(h.left(2), QByteArray("H+"));
    QCOMPARE(be16(h, 2), 4u);
    QCOMPARE(be32(h, 4) & 0x100, 0x100u);
    QCOMPARE(be32(h, 32), 0u);
    QCOMPARE(be32(h, 36), 0u);
    QCOMPARE(be32(h, 40), 4096u);
    const quint32 blocks = be32(h, 44);
    QCOMPARE(qint64(blocks) * 4096 + 4095 >= img.slot[1].count * sector, true);
    QCOMPARE(qint64(blocks), img.slot[1].count * sector / 4096);
    QVERIFY(be32(h, 48) <= blocks);
    QCOMPARE(be32(h, 64), 16u);
    // No FAT32 boot sector here.
    QVERIFY(readAt(img.file, m + 510, 2) != QByteArray("\x55\xAA", 2));
}

void TestFakePod::sparseAndSmall()
{
    const FakePodResult r = build(QStringLiteral("ipod-video"));
    QVERIFY(r.ok);
    QFile f(path(QStringLiteral("image.img")));
    QCOMPARE(f.size(), qint64(64 * 1024 * 1024));
    QVERIFY(f.size() <= qint64(64) * 1024 * 1024);
    QCOMPARE(r.fileSize, f.size());
    // The partitions are placed inside the file and in the order of the table.
    QVERIFY(r.firmwareStart > 0);
    QCOMPARE(r.musicStart, r.firmwareStart + r.firmwareBytes);
    QVERIFY(r.musicStart + r.musicBytes <= r.fileSize);
    // The last byte of the image is zero: the file is a hole there.
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(allZero(readAt(f, f.size() - 4096, 4096)));
}

void TestFakePod::bootloaderAlreadyThere_data()
{
    allModels(true, false, true);
}

void TestFakePod::bootloaderAlreadyThere()
{
    QFETCH(QString, id);
    QFETCH(bool, mac);
    QFETCH(int, sector);
    QFETCH(int, value);
    FakePodSpec spec;
    spec.modelId = id;
    spec.path = path(QStringLiteral("boot.img"));
    spec.table = mac ? TableKind::Apm : TableKind::Dos;
    spec.bootloaderPresent = true;
    QVERIFY(FakePod::build(spec).ok);
    Image img;
    QVERIFY(openImage(img, spec.path, sector));

    qint64 area = 0;
    const QList<DirEntry> list = readDirectory(img, &area);
    QCOMPARE(list.size(), 1);
    const DirEntry &e = list.first();
    QCOMPARE(e.type, QByteArray("soso"));
    QCOMPARE(int(e.version >> 8), value);
    // Section 7.4: Apple's firmware padded to a sector, then the bootloader.
    const qint64 padded = roundUp(0x8064, sector);
    QCOMPARE(qint64(e.entryOffset), padded);
    QCOMPARE(e.entryOffset % quint32(sector), 0u);
    const QByteArray program = FakePod::defaultBootloader();
    QCOMPARE(qint64(e.len), padded + program.size());
    QCOMPARE(e.flash, 0xFFFFFFFFu);
    const QByteArray data = imageData(img, area, e);
    QCOMPARE(data.left(0x8064), imageData(img, area, e, 0x8064));
    QVERIFY(data.startsWith("FAKEOSOS"));
    QVERIFY(allZero(data.mid(0x8064, int(padded) - 0x8064)));
    QCOMPARE(data.mid(int(padded)), program);
    quint32 sum = 0;
    for (char c : data)
        sum += quint8(c);
    QCOMPARE(e.checksum, sum);
}

void TestFakePod::nanoTwoWithBootloader()
{
    FakePodSpec spec;
    spec.modelId = QStringLiteral("nano-2g");
    spec.path = path(QStringLiteral("nano.img"));
    spec.bootloaderPresent = true;
    QVERIFY(FakePod::build(spec).ok);
    Image img;
    QVERIFY(openImage(img, spec.path, 2048));

    qint64 area = 0;
    const QList<DirEntry> list = readDirectory(img, &area);
    QCOMPARE(list.size(), 2);
    // Section 7.5: OSBK first, the bootloader as the last OSOS.
    QCOMPARE(list.at(0).type, QByteArray("kbso"));
    QCOMPARE(list.at(1).type, QByteArray("soso"));
    QCOMPARE(list.at(0).len, quint32(0x8064));
    QCOMPARE(list.at(0).entryOffset, 0u);
    QCOMPARE(int(list.at(0).version >> 8), 0x100);
    QCOMPARE(int(list.at(1).version >> 8), 0x100);
    const QByteArray program = FakePod::defaultBootloader();
    QCOMPARE(list.at(1).len, quint32(program.size()));
    // After the last image, a whole sector, plus the hash block.
    const qint64 expected = roundUp(list.at(0).devOffset + list.at(0).len, 2048) + 0x800;
    QCOMPARE(qint64(list.at(1).devOffset), expected);
    QCOMPARE(imageData(img, area, list.at(1)), program);
    // The 0x800 bytes before it are the hash block, not zeros.
    const QByteArray hash =
        readAt(img.file, img.fwStart() + area + list.at(1).devOffset - 0x800, 0x800);
    QVERIFY(hash.startsWith("FAKEHASH"));
    QVERIFY(!allZero(hash));
    quint32 sum = 0;
    for (char c : program)
        sum += quint8(c);
    QCOMPARE(list.at(1).checksum, sum);
    // The OSBK entry is Apple's image, untouched.
    QVERIFY(imageData(img, area, list.at(0)).startsWith("FAKEOSOS"));
}

void TestFakePod::customBootloader()
{
    const QByteArray program(1500, 'Q');
    FakePodSpec spec;
    spec.modelId = QStringLiteral("mini-2g");
    spec.path = path(QStringLiteral("custom.img"));
    spec.bootloaderPresent = true;
    spec.bootloader = program;
    QVERIFY(FakePod::build(spec).ok);
    Image img;
    QVERIFY(openImage(img, spec.path, 512));
    qint64 area = 0;
    const DirEntry e = readDirectory(img, &area).first();
    QCOMPARE(e.len, quint32(roundUp(0x8064, 512) + 1500));
    QCOMPARE(imageData(img, area, e).mid(int(e.entryOffset)), program);
}

void TestFakePod::extraEntries_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<bool>("boot");
    QTest::addColumn<bool>("rsrc");
    QTest::addColumn<bool>("aupd");
    for (const QString &id :
         {QStringLiteral("ipod-video"), QStringLiteral("nano-2g"), QStringLiteral("ipod-4g")})
        for (bool boot : {false, true})
            for (int mask = 1; mask < 4; ++mask)
                QTest::newRow(qPrintable(
                    QStringLiteral("%1/%2/%3").arg(id, boot ? "boot" : "clean").arg(mask)))
                    << id << boot << bool(mask & 1) << bool(mask & 2);
}

void TestFakePod::extraEntries()
{
    QFETCH(QString, id);
    QFETCH(bool, boot);
    QFETCH(bool, rsrc);
    QFETCH(bool, aupd);
    const int sector = id == QLatin1String("nano-2g") ? 2048 : 512;
    FakePodSpec spec;
    spec.modelId = id;
    spec.path = path(QStringLiteral("extra.img"));
    spec.bootloaderPresent = boot;
    spec.withRsrc = rsrc;
    spec.withAupd = aupd;
    QVERIFY(FakePod::build(spec).ok);
    Image img;
    QVERIFY(openImage(img, spec.path, sector));
    qint64 area = 0;
    const QList<DirEntry> list = readDirectory(img, &area);

    QList<QByteArray> tags;
    tags << (boot && sector == 2048 ? "kbso" : "soso");
    if (rsrc)
        tags << "crsr";
    if (aupd)
        tags << "dpua";
    if (boot && sector == 2048)
        tags << "soso";
    QCOMPARE(list.size(), tags.size());
    for (int i = 0; i < tags.size(); ++i)
        QCOMPARE(list.at(i).type, tags.at(i));

    // No overlap, increasing offsets, each image after the previous one with
    // its padding (section 11 check 3), and the sums are right.
    qint64 prevEnd = 0;
    for (const DirEntry &e : list) {
        QVERIFY(qint64(e.devOffset) >= prevEnd);
        prevEnd = qint64(e.devOffset) + roundUp(e.len, sector);
        const QByteArray data = imageData(img, area, e);
        quint32 sum = 0;
        for (char c : data)
            sum += quint8(c);
        QCOMPARE(e.checksum, sum);
        if (e.type == "crsr") {
            QVERIFY(data.startsWith("FAKERSRC"));
            QCOMPARE(e.len, quint32(5000));
            QCOMPARE(e.flash, 0xFFFFFFFFu);
        }
        if (e.type == "dpua") {
            QVERIFY(data.startsWith("FAKEAUPD"));
            QCOMPARE(e.len, quint32(3000));
        }
    }
    QVERIFY(prevEnd <= 8 * 1024 * 1024);
    QCOMPARE(list.size() > 0 && list.first().entryOffset != 0, boot && sector != 2048);
}

void TestFakePod::thirdGenerationImageArea()
{
    // Section 7.3: with one image the area starts one sector in; with more
    // than one, at the start of the partition. The directory says the same
    // thing with its devOffset values.
    FakePodSpec spec;
    spec.modelId = QStringLiteral("ipod-3g");
    spec.path = path(QStringLiteral("3g.img"));
    QVERIFY(FakePod::build(spec).ok);
    Image img;
    QVERIFY(openImage(img, spec.path, 512));
    qint64 area = 99;
    QList<DirEntry> list = readDirectory(img, &area);
    QCOMPARE(list.size(), 1);
    QCOMPARE(area, qint64(512));
    QCOMPARE(list.first().devOffset, quint32(0x4000 - 512));

    spec.withRsrc = true;
    QVERIFY(FakePod::build(spec).ok);
    Image img2;
    QVERIFY(openImage(img2, spec.path, 512));
    list = readDirectory(img2, &area);
    QCOMPARE(list.size(), 2);
    QCOMPARE(area, qint64(0));
    QCOMPARE(list.first().devOffset, quint32(0x4000));
    QVERIFY(imageData(img2, area, list.first()).startsWith("FAKEOSOS"));
    QVERIFY(imageData(img2, area, list.at(1)).startsWith("FAKERSRC"));
}

void TestFakePod::lateDirectory_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<int>("sector");
    QTest::newRow("nano-2g") << QStringLiteral("nano-2g") << 2048;
    QTest::newRow("video") << QStringLiteral("ipod-video") << 512;
}

void TestFakePod::lateDirectory()
{
    QFETCH(QString, id);
    QFETCH(int, sector);
    FakePodSpec spec;
    spec.modelId = id;
    spec.path = path(QStringLiteral("late.img"));
    spec.directoryLate = true;
    spec.withRsrc = true;
    const FakePodResult r = FakePod::build(spec);
    QVERIFY(r.ok);
    Image img;
    QVERIFY(openImage(img, spec.path, sector));
    const qint64 p = img.fwStart();
    // The first byte at 0x104 + 0x200 is zero; the real directory is one sector on.
    QCOMPARE(readAt(img.file, p + 0x200, 1), QByteArray(1, '\0'));
    qint64 dir = 0;
    const QList<DirEntry> list = readDirectory(img, nullptr, &dir);
    QCOMPARE(dir, qint64(sector == 2048 ? 0x800 : 0x400));
    QCOMPARE(r.directoryOffset, dir);
    QCOMPARE(list.size(), 2);
    QCOMPARE(readAt(img.file, p + dir, 4), QByteArray("!ATA"));
    QVERIFY(allZero(readAt(img.file, p + 0x200, 40)));
}

void TestFakePod::refusals()
{
    FakePodSpec spec;
    spec.path = path(QStringLiteral("no.img"));
    spec.modelId = QStringLiteral("zune");
    QVERIFY(!FakePod::build(spec).ok);
    spec.modelId = QStringLiteral("nano-2g");
    spec.table = TableKind::Apm;
    FakePodResult r = FakePod::build(spec);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    spec.modelId = QStringLiteral("classic-6g");
    QVERIFY(!FakePod::build(spec).ok);
    QVERIFY(!QFile::exists(spec.path));
    // A path that cannot be written.
    spec.table = TableKind::Dos;
    spec.path = path(QStringLiteral("missing-folder/x.img"));
    QVERIFY(!FakePod::build(spec).ok);
}

static bool sameFiles(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly) || fa.size() != fb.size())
        return false;
    while (!fa.atEnd()) {
        if (fa.read(1 << 20) != fb.read(1 << 20))
            return false;
    }
    return true;
}

void TestFakePod::sameBytesTwice()
{
    QVERIFY(build(QStringLiteral("mini-1g"), TableKind::Dos, QStringLiteral("a.img")).ok);
    QVERIFY(build(QStringLiteral("mini-1g"), TableKind::Dos, QStringLiteral("b.img")).ok);
    QVERIFY(sameFiles(path(QStringLiteral("a.img")), path(QStringLiteral("b.img"))));
    // Building over an existing image replaces it.
    QVERIFY(build(QStringLiteral("nano-1g"), TableKind::Dos, QStringLiteral("a.img")).ok);
    QVERIFY(!sameFiles(path(QStringLiteral("a.img")), path(QStringLiteral("b.img"))));
}

void TestFakePod::toolWritesTheSameImage()
{
    const QString out = path(QStringLiteral("tool.img"));
    QProcess p;
    p.start(QStringLiteral(TACETOS_FAKEPOD_TOOL),
            {QStringLiteral("ipod-video"), out, QStringLiteral("--mac"), QStringLiteral("--rsrc")});
    QVERIFY(p.waitForFinished(60000));
    QCOMPARE(p.exitCode(), 0);

    FakePodSpec spec;
    spec.modelId = QStringLiteral("ipod-video");
    spec.path = path(QStringLiteral("lib.img"));
    spec.table = TableKind::Apm;
    spec.withRsrc = true;
    QVERIFY(FakePod::build(spec).ok);
    QVERIFY2(sameFiles(out, spec.path), qPrintable(out));

    // A bad id or a missing argument gives a message and a code, no file.
    QProcess bad;
    bad.start(QStringLiteral(TACETOS_FAKEPOD_TOOL),
              {QStringLiteral("zune"), path(QStringLiteral("z.img"))});
    QVERIFY(bad.waitForFinished(60000));
    QCOMPARE(bad.exitCode(), 1);
    QProcess usage;
    usage.start(QStringLiteral(TACETOS_FAKEPOD_TOOL), {QStringLiteral("ipod-video")});
    QVERIFY(usage.waitForFinished(60000));
    QCOMPARE(usage.exitCode(), 2);
    QVERIFY(usage.readAllStandardError().contains("ipod-1g2g"));
}

QTEST_GUILESS_MAIN(TestFakePod)
#include "tst_fakepod.moc"
