// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Finding the iPod (docs/specs/bootloader-legacy.md sections 2 to 6), on the
// fixtures of P4.1 (section 26.1) and on a few made-up maps. The expected
// results are the ones of the fixture table; the constants of section 25 are
// compared with the independent copies of the fake iPod library.

#include "constants.h"
#include "faultdevice.h"
#include "fixtures.h"
#include "models.h"
#include "recognise.h"
#include "select.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const char *name(Verdict v)
{
    switch (v) {
    case Verdict::UnknownTable:
        return "UnknownTable";
    case Verdict::PartitionTestFailed:
        return "PartitionTestFailed";
    case Verdict::NoBanner:
        return "NoBanner";
    case Verdict::BadDirectory:
        return "BadDirectory";
    case Verdict::UnknownGeneration:
        return "UnknownGeneration";
    case Verdict::BadSectorSize:
        return "BadSectorSize";
    case Verdict::ReadError:
        return "ReadError";
    case Verdict::Recognised:
        return "Recognised";
    }
    return "?";
}

const char *name(fp::Verdict v)
{
    switch (v) {
    case fp::Verdict::UnknownTable:
        return "UnknownTable";
    case fp::Verdict::PartitionTestFailed:
        return "PartitionTestFailed";
    case fp::Verdict::NoBanner:
        return "NoBanner";
    case fp::Verdict::BadDirectory:
        return "BadDirectory";
    case fp::Verdict::UnknownGeneration:
        return "UnknownGeneration";
    case fp::Verdict::BadSectorSize:
        return "BadSectorSize";
    case fp::Verdict::Recognised:
        return "Recognised";
    }
    return "?";
}

void put16be(QByteArray &b, int at, quint32 v)
{
    b[at] = char(v >> 8);
    b[at + 1] = char(v);
}

void put32be(QByteArray &b, int at, quint32 v)
{
    put16be(b, at, v >> 16);
    put16be(b, at + 2, v & 0xFFFF);
}

// One map entry of a block of `block` bytes.
QByteArray apmEntry(int block, quint32 count, quint32 first, quint32 blocks, const char *type)
{
    QByteArray e(block, '\0');
    e.replace(0, 2, "PM");
    put32be(e, 4, count);
    put32be(e, 8, first);
    put32be(e, 12, blocks);
    e.replace(16, 4, "disk");
    e.replace(48, int(qstrlen(type)), type);
    return e;
}

} // namespace

class TestRecognise : public QObject
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(m_dir.isValid()); }

    void constantsMatchTheSpec();
    void bannerIsCheckedByteForByte();
    void modelTableMatchesModelsToml();
    void modelTableValues();

    void partitionTest_data();
    void partitionTest();

    void fixturesAreRecognised_data();
    void fixturesAreRecognised();
    void recognisingNeverWrites();

    void apmUsesBytesWhenTheBlockIsNotTheSector();
    void apmRefusesAMapItCannotReadSafely_data();
    void apmRefusesAMapItCannotReadSafely();
    void apmEntryOutsideTheDisk();
    void apmKeepsFourSlotsAtMost();
    void tinyDiskIsAReadError();

    void selectNone();
    void selectOne();
    void selectSeveralNeverPicks();
    void selectPermission();
    void selectUnsupported();

private:
    // Builds fixture `id` and opens it read-only with the sector size given.
    struct Opened
    {
        QString path;
        std::unique_ptr<FileSectorDevice> dev;
        fp::FixtureBuild build;
    };
    Opened open(fp::FixtureId id, const QString &file)
    {
        Opened o;
        o.path = m_dir.filePath(file);
        o.build = fp::Fixtures::build(id, o.path);
        if (!o.build.ok)
            return o;
        const int reported = fp::Fixtures::info(id).expected.reportedSectorSize;
        o.dev.reset(new FileSectorDevice(o.path, reported ? reported : o.build.pod.sectorSize));
        if (!o.dev->open())
            o.dev.reset();
        return o;
    }
    QString writeImage(const QByteArray &bytes, const QString &file)
    {
        const QString path = m_dir.filePath(file);
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size()
                   ? path
                   : QString();
    }

    QTemporaryDir m_dir;
};

void TestRecognise::constantsMatchTheSpec()
{
    // The values of section 25, written again here.
    QCOMPARE(kDosTableOffset, 446);
    QCOMPARE(kDosTableEntries, 4);
    QCOMPARE(kDosEntrySize, 16);
    QCOMPARE(kDosSignatureOffset, 510);
    QCOMPARE(int(kDosSignature0), 0x55);
    QCOMPARE(int(kDosSignature1), 0xAA);
    QCOMPARE(int(kDosTypeFirmware), 0);
    QCOMPARE(int(kDosTypeFat32), 0x0B);
    QCOMPARE(int(kDosTypeFat32Lba), 0x0C);
    QCOMPARE(kApmMaxBlocks, 64);
    QCOMPARE(kMaxSlots, 4);
    QCOMPARE(quint32(kTypeHfs), quint32(0xFFFF));
    QCOMPARE(kBannerLength, 256);
    QCOMPARE(kVolumeMagicOffset, 0x100);
    QCOMPARE(kDirectoryPointerOffset, 0x104);
    QCOMPARE(kDirectoryPointerBase, 0x200);
    QCOMPARE(kFormatVersionOffset, 0x10A);
    QCOMPARE(kDirectoryEntrySize, 40);
    QCOMPARE(kDirectoryMaxEntries, 10);
    QCOMPARE(kDirectoryMaxBytes, 400);
    QCOMPARE(kFlashAddressUnused, quint32(0xFFFFFFFF));
    QCOMPARE(kHashBlockBytes, 0x800);
    QCOMPARE(kSectorSizeNano2g, 2048);
    QCOMPARE(kBootFileHeaderBytes, 8);
    QCOMPARE(kMaxWriteBytes, qint64(8) << 20);
    QCOMPARE(kPieceBytes, qint64(8) << 20);
    QCOMPARE(kFatReservedSectors, 32);
    QCOMPARE(kFatCount, 2);
    QCOMPARE(kFatBootCopySector, 6);
    QCOMPARE(kFatFsInfoCopySector, 7);
    QCOMPARE(kFatMinClusters, quint32(65525));
    QCOMPARE(kFatMaxClusters, quint32(0x0FFFFFF5));
    QCOMPARE(kFatFirstEntry0, quint32(0x0FFFFFF8));
    QCOMPARE(kFatFirstEntry1, quint32(0x0FFFFFFF));
    QCOMPARE(kFatFirstEntry2, quint32(0x0FFFFFFF));
    QCOMPARE(kFsInfoSignature0, quint32(0x41615252));
    QCOMPARE(kFsInfoSignature1, quint32(0x61417272));
    QCOMPARE(kFsInfoSignature2, quint32(0xAA550000));
    QCOMPARE(QString(kDefaultVolumeLabel), QStringLiteral("IPOD"));
    QCOMPARE(QString(kVolumeMagic), QStringLiteral("]ih["));
    QCOMPARE(QString(kTagOsos), QStringLiteral("soso"));
    QCOMPARE(QString(kTagRsrc), QStringLiteral("crsr"));
    QCOMPARE(QString(kTagAupd), QStringLiteral("dpua"));
    QCOMPARE(QString(kTagOsbk), QStringLiteral("kbso"));
    QCOMPARE(QString(kTagHibe), QStringLiteral("ebih"));
    QCOMPARE(QString(kDeviceTagAta), QStringLiteral("!ATA"));
    QCOMPARE(QString(kDeviceTagNan), QStringLiteral("DNAN"));
    QCOMPARE(QString(kApmTypeFirmware), QStringLiteral("Apple_MDFW"));
    QCOMPARE(QString(kApmTypeHfs), QStringLiteral("Apple_HFS"));
}

void TestRecognise::bannerIsCheckedByteForByte()
{
    const QByteArray banner = bannerBytes();
    QCOMPARE(banner.size(), 256);
    // The copy of the fake iPod library was written from the same description,
    // by another piece of code.
    QCOMPARE(banner, fp::FakePod::banner());
    QCOMPARE(banner.at(255), '\0');
    QCOMPARE(banner.left(16), QByteArray("{{~~  /-----\\   "));
    QCOMPARE(banner.mid(112, 16), QByteArray("Copyright(C) 200"));
    QVERIFY(bannerMatches(banner));
    // Anything after the 256 bytes is not looked at, a change inside them is.
    QVERIFY(bannerMatches(banner + QByteArray(100, 'x')));
    for (int i : {0, 100, 254, 255}) {
        QByteArray bad = banner;
        bad[i] = char(bad[i] ^ 1);
        QVERIFY2(!bannerMatches(bad), qPrintable(QString::number(i)));
    }
    QVERIFY(!bannerMatches(banner.left(255)));
    QVERIFY(!bannerMatches(QByteArray()));
}

void TestRecognise::modelTableMatchesModelsToml()
{
    const tacetos::device::ModelList list =
        tacetos::device::loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    int checked = 0;
    for (const auto &m : list.models) {
        const fp::ModelRow *row = fp::FakePod::model(m.id);
        QVERIFY(row);
        if (m.id == QLatin1String("classic-6g"))
            continue; // no firmware partition: in no row of section 5
        QCOMPARE(targetForModelValue(row->osValue), m.bootloaderTarget);
        ++checked;
    }
    QCOMPARE(checked, 9);
}

void TestRecognise::modelTableValues()
{
    const QList<QPair<quint32, QString>> rows = {
        {0x01, "ipod1g2g"},  {0x02, "ipod3g"},     {0x40, "ipodmini1g"},
        {0x50, "ipod4g"},    {0x60, "ipodcolor"},  {0x70, "ipodmini2g"},
        {0xB0, "ipodvideo"}, {0xC0, "ipodnano1g"}, {0x100, "ipodnano2g"},
    };
    for (const auto &r : rows)
        QCOMPARE(targetForModelValue(r.first), r.second);
    for (quint32 v : {0u, 3u, 0x33u, 0xFFu, 0x101u, 0x3305u})
        QVERIFY2(targetForModelValue(v).isEmpty(), qPrintable(QString::number(v)));
}

void TestRecognise::partitionTest_data()
{
    QTest::addColumn<quint32>("type1");
    QTest::addColumn<quint32>("first1");
    QTest::addColumn<quint32>("count1");
    QTest::addColumn<quint32>("type2");
    QTest::addColumn<bool>("forScan");
    QTest::addColumn<bool>("passes");
    QTest::newRow("windows") << 0u << 64u << 100u << 0x0Bu << true << true;
    QTest::newRow("windows-lba") << 0u << 64u << 100u << 0x0Cu << true << true;
    QTest::newRow("mac") << 0u << 64u << 100u << 0xFFFFu << true << true;
    QTest::newRow("slot1-not-zero") << 0x83u << 64u << 100u << 0x0Bu << true << false;
    QTest::newRow("slot1-empty") << 0u << 64u << 0u << 0x0Bu << true << false;
    QTest::newRow("starts-at-zero-scan") << 0u << 0u << 100u << 0x0Bu << true << false;
    QTest::newRow("starts-at-zero-no-scan") << 0u << 0u << 100u << 0x0Bu << false << true;
    QTest::newRow("extended") << 0u << 64u << 100u << 0x05u << true << false;
    QTest::newRow("ntfs") << 0u << 64u << 100u << 0x07u << true << false;
    QTest::newRow("no-second-type") << 0u << 64u << 100u << 0u << true << false;
}

void TestRecognise::partitionTest()
{
    QFETCH(quint32, type1);
    QFETCH(quint32, first1);
    QFETCH(quint32, count1);
    QFETCH(quint32, type2);
    QFETCH(bool, forScan);
    QFETCH(bool, passes);
    const QList<Slot> slotList = {{type1, first1, count1}, {type2, 200, 5000}};
    QCOMPARE(passesPartitionTest(slotList, forScan), passes);
    // Fewer than two slots never pass.
    QVERIFY(!passesPartitionTest({}, forScan));
    QVERIFY(!passesPartitionTest({slotList.first()}, forScan));
}

void TestRecognise::fixturesAreRecognised_data()
{
    QTest::addColumn<int>("id");
    for (const fp::FixtureInfo &i : fp::Fixtures::all())
        QTest::newRow(i.name) << int(i.id);
}

void TestRecognise::fixturesAreRecognised()
{
    QFETCH(int, id);
    const fp::FixtureInfo &info = fp::Fixtures::info(fp::FixtureId(id));
    const fp::Expected &want = info.expected;
    Opened o = open(fp::FixtureId(id), QStringLiteral("fixture.img"));
    QVERIFY2(o.build.ok, qPrintable(o.build.error));
    QVERIFY(o.dev);

    const Recognition r = recognise(*o.dev);
    QCOMPARE(QLatin1String(name(r.verdict)), QLatin1String(name(want.verdict)));
    QCOMPARE(r.target, want.target);

    // Section 2.3: the slots, as the table of fixtures gives them.
    QCOMPARE(r.table.slotList.size(), want.slotList.size());
    for (int i = 0; i < want.slotList.size(); ++i) {
        const Slot &s = r.table.slotList.at(i);
        const fp::SlotExpect &e = want.slotList.at(i);
        QVERIFY2(s.type == e.type && s.firstSector == e.firstSector &&
                     s.sectorCount == e.sectorCount,
                 qPrintable(QStringLiteral("slot %1").arg(i + 1)));
    }

    if (want.verdict != fp::Verdict::Recognised) {
        QCOMPARE(r.message, want.message);
    } else {
        QVERIFY(r.message.isEmpty());
        QCOMPARE(r.isMac(), QByteArray(info.name).startsWith("mac-") ||
                                QByteArray(info.name).startsWith("apm-"));
        // The firmware partition is slot 1, from the table.
        QCOMPARE(r.firmwareStart, o.build.pod.firmwareStart);
        QCOMPARE(r.firmwareBytes, o.build.pod.firmwareBytes);
        QCOMPARE(r.modelValue << 8, r.osVersion & ~0xFFu);
    }
}

void TestRecognise::recognisingNeverWrites()
{
    // The device is opened read-only, but also: a device that counts writes.
    Opened o = open(fp::FixtureId::DosIpodVideo, QStringLiteral("ro.img"));
    QVERIFY(o.dev);
    const QByteArray before =
        QCryptographicHash::hash(fileBytes(o.path), QCryptographicHash::Sha256);
    FaultSectorDevice watched(*o.dev);
    QCOMPARE(recognise(watched).verdict, Verdict::Recognised);
    QCOMPARE(watched.writeCount(), 0);
    QCOMPARE(watched.flushCount(), 0);
    QCOMPARE(QCryptographicHash::hash(fileBytes(o.path), QCryptographicHash::Sha256), before);
}

void TestRecognise::apmUsesBytesWhenTheBlockIsNotTheSector()
{
    // Section 2.3: made-up map with blocks of 1024 bytes, on a disk of 512-byte
    // sectors. The numbers are blocks: they are converted through bytes.
    QByteArray disk(1024 * 40, '\0');
    disk.replace(0, 2, "ER");
    put16be(disk, 2, 1024);
    disk.replace(1024, 1024, apmEntry(1024, 3, 1, 3, "Apple_partition_map"));
    disk.replace(2048, 1024, apmEntry(1024, 3, 9, 3, "Apple_MDFW"));
    disk.replace(3072, 1024, apmEntry(1024, 3, 12, 20, "Apple_HFS"));
    const QString p = writeImage(disk, QStringLiteral("apm1024.img"));
    FileSectorDevice dev(p, 512);
    QVERIFY(dev.open());
    const PartitionTable t = readPartitionTable(dev);
    QCOMPARE(int(t.status), int(TableStatus::Ok));
    QCOMPARE(int(t.kind), int(TableKind::Apm));
    QCOMPARE(t.slotList.size(), 2);
    QVERIFY(t.slotList.at(0) == (Slot{0, 18, 6}));
    QVERIFY(t.slotList.at(1) == (Slot{0xFFFF, 24, 40}));

    // The same map read with 2048-byte sectors: block 9 is sector 4.5, refused.
    FileSectorDevice big(p, 2048);
    QVERIFY(big.open());
    QCOMPARE(int(readPartitionTable(big).status), int(TableStatus::BadMap));
}

void TestRecognise::apmRefusesAMapItCannotReadSafely_data()
{
    QTest::addColumn<int>("blockSize");
    QTest::addColumn<quint32>("count");
    QTest::newRow("block-size-zero") << 0 << 3u;
    QTest::newRow("block-size-tiny") << 64 << 3u;
    QTest::newRow("count-zero") << 512 << 0u;
    QTest::newRow("count-65") << 512 << 65u;
    QTest::newRow("count-huge") << 512 << 0xFFFFFFFFu;
}

void TestRecognise::apmRefusesAMapItCannotReadSafely()
{
    QFETCH(int, blockSize);
    QFETCH(quint32, count);
    QByteArray disk(512 * 40, '\0');
    disk.replace(0, 2, "ER");
    put16be(disk, 2, quint32(blockSize));
    disk.replace(512, 512, apmEntry(512, count, 1, 3, "Apple_partition_map"));
    const QString p = writeImage(disk, QStringLiteral("apmbad.img"));
    FileSectorDevice dev(p, 512);
    QVERIFY(dev.open());
    const PartitionTable t = readPartitionTable(dev);
    QCOMPARE(int(t.status), int(TableStatus::BadMap));
    QVERIFY(t.slotList.isEmpty());
    // The verdict is the one of the partition test: the layout is not an iPod's.
    const Recognition r = recognise(dev);
    QCOMPARE(int(r.verdict), int(Verdict::PartitionTestFailed));
}

void TestRecognise::apmEntryOutsideTheDisk()
{
    // The map says 3 entries but the disk ends after entry 2.
    QByteArray disk(512 * 3, '\0');
    disk.replace(0, 2, "ER");
    put16be(disk, 2, 512);
    disk.replace(512, 512, apmEntry(512, 3, 1, 3, "Apple_partition_map"));
    disk.replace(1024, 512, apmEntry(512, 3, 2, 1, "Apple_MDFW"));
    const QString p = writeImage(disk, QStringLiteral("apmshort.img"));
    FileSectorDevice dev(p, 512);
    QVERIFY(dev.open());
    QCOMPARE(int(readPartitionTable(dev).status), int(TableStatus::BadMap));
}

void TestRecognise::apmKeepsFourSlotsAtMost()
{
    // 64 entries, all firmware partitions: only 4 are kept, and nothing is
    // read past the 64th block.
    QByteArray disk(512 * 70, '\0');
    disk.replace(0, 2, "ER");
    put16be(disk, 2, 512);
    for (int n = 1; n <= 64; ++n)
        disk.replace(512 * n, 512, apmEntry(512, 64, quint32(100 + n), 1, "Apple_MDFW"));
    const QString p = writeImage(disk, QStringLiteral("apm64.img"));
    FileSectorDevice dev(p, 512);
    QVERIFY(dev.open());
    const PartitionTable t = readPartitionTable(dev);
    QCOMPARE(int(t.status), int(TableStatus::Ok));
    QCOMPARE(t.slotList.size(), 4);
    QCOMPARE(t.slotList.at(3).firstSector, quint32(104));
}

void TestRecognise::tinyDiskIsAReadError()
{
    const QString p = writeImage(QByteArray(100, '\0'), QStringLiteral("tiny.img"));
    FileSectorDevice dev(p, 512);
    QVERIFY(dev.open());
    QCOMPARE(int(readPartitionTable(dev).status), int(TableStatus::ReadError));
    QCOMPARE(int(recognise(dev).verdict), int(Verdict::ReadError));
}

void TestRecognise::selectNone()
{
    const Selection none = selectIpod({});
    QCOMPARE(int(none.outcome), int(SelectOutcome::None));
    QCOMPARE(none.message, QStringLiteral("No iPod found. Is it in Disk Mode?"));

    // A disk that is not an iPod, one that could not be opened, one that is a
    // permission refusal: none of them is an iPod, and not all were refused.
    Opened stick = open(fp::FixtureId::DosNoSignature, QStringLiteral("stick.img"));
    QVERIFY(stick.dev);
    DiskCandidate a;
    a.name = QStringLiteral("/dev/sdb");
    a.device = stick.dev.get();
    DiskCandidate b;
    b.name = QStringLiteral("/dev/sdc");
    DiskCandidate c;
    c.name = QStringLiteral("/dev/sdd");
    c.permissionDenied = true;
    const Selection s = selectIpod({a, b, c});
    QCOMPARE(int(s.outcome), int(SelectOutcome::None));
    QCOMPARE(s.permissionRefused, 1);
    QVERIFY(s.ipods.isEmpty());
}

void TestRecognise::selectOne()
{
    Opened stick = open(fp::FixtureId::DosNoBanner, QStringLiteral("stick.img"));
    Opened ipod = open(fp::FixtureId::MacNano1g, QStringLiteral("ipod.img"));
    QVERIFY(stick.dev && ipod.dev);
    DiskCandidate a;
    a.name = QStringLiteral("/dev/sdb");
    a.device = stick.dev.get();
    DiskCandidate b;
    b.name = QStringLiteral("/dev/sdc");
    b.description = QStringLiteral("iPod nano");
    b.capacityBytes = 64 << 20;
    b.device = ipod.dev.get();
    DiskCandidate denied;
    denied.name = QStringLiteral("/dev/sdd");
    denied.permissionDenied = true;
    const Selection s = selectIpod({a, denied, b});
    QCOMPARE(int(s.outcome), int(SelectOutcome::One));
    QCOMPARE(s.ipods.size(), 1);
    QCOMPARE(s.ipods.at(0).index, 2);
    QCOMPARE(s.ipods.at(0).name, QStringLiteral("/dev/sdc"));
    QCOMPARE(s.ipods.at(0).capacityBytes, qint64(64 << 20));
    QCOMPARE(s.ipods.at(0).recognition.target, QStringLiteral("ipodnano1g"));
    QVERIFY(s.ipods.at(0).recognition.isMac());
    QCOMPARE(s.permissionRefused, 1);
}

void TestRecognise::selectSeveralNeverPicks()
{
    Opened one = open(fp::FixtureId::DosIpodVideo, QStringLiteral("one.img"));
    Opened two = open(fp::FixtureId::DosNano2g, QStringLiteral("two.img"));
    QVERIFY(one.dev && two.dev);
    DiskCandidate a;
    a.name = QStringLiteral("/dev/sdb");
    a.device = one.dev.get();
    DiskCandidate b;
    b.name = QStringLiteral("/dev/sdc");
    b.device = two.dev.get();
    const Selection s = selectIpod({a, b});
    QCOMPARE(int(s.outcome), int(SelectOutcome::Several));
    QCOMPARE(s.ipods.size(), 2);
    QCOMPARE(s.ipods.at(0).name, QStringLiteral("/dev/sdb"));
    QCOMPARE(s.ipods.at(1).name, QStringLiteral("/dev/sdc"));
    QCOMPARE(s.ipods.at(1).recognition.sectorSize, 2048);
}

void TestRecognise::selectPermission()
{
    DiskCandidate a;
    a.name = QStringLiteral("/dev/sdb");
    a.permissionDenied = true;
    DiskCandidate b = a;
    b.name = QStringLiteral("/dev/sdc");
    const Selection s = selectIpod({a, b});
    QCOMPARE(int(s.outcome), int(SelectOutcome::PermissionDenied));
    QCOMPARE(s.permissionRefused, 2);
    QCOMPARE(s.message, QStringLiteral("Permission denied"));

    // Recognised despite one refusal: the iPod wins.
    Opened ipod = open(fp::FixtureId::DosIpodVideo, QStringLiteral("p.img"));
    QVERIFY(ipod.dev);
    DiskCandidate c;
    c.name = QStringLiteral("/dev/sdd");
    c.device = ipod.dev.get();
    QCOMPARE(int(selectIpod({a, c}).outcome), int(SelectOutcome::One));
}

void TestRecognise::selectUnsupported()
{
    Opened odd = open(fp::FixtureId::DosUnknownValue, QStringLiteral("odd.img"));
    QVERIFY(odd.dev);
    DiskCandidate a;
    a.name = QStringLiteral("/dev/sdb");
    a.device = odd.dev.get();
    const Selection s = selectIpod({a});
    QCOMPARE(int(s.outcome), int(SelectOutcome::None));
    QVERIFY(s.ipods.isEmpty());
    QCOMPARE(s.unsupported.size(), 1);
    QCOMPARE(s.message, QStringLiteral("An iPod TacetOS does not support"));
}

QTEST_GUILESS_MAIN(TestRecognise)
#include "tst_installer_recognise.moc"
