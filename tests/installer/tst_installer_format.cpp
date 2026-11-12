// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Formatting the music partition as FAT32 (docs/specs/bootloader-legacy.md
// sections 20.1 to 20.4, tests of section 26.4, first part). The expected sectors
// are written again here from the tables of the spec, and the sizes are found by
// a brute force that does not share code with the installer. A format on a file
// is also read by `fsck.fat` when the tool is there.

#include "constants.h"
#include "fakepod.h"
#include "faultdevice.h"
#include "format.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

constexpr int kSector = 512;
constexpr qint64 kGiBSectors = 1024ll * 1024 * 1024 / kSector;
constexpr quint8 kFill = 0xE5;

// A disk that stores only the sectors written to it. What was never written reads
// as `kFill`, so a format that leaves old content shows.
class SparseDevice : public SectorDevice
{
public:
    explicit SparseDevice(qint64 sectors) : m_sectors(sectors) {}
    int sectorSize() const override { return kSector; }
    qint64 sectorCount() const override { return m_sectors; }
    bool isRealDisk() const override { return false; }
    IoResult readSectors(qint64 first, qint64 count, QByteArray &out) override
    {
        if (first < 0 || count < 0 || first + count > m_sectors)
            return IoResult::failure(QStringLiteral("read past the end"));
        out.clear();
        out.reserve(int(count * kSector));
        for (qint64 s = first; s < first + count; ++s)
            out.append(m_data.value(s, QByteArray(kSector, char(kFill))));
        return IoResult::success();
    }
    IoResult writeSectors(qint64 first, const QByteArray &data) override
    {
        if (data.size() % kSector != 0 || first < 0 || first + data.size() / kSector > m_sectors)
            return IoResult::failure(QStringLiteral("write outside the disk"));
        for (int i = 0; i < data.size() / kSector; ++i)
            m_data.insert(first + i, data.mid(i * kSector, kSector));
        return IoResult::success();
    }
    IoResult flush() override { return IoResult::success(); }
    QByteArray sector(qint64 s) const { return m_data.value(s, QByteArray(kSector, char(kFill))); }

private:
    qint64 m_sectors;
    QHash<qint64, QByteArray> m_data;
};

// What the spec says, by brute force: the smallest FAT size for which one FAT
// holds the 2 first entries and one entry per cluster.
struct Expected
{
    qint64 spc = 0;
    qint64 fat = 0;
    qint64 clusters = 0;
};

Expected expectedFor(qint64 total)
{
    Expected e;
    const qint64 gib = total * kSector / (1024ll * 1024 * 1024);
    const bool over = total * kSector > gib * 1024ll * 1024 * 1024;
    const qint64 sizeGiB = gib + (over ? 1 : 0); // rounded up: "up to 8 GiB" and so on
    const int clusterKiB = sizeGiB <= 8 ? 4 : sizeGiB <= 16 ? 8 : sizeGiB <= 32 ? 16 : 32;
    e.spc = clusterKiB * 2;
    for (e.fat = 1;; ++e.fat) {
        const qint64 clusters = (total - 32 - 2 * e.fat) / e.spc;
        if (e.fat * 128 >= clusters + 2) {
            e.clusters = clusters;
            break;
        }
    }
    return e;
}

void put16(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 2; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xFF);
}

void put32(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((v >> (8 * i)) & 0xFF);
}

QByteArray expectedBoot(const Expected &e, qint64 total, quint32 hidden, quint32 serial,
                        const QByteArray &label11)
{
    QByteArray b(kSector, '\0');
    b.replace(0, 3, QByteArray::fromHex("EB5890"));
    b.replace(3, 8, "MSWIN4.1");
    put16(b, 11, 512);
    b[13] = char(e.spc);
    put16(b, 14, 32);
    b[16] = 2;
    b[21] = char(0xF8);
    put16(b, 24, 63);
    put16(b, 26, 255);
    put32(b, 28, hidden);
    put32(b, 32, quint32(total));
    put32(b, 36, quint32(e.fat));
    put32(b, 44, 2);
    put16(b, 48, 1);
    put16(b, 50, 6);
    b[64] = char(0x80);
    b[66] = 0x29;
    put32(b, 67, serial);
    b.replace(71, 11, label11);
    b.replace(82, 8, "FAT32   ");
    b[510] = 0x55;
    b[511] = char(0xAA);
    return b;
}

QByteArray expectedFsInfo(const Expected &e)
{
    QByteArray b(kSector, '\0');
    put32(b, 0, 0x41615252);
    put32(b, 484, 0x61417272);
    put32(b, 488, quint32(e.clusters - 1));
    put32(b, 492, 3);
    put32(b, 508, 0xAA550000);
    return b;
}

QByteArray expectedFatStart()
{
    QByteArray b(kSector, '\0');
    put32(b, 0, 0x0FFFFFF8);
    put32(b, 4, 0x0FFFFFFF);
    put32(b, 8, 0x0FFFFFFF);
    return b;
}

const QDateTime kWhen(QDate(2026, 11, 11), QTime(13, 45, 7, 120));
// By hand from section 20.3: month and day 0x0B0B plus seconds and hundredths
// 0x070C, year 0x07EA plus hours and minutes 0x0D2D.
constexpr quint32 kSerial = 0x15171217;

// Checks every sector of the formatted area of a partition at `first` on `dev`.
void verifyFormat(SparseDevice &dev, qint64 first, qint64 total)
{
    const Expected e = expectedFor(total);
    const QByteArray boot = expectedBoot(e, total, quint32(first), kSerial, "IPOD       ");
    const QByteArray fsinfo = expectedFsInfo(e);
    const QByteArray fatStart = expectedFatStart();
    const QByteArray zero(kSector, '\0');
    const qint64 rootEnd = 32 + 2 * e.fat + e.spc;
    for (qint64 s = 0; s < rootEnd; ++s) {
        QByteArray want = zero;
        if (s == 0 || s == 6)
            want = boot;
        else if (s == 1 || s == 7)
            want = fsinfo;
        else if (s == 32 || s == 32 + e.fat)
            want = fatStart;
        if (dev.sector(first + s) != want)
            QFAIL(qPrintable(QStringLiteral("sector %1 of the partition is wrong").arg(s)));
    }
    // Nothing after the root folder is written, and nothing before the partition.
    QCOMPARE(dev.sector(first + rootEnd), QByteArray(kSector, char(kFill)));
    QCOMPARE(dev.sector(first + total - 1), QByteArray(kSector, char(kFill)));
    if (first > 0)
        QCOMPARE(dev.sector(first - 1), QByteArray(kSector, char(kFill)));
}

// The smallest number of sectors that gives 65 525 clusters of 4 KiB.
qint64 smallestFat32Total()
{
    qint64 total = 32 + 65525 * 8;
    while (expectedFor(total).clusters < 65525)
        ++total;
    return total;
}

QString findFsck()
{
    const QString found = QStandardPaths::findExecutable(QStringLiteral("fsck.fat"));
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable(QStringLiteral("fsck.fat"),
                                          {QStringLiteral("/usr/sbin"), QStringLiteral("/sbin")});
}

QByteArray readAll(const QString &path, qint64 offset = 0, qint64 length = -1)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return QByteArray();
    return length < 0 ? f.readAll() : f.read(length);
}

QByteArray hashOf(const QString &path, qint64 length = -1)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    QCryptographicHash h(QCryptographicHash::Sha256);
    qint64 left = length < 0 ? f.size() : length;
    while (left > 0) {
        const QByteArray part = f.read(qMin<qint64>(left, 4 * 1024 * 1024));
        if (part.isEmpty())
            break;
        h.addData(part);
        left -= part.size();
    }
    return h.result();
}

// An image of an iPod (fake) whose music partition starts at `musicFirst` and
// has `musicTotal` sectors. The file is made longer (sparse) when needed.
struct Pod
{
    QString path;
    fp::FakePodResult info;
    qint64 musicFirst = 0;
    qint64 musicTotal = 0;
};

Pod makePod(const QTemporaryDir &dir, const QString &name, const QString &model, qint64 musicFirst,
            qint64 musicTotal, bool bootloader = false, fp::TableKind table = fp::TableKind::Dos)
{
    Pod p;
    fp::FakePodSpec spec;
    spec.modelId = model;
    spec.path = dir.filePath(name);
    spec.table = table;
    spec.bootloaderPresent = bootloader;
    p.info = fp::FakePod::build(spec);
    p.path = spec.path;
    p.musicFirst = musicFirst;
    p.musicTotal = musicTotal;
    if (!p.info.ok || p.info.sectorSize != kSector || table != fp::TableKind::Dos)
        return p;
    QFile f(p.path);
    if (!f.open(QIODevice::ReadWrite))
        return p;
    const qint64 end = (musicFirst + musicTotal) * kSector;
    if (f.size() < end)
        f.resize(end);
    QByteArray entry(8, '\0');
    put32(entry, 0, quint32(musicFirst));
    put32(entry, 4, quint32(musicTotal));
    f.seek(0x1BE + 16 + 8);
    f.write(entry);
    return p;
}

} // namespace

class TestFormat : public QObject
{
    Q_OBJECT

private slots:
    void clusterSizeTable_data();
    void clusterSizeTable();
    void fatSizeIsTheSmallest();
    void limits();
    void serialNumber();
    void sectorsOfEachRow_data();
    void sectorsOfEachRow();
    void readByFsck_data();
    void readByFsck();
    void startsPastSector65535();
    void labels();
    void orderOfTheWrites();
    void aFailureLeavesAnUnreadablePartition();
    void allowedStates();
    void refusals();
    void notWrittenOutsideTheSlot();
};

void TestFormat::clusterSizeTable_data()
{
    QTest::addColumn<qint64>("total");
    QTest::addColumn<int>("clusterBytes");
    QTest::newRow("tiny") << qint64(1000) << 4096;
    QTest::newRow("8 GiB") << 8 * kGiBSectors << 4096;
    QTest::newRow("8 GiB + 1 sector") << 8 * kGiBSectors + 1 << 8192;
    QTest::newRow("16 GiB") << 16 * kGiBSectors << 8192;
    QTest::newRow("16 GiB + 1 sector") << 16 * kGiBSectors + 1 << 16384;
    QTest::newRow("32 GiB") << 32 * kGiBSectors << 16384;
    QTest::newRow("32 GiB + 1 sector") << 32 * kGiBSectors + 1 << 32768;
    QTest::newRow("1 TiB") << 1024 * kGiBSectors << 32768;
}

void TestFormat::clusterSizeTable()
{
    QFETCH(qint64, total);
    QFETCH(int, clusterBytes);
    QCOMPARE(clusterBytesFor(total), clusterBytes);
}

void TestFormat::fatSizeIsTheSmallest()
{
    // The sizes of the installer are those of the brute force, around the limits
    // of every row and on a few sizes in between.
    QList<qint64> totals;
    for (qint64 t = smallestFat32Total() - 3; t < smallestFat32Total() + 300; ++t)
        totals << t;
    for (const qint64 edge : {8 * kGiBSectors, 16 * kGiBSectors, 32 * kGiBSectors})
        for (qint64 t = edge - 2; t <= edge + 2; ++t)
            totals << t;
    totals << 1000003 << 4194304 << 12345678 << 100000000 << 1000000007;
    for (const qint64 total : totals) {
        const FatGeometry g = computeFatGeometry(total);
        const Expected e = expectedFor(total);
        if (e.clusters < 65525) {
            QVERIFY2(!g.ok, qPrintable(QString::number(total)));
            continue;
        }
        QVERIFY2(g.ok, qPrintable(QString::number(total)));
        QCOMPARE(qint64(g.sectorsPerCluster), e.spc);
        QCOMPARE(qint64(g.fatSize), e.fat);
        QCOMPARE(qint64(g.clusters), e.clusters);
        QCOMPARE(g.total, quint32(total));
    }
}

void TestFormat::limits()
{
    const qint64 smallest = smallestFat32Total();
    QVERIFY(computeFatGeometry(smallest).ok);
    QCOMPARE(computeFatGeometry(smallest).clusters, quint32(65525));
    // One cluster short: 65 524 clusters, which is FAT16 by definition.
    const FatGeometry shorter = computeFatGeometry(smallest - 1);
    QVERIFY(!shorter.ok);
    QCOMPARE(shorter.message, QStringLiteral("This partition size is not supported"));
    QCOMPARE(expectedFor(smallest - 1).clusters, qint64(65524));
    QVERIFY(!computeFatGeometry(0).ok);
    QVERIFY(!computeFatGeometry(32).ok);
    QVERIFY(!computeFatGeometry(-5).ok);
    // The sector count must fit in 32 bits.
    QVERIFY(computeFatGeometry(0xFFFFFFFFll).ok);
    QVERIFY(!computeFatGeometry(0x100000000ll).ok);
}

void TestFormat::serialNumber()
{
    QCOMPARE(volumeSerialFor(kWhen), kSerial);
    QVERIFY(volumeSerialFor(QDateTime(QDate(2026, 1, 1), QTime(0, 0, 0, 0))) != 0);
    QVERIFY(volumeSerialFor(QDateTime(QDate(2026, 11, 11), QTime(13, 45, 8, 0))) != kSerial);
}

void TestFormat::sectorsOfEachRow_data()
{
    QTest::addColumn<qint64>("total");
    QTest::addColumn<qint64>("first");
    // The row of 4 KiB starts at the smallest size that is FAT32.
    QTest::newRow("4 KiB, smallest") << smallestFat32Total() << qint64(16448);
    QTest::newRow("4 KiB, largest") << 8 * kGiBSectors << qint64(16448);
    QTest::newRow("8 KiB, smallest") << 8 * kGiBSectors + 1 << qint64(16448);
    QTest::newRow("8 KiB, largest") << 16 * kGiBSectors << qint64(16448);
    QTest::newRow("16 KiB, smallest") << 16 * kGiBSectors + 1 << qint64(16448);
    QTest::newRow("16 KiB, largest") << 32 * kGiBSectors << qint64(16448);
    QTest::newRow("32 KiB, smallest") << 32 * kGiBSectors + 1 << qint64(16448);
    QTest::newRow("32 KiB, 64 GiB") << 64 * kGiBSectors << qint64(70000);
}

void TestFormat::sectorsOfEachRow()
{
    QFETCH(qint64, total);
    QFETCH(qint64, first);
    SparseDevice dev(first + total);
    FormatOptions o;
    o.now = kWhen;
    const FormatResult r = writeFat32(dev, quint32(first), quint32(total), o);
    QVERIFY2(r.ok, qPrintable(r.message));
    QCOMPARE(r.serial, kSerial);
    QCOMPARE(qint64(r.geometry.sectorsPerCluster), expectedFor(total).spc);
    verifyFormat(dev, first, total);

    // One cluster short of the smallest size is refused, and writes nothing.
    if (total == smallestFat32Total()) {
        SparseDevice small(first + total - 1);
        const FormatResult bad = writeFat32(small, quint32(first), quint32(total - 1), o);
        QVERIFY(!bad.ok);
        QVERIFY(!bad.wroteAnything);
        QCOMPARE(bad.writeCount, 0);
        QCOMPARE(small.sector(first), QByteArray(kSector, char(kFill)));
    }
}

void TestFormat::readByFsck_data()
{
    QTest::addColumn<qint64>("total");
    QTest::addColumn<QString>("label");
    // The tool also checks that a label in the boot sector has its entry in the
    // root folder, and section 20.3 leaves the root folder empty. "NO NAME" is the
    // value that means "no label", so those rows must read clean; one row keeps
    // the default label and may only complain about that.
    const QString none = QStringLiteral("NO NAME");
    QTest::newRow("4 KiB, smallest") << smallestFat32Total() << none;
    QTest::newRow("4 KiB, 1 GiB") << kGiBSectors << none;
    QTest::newRow("8 KiB, smallest") << 8 * kGiBSectors + 1 << none;
    QTest::newRow("16 KiB, smallest") << 16 * kGiBSectors + 1 << none;
    QTest::newRow("32 KiB, smallest") << 32 * kGiBSectors + 1 << none;
    QTest::newRow("default label") << smallestFat32Total() << QString(kDefaultVolumeLabel);
}

void TestFormat::readByFsck()
{
    const QString fsck = findFsck();
    if (fsck.isEmpty())
        QSKIP("fsck.fat is not installed");
#ifndef Q_OS_LINUX
    QSKIP("the images of the large rows are sparse files, which only Linux makes cheaply");
#endif
    QFETCH(qint64, total);
    QFETCH(QString, label);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("part.img"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.resize(total * kSector));
    }
    FileSectorDevice dev(path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(dev.open());
    FormatOptions o;
    o.now = kWhen;
    o.label = label;
    const FormatResult r = writeFat32(dev, 0, quint32(total), o);
    QVERIFY2(r.ok, qPrintable(r.message));

    QProcess p;
    p.start(fsck, {QStringLiteral("-n"), QStringLiteral("-v"), path});
    QVERIFY(p.waitForFinished(120000));
    const QString output = QString::fromLocal8Bit(p.readAll());
    const Expected e = expectedFor(total);
    if (label == kDefaultVolumeLabel) {
        // Nothing is changed (-n), and the only complaint is the missing label entry.
        QVERIFY2(output.contains(QStringLiteral("no volume label in root directory")),
                 qPrintable(output));
        QVERIFY2(!output.contains(QStringLiteral("Dirty bit")) && p.exitCode() <= 1,
                 qPrintable(output));
    } else {
        QVERIFY2(p.exitCode() == 0, qPrintable(output));
    }
    // The tool reads the same sizes as the spec.
    QVERIFY2(output.contains(QStringLiteral("%1 bytes per cluster").arg(e.spc * kSector)),
             qPrintable(output));
    QVERIFY2(output.contains(QStringLiteral("(= %1 sectors)").arg(e.fat)), qPrintable(output));
    QVERIFY2(output.contains(QStringLiteral("%1 data clusters").arg(e.clusters)),
             qPrintable(output));
}

void TestFormat::startsPastSector65535()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const qint64 first = 100000;
    QVERIFY((first & 0xFFFF) != first);
    const Pod pod = makePod(dir, "late.img", "ipod-video", first, smallestFat32Total());
    QVERIFY(pod.info.ok);
    FileSectorDevice dev(pod.path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(dev.open());
    const QByteArray head = hashOf(pod.path, first * kSector);
    FormatRequest req;
    req.ipodName = req.typedName = QStringLiteral("iPod");
    req.now = kWhen;
    req.slotUnmounted = true;
    const FormatResult r = formatMusicPartition(dev, req);
    QVERIFY2(r.ok, qPrintable(r.message));
    QCOMPARE(r.slot.firstSector, quint32(first));
    const QByteArray boot = readAll(pod.path, first * kSector, kSector);
    QCOMPARE(boot.mid(28, 4), QByteArray::fromHex("A0860100")); // 100000, whole
    QCOMPARE(boot, expectedBoot(expectedFor(pod.musicTotal), pod.musicTotal, quint32(first),
                                kSerial, "IPOD       "));
    // Before the music partition, nothing changed.
    QCOMPARE(hashOf(pod.path, first * kSector), head);
}

void TestFormat::labels()
{
    SparseDevice dev(70000 + smallestFat32Total());
    FormatOptions o;
    o.now = kWhen;
    o.label = QStringLiteral("My music");
    FormatResult r = writeFat32(dev, 70000, quint32(smallestFat32Total()), o);
    QVERIFY2(r.ok, qPrintable(r.message));
    QCOMPARE(dev.sector(70000).mid(71, 11), QByteArray("MY MUSIC   "));
    for (const QString &bad :
         {QString(), QStringLiteral("TWELVE CHARS"), QStringLiteral("A/B"), QStringLiteral("é")}) {
        SparseDevice fresh(70000 + smallestFat32Total());
        o.label = bad;
        r = writeFat32(fresh, 70000, quint32(smallestFat32Total()), o);
        QVERIFY(!r.ok);
        QVERIFY(!r.wroteAnything);
    }
}

void TestFormat::orderOfTheWrites()
{
    // 8 GiB of 4 KiB clusters: 2 FATs of 8 MiB, which need several pieces.
    const qint64 total = 8 * kGiBSectors;
    const qint64 first = 70000;
    SparseDevice inner(first + total);
    FaultSectorDevice dev(inner);
    FormatOptions o;
    o.now = kWhen;
    const FormatResult r = writeFat32(dev, quint32(first), quint32(total), o);
    QVERIFY2(r.ok, qPrintable(r.message));
    const Expected e = expectedFor(total);
    const qint64 areaSectors = 32 + 2 * e.fat + e.spc;
    const QList<FaultSectorDevice::WriteRecord> log = dev.writeLog();
    QCOMPARE(log.size(), r.writeCount);

    // Step 1: zeros from the start, in pieces of at most 8 MiB, in order.
    qint64 next = first;
    int i = 0;
    for (; next < first + areaSectors; ++i) {
        QVERIFY(i < log.size());
        QCOMPARE(log[i].first, next);
        QVERIFY(log[i].count * kSector <= 8 * 1024 * 1024);
        next += log[i].count;
    }
    QCOMPARE(next, first + areaSectors);
    // Two FATs of about 8 MiB and the rest do not fit in one piece.
    QCOMPARE(qint64(i), (areaSectors + 16383) / 16384);
    QVERIFY(i >= 2);
    // Steps 2 and 3: first sectors of the FATs, FSInfo and its copy, the copy of the
    // boot sector, and the boot sector last.
    const qint64 rest[] = {32, 32 + e.fat, 1, 7, 6, 0};
    QCOMPARE(log.size(), i + 6);
    for (int k = 0; k < 6; ++k) {
        QCOMPARE(log[i + k].first, first + rest[k]);
        QCOMPARE(log[i + k].count, qint64(1));
    }
    // What was counted is what went to the disk, and every sector is in the slot.
    qint64 bytes = 0;
    for (const auto &w : log) {
        bytes += w.count * kSector;
        QVERIFY(w.first >= first && w.first + w.count <= first + total);
    }
    QCOMPARE(r.bytesWritten, bytes);
    QCOMPARE(bytes, areaSectors * kSector + 6 * kSector);
    verifyFormat(inner, first, total);

    // A smaller piece gives more writes and the same disk.
    SparseDevice inner2(first + total);
    FaultSectorDevice dev2(inner2);
    o.pieceBytes = 1024 * 1024;
    const FormatResult r2 = writeFat32(dev2, quint32(first), quint32(total), o);
    QVERIFY2(r2.ok, qPrintable(r2.message));
    QVERIFY(r2.writeCount > r.writeCount);
    for (const auto &w : dev2.writeLog())
        QVERIFY(w.count * kSector <= 1024 * 1024);
    verifyFormat(inner2, first, total);
}

void TestFormat::aFailureLeavesAnUnreadablePartition()
{
    const qint64 total = smallestFat32Total();
    const qint64 first = 70000;
    FormatOptions o;
    o.now = kWhen;
    int writes = 0;
    {
        SparseDevice probe(first + total);
        const FormatResult r = writeFat32(probe, quint32(first), quint32(total), o);
        QVERIFY(r.ok);
        writes = r.writeCount;
    }
    QVERIFY(writes >= 7);
    for (int n = 1; n <= writes; ++n) {
        SparseDevice inner(first + total);
        FaultSectorDevice dev(inner);
        dev.failAtWrite(n);
        const FormatResult r = writeFat32(dev, quint32(first), quint32(total), o);
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains(QStringLiteral("the disk is gone")));
        const QByteArray boot = inner.sector(first);
        // Before the first write the old content is still there; from then on the
        // boot sector is zero (a fault at the last write included): nothing mounts it.
        if (n == 1)
            QCOMPARE(boot, QByteArray(kSector, char(kFill)));
        else
            QCOMPARE(boot, QByteArray(kSector, '\0'));
        // Starting again from step 1 always works.
        const FormatResult again = writeFat32(inner, quint32(first), quint32(total), o);
        QVERIFY2(again.ok, qPrintable(again.message));
        verifyFormat(inner, first, total);
    }
}

void TestFormat::allowedStates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const bool installed : {false, true}) {
        const Pod pod = makePod(dir, installed ? "i.img" : "c.img", "ipod-video", 16448,
                                smallestFat32Total(), installed);
        QVERIFY(pod.info.ok);
        FileSectorDevice dev(pod.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(dev.open());
        const FormatPermission p = checkFormatAllowed(dev, true);
        QVERIFY2(p.allowed, qPrintable(p.message));
        QCOMPARE(p.slot.firstSector, quint32(16448));
        QCOMPARE(p.slot.sectorCount, quint32(smallestFat32Total()));
        QCOMPARE(p.firmware.state, installed ? State::Installed : State::Clean);
        // The firmware partition is not touched by the format.
        const QByteArray firmware =
            readAll(pod.path, pod.info.firmwareStart, pod.info.firmwareBytes);
        FormatRequest req;
        req.ipodName = req.typedName = QStringLiteral("iPod");
        req.now = kWhen;
        req.slotUnmounted = true;
        QVERIFY2(formatMusicPartition(dev, req).ok, "format");
        QCOMPARE(readAll(pod.path, pod.info.firmwareStart, pod.info.firmwareBytes), firmware);
        QVERIFY(checkFormatAllowed(dev, true).allowed);
    }
    // The type 0x0C is accepted too.
    const Pod pod = makePod(dir, "lba.img", "ipod-video", 16448, smallestFat32Total());
    {
        QFile f(pod.path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(0x1BE + 16 + 4);
        f.write(QByteArray(1, char(0x0C)));
    }
    FileSectorDevice dev(pod.path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(dev.open());
    QVERIFY(checkFormatAllowed(dev, true).allowed);
}

void TestFormat::refusals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FormatRequest req;
    req.ipodName = req.typedName = QStringLiteral("iPod");
    req.now = kWhen;
    req.slotUnmounted = true;
    const qint64 music = smallestFat32Total();

    auto refused = [&](const Pod &pod, int sectorSize, const FormatRequest &r, const QString &text,
                       bool real = false) {
        QVERIFY(pod.info.ok);
        const QByteArray before = hashOf(pod.path);
        FileSectorDevice file(pod.path, sectorSize, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.setRealDisk(real);
        const FormatResult res = formatMusicPartition(dev, r);
        QVERIFY(!res.ok);
        QVERIFY2(res.message.contains(text), qPrintable(res.message + " / wanted " + text));
        QVERIFY(!res.wroteAnything);
        QCOMPARE(dev.writeCount(), 0);
        QCOMPARE(hashOf(pod.path), before);
    };

    // Point 4: slot 2 not unmounted.
    FormatRequest mounted = req;
    mounted.slotUnmounted = false;
    refused(makePod(dir, "a.img", "ipod-video", 16448, music), kSector, mounted,
            QStringLiteral("Close the programs"));
    // Check 8: a real disk, in a build that does not write to one.
    FormatRequest noReal = req;
    noReal.allowRealWrites = false;
    refused(makePod(dir, "b.img", "ipod-video", 16448, music), kSector, noReal,
            QStringLiteral("real iPod"), true);
    // Point 2: a Mac iPod.
    refused(makePod(dir, "c.img", "ipod-video", 16448, music, false, fp::TableKind::Apm), kSector,
            req, QStringLiteral("Mac"));
    // Point 3: sectors of 2048 bytes.
    refused(makePod(dir, "d.img", "nano-2g", 16448, music), 2048, req, QStringLiteral(""));
    // Point 2: slot 2 is not FAT32 (the recognition of section 6 already refuses it).
    {
        const Pod pod = makePod(dir, "e.img", "ipod-video", 16448, music);
        QFile f(pod.path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(0x1BE + 16 + 4);
        f.write(QByteArray(1, char(0x83)));
        f.close();
        refused(pod, kSector, req, QStringLiteral("partition layout"));
    }
    // Point 1: not an iPod any more (the volume magic is broken).
    {
        const Pod pod = makePod(dir, "f.img", "ipod-video", 16448, music);
        QFile f(pod.path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(pod.info.firmwareStart + 0x100);
        f.write("XXXX");
        f.close();
        refused(pod, kSector, req, QStringLiteral(""));
    }
    // Section 20.2: a partition too small for FAT32 (the one of the fake iPod).
    refused(makePod(dir, "g.img", "ipod-video", 16448, music - 1), kSector, req,
            QStringLiteral("size is not supported"));
    // A music partition that overlaps the firmware partition is not formatted.
    refused(makePod(dir, "h.img", "ipod-video", 100, music), kSector, req,
            QStringLiteral("does not fit"));
}

void TestFormat::notWrittenOutsideTheSlot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Pod pod = makePod(dir, "x.img", "ipod-video", 16448, smallestFat32Total());
    QVERIFY(pod.info.ok);
    FileSectorDevice file(pod.path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(file.open());
    FaultSectorDevice dev(file);
    const QByteArray before = hashOf(pod.path, 16448 * kSector);
    FormatRequest req;
    req.ipodName = req.typedName = QStringLiteral("iPod");
    req.now = kWhen;
    req.slotUnmounted = true;
    const FormatResult r = formatMusicPartition(dev, req);
    QVERIFY2(r.ok, qPrintable(r.message));
    for (const auto &w : dev.writeLog())
        QVERIFY(w.first >= 16448 && w.first + w.count <= 16448 + smallestFat32Total());
    // Sector 0, the partition table and the firmware partition are as they were.
    QCOMPARE(hashOf(pod.path, 16448 * kSector), before);
}

QTEST_GUILESS_MAIN(TestFormat)
#include "tst_installer_format.moc"
