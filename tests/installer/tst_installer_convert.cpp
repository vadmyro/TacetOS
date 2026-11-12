// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Converting a Mac iPod to FAT32, and the typed confirmation (docs/specs/
// bootloader-legacy.md sections 21 and 22, tests of section 26.4 for the
// conversion). The new sector 0 is written again here from the spec.

#include "fixtures.h"
#include "support.h"

#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;
using namespace support;
namespace fp = tacetos::fakepod;

namespace {

QStringList macModels()
{
    QStringList ids;
    for (const QString &id : fp::FakePod::modelIds()) {
        const fp::ModelRow *m = fp::FakePod::model(id);
        if (m->hasMac && m->sectorSize == kSector && m->hasFirmware)
            ids << id;
    }
    return ids;
}

QByteArray expectedSector0(const Img &img)
{
    QByteArray s(kSector, '\0');
    auto put = [&](int at, quint32 v) {
        for (int i = 0; i < 4; ++i)
            s[at + i] = char((v >> (8 * i)) & 0xFF);
    };
    // Entry 1: type 0, the firmware partition. Entry 2: type 0x0B, the music partition.
    put(0x1BE + 8, quint32(img.info.firmwareStart / kSector));
    put(0x1BE + 12, quint32(img.info.firmwareBytes / kSector));
    s[0x1BE + 16 + 4] = char(0x0B);
    put(0x1BE + 16 + 8, quint32(img.musicFirst));
    put(0x1BE + 16 + 12, quint32(img.musicTotal));
    s[510] = char(0x55);
    s[511] = char(0xAA);
    return s;
}

} // namespace

class TestConvert : public QObject
{
    Q_OBJECT

private slots:
    void macBecomesAWindowsIpod_data();
    void macBecomesAWindowsIpod();
    void faultAfterTheFormatLeavesAMac();
    void faultAtEveryWrite();
    void refusals();
    void typedConfirmation();
};

void TestConvert::macBecomesAWindowsIpod_data()
{
    QTest::addColumn<QString>("model");
    for (const QString &id : macModels())
        QTest::newRow(qPrintable(id)) << id;
}

void TestConvert::macBecomesAWindowsIpod()
{
    QFETCH(QString, model);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Img img = makeImg(dir.path(), "mac.img", model, fp::TableKind::Apm);
    const Img win = makeImg(dir.path(), "win.img", model, fp::TableKind::Dos);
    QVERIFY(img.ok && win.ok);
    const QByteArray firmware = readAll(img.path, img.info.firmwareStart, img.info.firmwareBytes);
    const QByteArray mapBefore = readAll(img.path, 0, img.info.firmwareStart);

    FileSectorDevice dev(img.path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(dev.open());
    const Recognition before = recognise(dev);
    QVERIFY(before.isMac());
    const ConvertRequest req = convertRequest(dev, dir.filePath("backups"));
    const ConvertResult res = convertMacToFat32(dev, req);
    QVERIFY2(res.ok, qPrintable(res.message));
    QVERIFY(res.formatted && res.tableWritten && res.wroteAnything);

    // Recognised again as a Windows iPod of the same model, as a real Windows one is.
    FileSectorDevice after(img.path, kSector);
    QVERIFY(after.open());
    const Recognition rec = recognise(after);
    QCOMPARE(rec.verdict, Verdict::Recognised);
    QVERIFY(!rec.isMac());
    QCOMPARE(rec.target, before.target);
    FileSectorDevice windows(win.path, kSector);
    QVERIFY(windows.open());
    const Recognition ref = recognise(windows);
    QCOMPARE(rec.target, ref.target);
    QCOMPARE(rec.firmwareStart, ref.firmwareStart);
    QCOMPARE(rec.firmwareBytes, ref.firmwareBytes);
    QCOMPARE(readFirmware(after, rec).state, State::Clean);
    QCOMPARE(rec.table.slotList.size(), 4);
    QCOMPARE(rec.table.slotList.at(1).type, quint32(0x0B));
    QCOMPARE(rec.table.slotList.at(1).firstSector, quint32(img.musicFirst));
    QCOMPARE(rec.table.slotList.at(1).sectorCount, quint32(img.musicTotal));

    // The firmware partition is the same byte for byte, sector 0 is the new table,
    // and the other blocks of the old map are left where they were.
    QCOMPARE(readAll(img.path, img.info.firmwareStart, img.info.firmwareBytes), firmware);
    QCOMPARE(readAll(img.path, 0, kSector), expectedSector0(img));
    QCOMPARE(readAll(img.path, kSector, img.info.firmwareStart - kSector), mapBefore.mid(kSector));
    // The backups of section 21 step 1.
    QCOMPARE(readAll(res.backupPath), firmware);
    QCOMPARE(readAll(res.mapBackupPath), mapBefore);
    // The new file system: hidden sectors counted in full, FAT32, label IPOD.
    const QByteArray boot = readAll(img.path, img.musicFirst * kSector, kSector);
    QCOMPARE(boot.mid(82, 8), QByteArray("FAT32   "));
    QCOMPARE(boot.mid(71, 11), QByteArray("IPOD       "));
    QCOMPARE(quint8(boot.at(28)) | (quint8(boot.at(29)) << 8), int(img.musicFirst & 0xFFFF));
    QCOMPARE(quint8(boot.at(510)), quint8(0x55));
}

void TestConvert::faultAfterTheFormatLeavesAMac()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Img img = makeImg(dir.path(), "mac.img", "ipod-video", fp::TableKind::Apm);
    QVERIFY(img.ok);
    const QByteArray sector0 = readAll(img.path, 0, kSector);

    FileSectorDevice file(img.path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(file.open());
    FaultSectorDevice dev(file);
    dev.failAtWrite(0);
    const ConvertRequest req = convertRequest(dev, dir.filePath("backups"));
    // Count the writes of a whole conversion on a copy: the last one is sector 0.
    // Here the table write is the one that fails.
    QVERIFY(QFile::copy(img.path, dir.filePath("count.img")));
    FileSectorDevice countFile(dir.filePath("count.img"), kSector,
                               FileSectorDevice::Mode::ReadWrite);
    QVERIFY(countFile.open());
    FaultSectorDevice counter(countFile);
    counter.failAtWrite(0);
    QVERIFY(convertMacToFat32(counter, convertRequest(counter, dir.filePath("b2"))).ok);
    const int last = counter.writeCount();
    QCOMPARE(counter.writeLog().last().first, qint64(0));

    dev.failAtWrite(last);
    const ConvertResult res = convertMacToFat32(dev, req);
    QVERIFY(!res.ok);
    QVERIFY(res.formatted && !res.tableWritten);
    // Still a Mac iPod, sector 0 untouched.
    QCOMPARE(readAll(img.path, 0, kSector), sector0);
    FileSectorDevice plain(img.path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(plain.open());
    QVERIFY(recognise(plain).isMac());
    QCOMPARE(recognise(plain).verdict, Verdict::Recognised);
    // A second conversion succeeds.
    const ConvertResult again = convertMacToFat32(plain, convertRequest(plain, dir.filePath("b3")));
    QVERIFY2(again.ok, qPrintable(again.message));
    QVERIFY(!recognise(plain).isMac());
}

void TestConvert::faultAtEveryWrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    int total = 0;
    for (int n = 1; n == 1 || n <= total; ++n) {
        const Img img =
            makeImg(dir.path(), QStringLiteral("m%1.img").arg(n), "ipod-video", fp::TableKind::Apm);
        QVERIFY(img.ok);
        const QByteArray sector0 = readAll(img.path, 0, kSector);
        const QByteArray firmware =
            readAll(img.path, img.info.firmwareStart, img.info.firmwareBytes);
        FileSectorDevice file(img.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.failAtWrite(0);
        if (total == 0) {
            // The first pass counts the writes.
            QVERIFY(convertMacToFat32(dev, convertRequest(dev, dir.filePath("c"))).ok);
            total = dev.writeCount();
            QVERIFY(total > 6);
            continue;
        }
        dev.failAtWrite(n);
        const ConvertResult res = convertMacToFat32(dev, convertRequest(dev, dir.filePath("c")));
        QVERIFY(!res.ok);
        QCOMPARE(res.tableWritten, false);
        // Until the last write, the disk is a Mac iPod with the same sector 0, and
        // Apple's firmware is the same.
        QCOMPARE(readAll(img.path, 0, kSector), sector0);
        QCOMPARE(readAll(img.path, img.info.firmwareStart, img.info.firmwareBytes), firmware);
        FileSectorDevice plain(img.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(plain.open());
        QVERIFY2(recognise(plain).isMac(), qPrintable(QString::number(n)));
        QVERIFY2(convertMacToFat32(plain, convertRequest(plain, dir.filePath("c"))).ok,
                 qPrintable(QString::number(n)));
        QVERIFY(!recognise(plain).isMac());
        if (n == total)
            break;
    }
}

void TestConvert::refusals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString backups = dir.filePath("backups");
    using Tweak = std::function<void(ConvertRequest &)>;
    auto attempt = [&](const Img &img, int sectorSize, const Tweak &tweak, const QString &text,
                       bool real = false) {
        QVERIFY(img.ok);
        const QByteArray before = hashOutside(img.path);
        FileSectorDevice file(img.path, sectorSize, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.failAtWrite(0);
        dev.setRealDisk(real);
        ConvertRequest req = convertRequest(dev, backups);
        tweak(req);
        const ConvertResult res = convertMacToFat32(dev, req);
        QVERIFY(!res.ok);
        QVERIFY2(res.message.contains(text), qPrintable(res.message + " / wanted " + text));
        QVERIFY(!res.wroteAnything);
        QCOMPARE(dev.writeCount(), 0);
        QCOMPARE(hashOutside(img.path), before);
    };
    const Tweak none = [](ConvertRequest &) {};
    const auto Apm = fp::TableKind::Apm;

    // A Windows iPod has nothing to convert.
    attempt(makeImg(dir.path(), "a.img", "ipod-video", fp::TableKind::Dos), kSector, none,
            QStringLiteral("Windows"));
    // An Installed Mac iPod.
    attempt(makeImg(dir.path(), "b.img", "ipod-video", Apm, true), kSector, none,
            QStringLiteral("not in a state"));
    // Slot 2 mounted, the build gate, the scan.
    attempt(
        makeImg(dir.path(), "c.img", "ipod-video", Apm), kSector,
        [](ConvertRequest &r) { r.slotUnmounted = false; }, QStringLiteral("Close the programs"));
    attempt(makeImg(dir.path(), "d.img", "ipod-video", Apm), kSector, none,
            QStringLiteral("real iPod"), true);
    attempt(
        makeImg(dir.path(), "e.img", "ipod-video", Apm), kSector,
        [](ConvertRequest &r) { r.scan = ScanSnapshot(); }, QStringLiteral("unplugged"));
    // The data folder cannot be made: a file stands where the folder should be.
    {
        QFile blocker(dir.filePath("blocker"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        attempt(
            makeImg(dir.path(), "f.img", "ipod-video", Apm), kSector,
            [&](ConvertRequest &r) { r.backupDir = dir.filePath("blocker"); },
            QStringLiteral("backup"));
    }
    // A music partition too small for FAT32 (the fake iPod as it is built).
    {
        fp::FakePodSpec spec;
        spec.modelId = "ipod-video";
        spec.path = dir.filePath("g.img");
        spec.table = Apm;
        Img small;
        small.info = fp::FakePod::build(spec);
        small.path = spec.path;
        small.ok = small.info.ok;
        attempt(small, kSector, none, QStringLiteral("size is not supported"));
    }
    // Sectors of 2048 bytes: refused, whatever the recognition says.
    {
        const Img img = makeImg(dir.path(), "h.img", "ipod-video", Apm);
        FileSectorDevice file(img.path, 2048, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        const ConvertPermission p = checkConvertAllowed(file, true);
        QVERIFY(!p.allowed);
        attempt(img, 2048, none, QString());
    }
    // A map with three slots, and one with more than four kept entries.
    {
        const Img img = makeImg(dir.path(), "i.img", "ipod-video", Apm);
        QFile f(img.path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray count(4, '\0');
        put32be(count, 0, 4);
        f.seek(512 + 4);
        f.write(count);
        QByteArray entry(512, '\0');
        entry.replace(0, 2, "PM");
        put32be(entry, 4, 4);
        put32be(entry, 8, 100);
        put32be(entry, 12, 1);
        entry.replace(48, 9, "Apple_HFS");
        f.seek(2048);
        f.write(entry);
        f.close();
        FileSectorDevice file(img.path, kSector);
        QVERIFY(file.open());
        QCOMPARE(recognise(file).table.slotList.size(), 3);
        attempt(img, kSector, none, QStringLiteral("partition map"));
    }
    {
        const auto id = fp::FixtureId::ApmTooManyKept;
        const QString path = dir.filePath("j.img");
        QVERIFY(fp::Fixtures::build(id, path).ok);
        Img img;
        img.path = path;
        img.ok = true;
        attempt(img, kSector, none, QString());
    }
    // `Apple_HFSX` is not the HFS partition of a Mac iPod.
    {
        const QString path = dir.filePath("k.img");
        QVERIFY(fp::Fixtures::build(fp::FixtureId::ApmHfsx, path).ok);
        Img img;
        img.path = path;
        img.ok = true;
        attempt(img, kSector, none, QString());
    }
}

void TestConvert::typedConfirmation()
{
    QVERIFY(confirmationMatches("My iPod", "My iPod"));
    QVERIFY(confirmationMatches("My iPod", "  My iPod "));
    QVERIFY(!confirmationMatches("My iPod", "my ipod"));
    QVERIFY(!confirmationMatches("My iPod", ""));
    QVERIFY(!confirmationMatches("", ""));
    QVERIFY(!confirmationMatches("  ", "  "));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Img mac = makeImg(dir.path(), "mac.img", "ipod-video", fp::TableKind::Apm);
    const Img win = makeImg(dir.path(), "win.img", "ipod-video", fp::TableKind::Dos);
    QVERIFY(mac.ok && win.ok);
    for (const QString &typed : {QString(), QStringLiteral("my ipod"), QStringLiteral("iPod")}) {
        const QByteArray macBefore = hashOutside(mac.path);
        const QByteArray winBefore = hashOutside(win.path);
        FileSectorDevice macFile(mac.path, kSector, FileSectorDevice::Mode::ReadWrite);
        FileSectorDevice winFile(win.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(macFile.open() && winFile.open());
        FaultSectorDevice macDev(macFile), winDev(winFile);
        macDev.failAtWrite(0);
        winDev.failAtWrite(0);
        ConvertRequest c = convertRequest(macDev, dir.filePath("b"));
        c.typedName = typed;
        const ConvertResult cr = convertMacToFat32(macDev, c);
        QVERIFY(!cr.ok && !cr.wroteAnything);
        QVERIFY(cr.message.contains("Type the name"));
        FormatRequest f;
        f.ipodName = "My iPod";
        f.typedName = typed;
        f.slotUnmounted = true;
        const FormatResult fr = formatMusicPartition(winDev, f);
        QVERIFY(!fr.ok && !fr.wroteAnything);
        QVERIFY(fr.message.contains("Type the name"));
        QCOMPARE(macDev.writeCount() + winDev.writeCount(), 0);
        QCOMPARE(hashOutside(mac.path), macBefore);
        QCOMPARE(hashOutside(win.path), winBefore);
    }
}

QTEST_GUILESS_MAIN(TestConvert)
#include "tst_installer_convert.moc"
