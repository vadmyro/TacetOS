// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The 8 checks before any write (docs/specs/bootloader-legacy.md section 11),
// on the fixtures of P4.1: the first check that fails and its message for every
// fixture, and the image file the same byte for byte afterwards.

#include "checks.h"
#include "constants.h"
#include "faultdevice.h"
#include "fixtures.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray hashOf(const QString &path)
{
    return QCryptographicHash::hash(fileBytes(path), QCryptographicHash::Sha256);
}

// The good bootloader file of a target.
QByteArray goodFileFor(const QString &target)
{
    for (const fp::BootFileInfo &i : fp::Fixtures::bootFiles()) {
        if (i.ok && target == QLatin1String(i.target))
            return fp::Fixtures::bootFile(i.id);
    }
    return QByteArray();
}

} // namespace

class TestChecks : public QObject
{
    Q_OBJECT

private slots:
    void everyFixtureGivesItsFirstCheck_data();
    void everyFixtureGivesItsFirstCheck();
    void singleCheckFixtures_data();
    void singleCheckFixtures();
    void checksAreInOrder();
    void backupIsACopyOfThePartition();
    void backupNameHoldsModelAndDate();
    void backupThatCannotBeMadeRefuses();
    void noBackupBeforeTheEarlierChecksPass();
    void realDiskIsRefusedWithoutTheOption();
    void defaultGateFollowsTheBuild();
    void wrongStateIsRefused();
    void uninstallNeedsNoFile();
    void roomWithoutMoving();
    void roomWithMovingImages();
    void roomForTheNano();
    void nanoWithFullDirectory();
    void dataThatDoesNotFitIn8MiB();
    void noScanIsRefused();

private:
    struct Opened
    {
        QString path;
        std::unique_ptr<FileSectorDevice> dev;
        fp::FixtureBuild build;
    };
    Opened open(fp::FixtureId id, const QString &name = QStringLiteral("fixture.img"))
    {
        Opened o;
        o.path = m_dir.filePath(name);
        QFile::remove(o.path);
        o.build = fp::Fixtures::build(id, o.path);
        if (!o.build.ok)
            return o;
        const int reported = fp::Fixtures::info(id).expected.reportedSectorSize;
        o.dev.reset(new FileSectorDevice(o.path, reported ? reported : o.build.pod.sectorSize,
                                         FileSectorDevice::Mode::ReadWrite));
        if (!o.dev->open())
            o.dev.reset();
        return o;
    }
    // A request as the helper would make it right after a scan of this device.
    static CheckRequest requestFor(SectorDevice &dev, Operation op, const QByteArray &file,
                                   const QString &backupDir)
    {
        CheckRequest req;
        req.op = op;
        req.scan = takeScanSnapshot(dev, recognise(dev));
        req.bootloaderFile = file;
        req.backupDir = backupDir;
        req.allowRealWrites = false;
        return req;
    }
    QString freshBackupDir()
    {
        const QString dir = m_dir.filePath(QStringLiteral("backup%1").arg(++m_count));
        return dir;
    }

    QTemporaryDir m_dir;
    int m_count = 0;
};

void TestChecks::everyFixtureGivesItsFirstCheck_data()
{
    QTest::addColumn<int>("id");
    for (const fp::FixtureInfo &i : fp::Fixtures::all()) {
        if (i.expected.verdict == fp::Verdict::Recognised)
            QTest::newRow(i.name) << int(i.id);
    }
}

void TestChecks::everyFixtureGivesItsFirstCheck()
{
    QFETCH(int, id);
    const fp::FixtureInfo &info = fp::Fixtures::info(fp::FixtureId(id));
    const fp::Expected &e = info.expected;

    // The scan saw the fixture itself, or the one named by `scannedAs`.
    Opened scanned = open(e.scannedAs.value_or(info.id), QStringLiteral("scanned.img"));
    Opened o = open(info.id);
    QVERIFY(o.dev && scanned.dev);
    const ScanSnapshot snap = takeScanSnapshot(*scanned.dev, recognise(*scanned.dev));
    QVERIFY(snap.valid);

    const QByteArray file =
        info.bootFile ? fp::Fixtures::bootFile(*info.bootFile) : goodFileFor(e.target);
    QVERIFY(!file.isEmpty());
    const bool installed = e.state == fp::State::Installed;
    CheckRequest req = requestFor(*o.dev, installed ? Operation::Update : Operation::Install, file,
                                  freshBackupDir());
    req.scan = snap;

    const QByteArray before = hashOf(o.path);
    FaultSectorDevice watched(*o.dev);
    const CheckReport r = runChecks(watched, req);
    QCOMPARE(watched.writeCount(), 0);
    QCOMPARE(hashOf(o.path), before);

    QCOMPARE(r.failingCheck, e.firstFailingCheck);
    QCOMPARE(r.passed, e.firstFailingCheck == 0);
    if (e.firstFailingCheck != 0) {
        QCOMPARE(r.message, e.message);
        QVERIFY(r.warnings.isEmpty());
    } else if (e.warningCheck == 7) {
        QCOMPARE(r.warnings, QStringList{e.message});
    } else if (e.warningCheck == 0) {
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join('\n')));
    }
}

void TestChecks::singleCheckFixtures_data()
{
    QTest::addColumn<int>("check");
    for (int check : {1, 2, 3, 4, 5})
        QTest::newRow(qPrintable(QString::number(check))) << check;
}

void TestChecks::singleCheckFixtures()
{
    QFETCH(int, check);
    const auto id = fp::Fixtures::singleCheck(check);
    QVERIFY(id.has_value());
    const fp::FixtureInfo &info = fp::Fixtures::info(*id);
    Opened scanned = open(info.expected.scannedAs.value_or(*id), QStringLiteral("scanned.img"));
    Opened o = open(*id);
    QVERIFY(o.dev && scanned.dev);
    const QByteArray file =
        info.bootFile ? fp::Fixtures::bootFile(*info.bootFile) : goodFileFor(info.expected.target);
    CheckRequest req = requestFor(*o.dev, Operation::Install, file, freshBackupDir());
    req.scan = takeScanSnapshot(*scanned.dev, recognise(*scanned.dev));

    const QByteArray before = hashOf(o.path);
    FaultSectorDevice watched(*o.dev);
    const CheckReport r = runChecks(watched, req);
    QVERIFY(!r.passed);
    QCOMPARE(r.failingCheck, check);
    QCOMPARE(r.message, info.expected.message);
    QCOMPARE(watched.writeCount(), 0);
    QCOMPARE(watched.flushCount(), 0);
    QCOMPARE(hashOf(o.path), before);
    // Nothing is saved when a check before the backup fails.
    QVERIFY(!QDir(req.backupDir).exists());
}

void TestChecks::checksAreInOrder()
{
    // Check 1 comes before 2: a changed disk with a directory that is also broken.
    Opened scanned = open(fp::FixtureId::DosIpodVideo, QStringLiteral("scanned.img"));
    Opened broken = open(fp::FixtureId::DosFormatVersion4);
    QVERIFY(scanned.dev && broken.dev);
    CheckRequest req =
        requestFor(*broken.dev, Operation::Install,
                   fp::Fixtures::bootFile(fp::BootFileId::WrongByte), freshBackupDir());
    req.scan = takeScanSnapshot(*scanned.dev, recognise(*scanned.dev));
    // The first firmware sector of the broken image has `]ih[` and the version in
    // it, so the scan differs from it.
    QCOMPARE(runChecks(*broken.dev, req).failingCheck, 1);
    // Same disk as the scan: check 2 comes before 4.
    req.scan = takeScanSnapshot(*broken.dev, recognise(*broken.dev));
    const CheckReport r2 = runChecks(*broken.dev, req);
    QCOMPARE(r2.failingCheck, 2);
    QCOMPARE(r2.message, fp::Fixtures::info(fp::FixtureId::DosFormatVersion4).expected.message);

    // Check 3 comes before 4.
    Opened overlap = open(fp::FixtureId::DosOverlap);
    QVERIFY(overlap.dev);
    req = requestFor(*overlap.dev, Operation::Install,
                     fp::Fixtures::bootFile(fp::BootFileId::WrongByte), freshBackupDir());
    QCOMPARE(runChecks(*overlap.dev, req).failingCheck, 3);

    // Check 4 comes before 5, and both before the backup.
    Opened noRoom = open(fp::FixtureId::Check5NoRoom);
    QVERIFY(noRoom.dev);
    req = requestFor(*noRoom.dev, Operation::Install,
                     fp::Fixtures::bootFile(fp::BootFileId::WrongTag), freshBackupDir());
    QCOMPARE(runChecks(*noRoom.dev, req).failingCheck, 4);
    req.bootloaderFile = goodFileFor(QStringLiteral("ipodvideo"));
    QCOMPARE(runChecks(*noRoom.dev, req).failingCheck, 5);
    QVERIFY(!QDir(req.backupDir).exists());
}

void TestChecks::backupIsACopyOfThePartition()
{
    Opened o = open(fp::FixtureId::DosVideoRsrcAupd);
    QVERIFY(o.dev);
    const CheckRequest req = requestFor(*o.dev, Operation::Install,
                                        goodFileFor(QStringLiteral("ipodvideo")), freshBackupDir());
    const QByteArray before = hashOf(o.path);
    const CheckReport r = runChecks(*o.dev, req);
    QVERIFY2(r.passed, qPrintable(r.message));
    QCOMPARE(r.failingCheck, 0);
    QVERIFY(QFile::exists(r.backupPath));
    QCOMPARE(QFileInfo(r.backupPath).absolutePath(), QFileInfo(req.backupDir).absoluteFilePath());
    const QByteArray image = fileBytes(o.path);
    const QByteArray backup = fileBytes(r.backupPath);
    QCOMPARE(qint64(backup.size()), o.build.pod.firmwareBytes);
    QCOMPARE(backup, image.mid(o.build.pod.firmwareStart, o.build.pod.firmwareBytes));
    QCOMPARE(hashOf(o.path), before);
}

void TestChecks::backupNameHoldsModelAndDate()
{
    const QDateTime when(QDate(2026, 11, 11), QTime(15, 30, 5));
    QCOMPARE(backupFileName(QStringLiteral("ipodvideo"), when),
             QStringLiteral("firmware-ipodvideo-20261111-153005.bin"));

    // A second backup in the same second never overwrites the first.
    Opened o = open(fp::FixtureId::DosIpodVideo);
    QVERIFY(o.dev);
    CheckRequest req = requestFor(*o.dev, Operation::Install,
                                  goodFileFor(QStringLiteral("ipodvideo")), freshBackupDir());
    req.now = when;
    const CheckReport first = runChecks(*o.dev, req);
    const CheckReport second = runChecks(*o.dev, req);
    QVERIFY(first.passed && second.passed);
    QCOMPARE(QFileInfo(first.backupPath).fileName(),
             backupFileName(QStringLiteral("ipodvideo"), when));
    QVERIFY(first.backupPath != second.backupPath);
    QCOMPARE(fileBytes(first.backupPath), fileBytes(second.backupPath));
}

void TestChecks::backupThatCannotBeMadeRefuses()
{
    Opened o = open(fp::FixtureId::DosIpodVideo);
    QVERIFY(o.dev);
    // A folder that cannot exist: its parent is a file.
    const QString blocker = m_dir.filePath(QStringLiteral("a-file"));
    QFile f(blocker);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    CheckRequest req =
        requestFor(*o.dev, Operation::Install, goodFileFor(QStringLiteral("ipodvideo")),
                   QDir(blocker).filePath(QStringLiteral("sub")));
    const QByteArray before = hashOf(o.path);
    FaultSectorDevice watched(*o.dev);
    const CheckReport r = runChecks(watched, req);
    QVERIFY(!r.passed);
    QCOMPARE(r.failingCheck, 6);
    QVERIFY(
        r.message.startsWith(QStringLiteral("check 6: cannot save a backup in the data folder")));
    QVERIFY(r.message.contains(QString::number(o.build.pod.firmwareBytes)));
    QCOMPARE(watched.writeCount(), 0);
    QCOMPARE(hashOf(o.path), before);

    // No folder given at all.
    req.backupDir.clear();
    QCOMPARE(runChecks(*o.dev, req).failingCheck, 6);
}

void TestChecks::noBackupBeforeTheEarlierChecksPass()
{
    Opened o = open(fp::FixtureId::DosFormatVersion4);
    QVERIFY(o.dev);
    const CheckRequest req = requestFor(*o.dev, Operation::Install,
                                        goodFileFor(QStringLiteral("ipodvideo")), freshBackupDir());
    QCOMPARE(runChecks(*o.dev, req).failingCheck, 2);
    QVERIFY(!QDir(req.backupDir).exists());
}

void TestChecks::realDiskIsRefusedWithoutTheOption()
{
    Opened o = open(fp::FixtureId::DosIpodVideo);
    QVERIFY(o.dev);
    FaultSectorDevice real(*o.dev);
    real.setRealDisk(true);
    CheckRequest req = requestFor(real, Operation::Install,
                                  goodFileFor(QStringLiteral("ipodvideo")), freshBackupDir());
    const QByteArray before = hashOf(o.path);

    // A build without the option: the checks before pass, the gate refuses.
    req.allowRealWrites = false;
    const CheckReport refused = runChecks(real, req);
    QVERIFY(!refused.passed);
    QCOMPARE(refused.failingCheck, 8);
    QCOMPARE(refused.message, QStringLiteral("check 8: this build does not write to a real iPod"));
    QCOMPARE(real.writeCount(), 0);
    QCOMPARE(hashOf(o.path), before);

    // A build with the option.
    req.allowRealWrites = true;
    QVERIFY(runChecks(real, req).passed);

    // A disk image file passes in every build.
    req.allowRealWrites = false;
    const CheckReport image = runChecks(*o.dev, req);
    QVERIFY2(image.passed, qPrintable(image.message));
    QCOMPARE(hashOf(o.path), before);

    // The gate is the last check: a real disk that fails check 4 says check 4.
    req.bootloaderFile = fp::Fixtures::bootFile(fp::BootFileId::WrongByte);
    QCOMPARE(runChecks(real, req).failingCheck, 4);
}

void TestChecks::defaultGateFollowsTheBuild()
{
    Opened o = open(fp::FixtureId::DosIpodVideo);
    QVERIFY(o.dev);
    FaultSectorDevice real(*o.dev);
    real.setRealDisk(true);
    CheckRequest req = requestFor(real, Operation::Install,
                                  goodFileFor(QStringLiteral("ipodvideo")), freshBackupDir());
    CheckRequest fresh;
    QCOMPARE(fresh.allowRealWrites, realWritesAllowed());
    req.allowRealWrites = fresh.allowRealWrites;
    QCOMPARE(runChecks(real, req).passed, realWritesAllowed());
}

void TestChecks::wrongStateIsRefused()
{
    Opened clean = open(fp::FixtureId::DosIpodVideo);
    Opened installed = open(fp::FixtureId::DosIpodVideoInstalled);
    QVERIFY(clean.dev && installed.dev);
    const QByteArray file = goodFileFor(QStringLiteral("ipodvideo"));
    CheckRequest req = requestFor(*clean.dev, Operation::Update, file, freshBackupDir());
    CheckReport r = runChecks(*clean.dev, req);
    QVERIFY(!r.passed);
    QCOMPARE(r.failingCheck, 0);
    req = requestFor(*clean.dev, Operation::Uninstall, file, freshBackupDir());
    QVERIFY(!runChecks(*clean.dev, req).passed);
    req = requestFor(*installed.dev, Operation::Install, file, freshBackupDir());
    r = runChecks(*installed.dev, req);
    QVERIFY(!r.passed);
    QCOMPARE(r.failingCheck, 0);
    QVERIFY(!QDir(req.backupDir).exists());
}

void TestChecks::uninstallNeedsNoFile()
{
    Opened installed = open(fp::FixtureId::DosIpodVideoInstalled);
    QVERIFY(installed.dev);
    const CheckRequest req =
        requestFor(*installed.dev, Operation::Uninstall, QByteArray(), freshBackupDir());
    const CheckReport r = runChecks(*installed.dev, req);
    QVERIFY2(r.passed, qPrintable(r.message));
    QVERIFY(QFile::exists(r.backupPath));
}

void TestChecks::roomWithoutMoving()
{
    Opened o = open(fp::FixtureId::DosIpodVideo);
    QVERIFY(o.dev);
    const Recognition rec = recognise(*o.dev);
    const FirmwareState fw = readFirmware(*o.dev, rec);
    const qint64 len = fw.entryList.at(0).length;
    const RoomResult res = checkRoom(fw, Operation::Install, 100);
    QVERIFY(res.ok);
    QCOMPARE(res.plan.entryOffset, roundUpToSector(len, fw.sectorSize));
    QCOMPARE(res.plan.dataStart, fw.imageStart(fw.entryList.at(0)) + res.plan.entryOffset);
    QCOMPARE(res.plan.dataEnd, res.plan.dataStart + roundUpToSector(100, fw.sectorSize));
    QCOMPARE(res.plan.moveDelta, qint64(0));
}

void TestChecks::roomWithMovingImages()
{
    Opened o = open(fp::FixtureId::DosVideoRsrcAupd);
    QVERIFY(o.dev);
    const FirmwareState fw = readFirmware(*o.dev, recognise(*o.dev));
    QCOMPARE(fw.state, State::Clean);
    const int ss = fw.sectorSize;
    const qint64 nextStart = fw.imageStart(fw.entryList.at(1));
    const qint64 osStart = fw.imageStart(fw.entryList.at(0));
    const qint64 entryOffset = roundUpToSector(fw.entryList.at(0).length, ss);
    // Where the next image starts, relative to the end of Apple's firmware.
    const qint64 gap = nextStart - osStart - entryOffset;
    QVERIFY(gap >= 0 && gap % ss == 0);
    RoomResult res;
    if (gap > 0) {
        // A program that ends exactly at the next image: nothing moves.
        res = checkRoom(fw, Operation::Install, gap);
        QVERIFY(res.ok);
        QCOMPARE(res.plan.moveDelta, qint64(0));
    }
    // One byte more: the block of the following images moves by one sector.
    res = checkRoom(fw, Operation::Install, gap + 1);
    QVERIFY(res.ok);
    QCOMPARE(res.plan.moveDelta, qint64(ss));
    QCOMPARE(res.plan.moveStart, nextStart);
    const DirectoryEntry &last = fw.entryList.last();
    const qint64 blockEnd = fw.imageStart(last) + last.paddedBytes(ss);
    QCOMPARE(res.plan.moveEnd, blockEnd);
    // The block fits only up to the end of the partition.
    const qint64 spare = fw.firmwareBytes - blockEnd;
    res = checkRoom(fw, Operation::Install, gap + spare);
    QVERIFY(res.ok);
    QCOMPARE(res.plan.moveDelta, spare);
    res = checkRoom(fw, Operation::Install, gap + spare + 1);
    QVERIFY(!res.ok);
    QCOMPARE(res.message, QStringLiteral("check 5: not enough room in the firmware partition"));
}

void TestChecks::roomForTheNano()
{
    Opened clean = open(fp::FixtureId::DosNano2g);
    Opened installed = open(fp::FixtureId::DosNano2gInstalled);
    QVERIFY(clean.dev && installed.dev);
    const FirmwareState fw = readFirmware(*clean.dev, recognise(*clean.dev));
    QCOMPARE(fw.state, State::Clean);
    const int ss = fw.sectorSize;
    QCOMPARE(ss, 2048);
    const DirectoryEntry &last = fw.entryList.last();
    const qint64 program = 5 * ss + 10;
    RoomResult res = checkRoom(fw, Operation::Install, program);
    QVERIFY(res.ok);
    QCOMPARE(res.plan.newDevOffset,
             roundUpToSector(qint64(last.devOffset) + last.length, ss) + kHashBlockBytes);
    QCOMPARE(res.plan.dataStart, fw.imageArea + res.plan.newDevOffset - kHashBlockBytes);
    QCOMPARE(res.plan.dataEnd, res.plan.dataStart + 6 * ss);
    QCOMPARE(res.plan.moveDelta, qint64(0));
    // The end of the data is the end of the partition.
    const qint64 room = fw.firmwareBytes - res.plan.dataStart;
    QVERIFY(checkRoom(fw, Operation::Install, room).ok);
    QVERIFY(!checkRoom(fw, Operation::Install, room + 1).ok);
    // Not even the hash block.
    QVERIFY(!checkRoom(fw, Operation::Install, kHashBlockBytes).ok);

    // An update writes where the OSOS entry is.
    const FirmwareState inst = readFirmware(*installed.dev, recognise(*installed.dev));
    QCOMPARE(inst.state, State::Installed);
    res = checkRoom(inst, Operation::Update, program);
    QVERIFY(res.ok);
    QCOMPARE(res.plan.newDevOffset, qint64(inst.entryList.at(inst.osIndex).devOffset));
}

void TestChecks::nanoWithFullDirectory()
{
    // A clean 2nd generation nano with 10 entries has no room for the new one.
    Opened o = open(fp::FixtureId::DosNano2g);
    QVERIFY(o.dev);
    const Recognition rec = recognise(*o.dev);
    FirmwareState fw = readFirmware(*o.dev, rec);
    QCOMPARE(fw.state, State::Clean);
    // Ten entries of one OSOS and nine unknown tags, as the directory itself.
    QByteArray dir;
    QFile f(o.path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    f.seek(rec.firmwareStart + rec.directoryStart);
    const QByteArray first = f.read(40);
    QVERIFY(first.size() == 40);
    const quint32 start = quint8(first[12]) | quint8(first[13]) << 8 | quint8(first[14]) << 16 |
                          quint8(first[15]) << 24;
    QByteArray all = first;
    for (int i = 1; i < 10; ++i) {
        QByteArray e = first;
        e.replace(4, 4, "zzzz");
        const quint32 at =
            start + quint32(fw.entryList.at(0).paddedBytes(2048)) + 2048 * quint32(i - 1);
        for (int b = 0; b < 4; ++b)
            e[12 + b] = char((at >> (8 * b)) & 0xFF);
        for (int b = 0; b < 4; ++b)
            e[16 + b] = b == 0 ? '\x10' : '\0';
        all += e;
    }
    f.seek(rec.firmwareStart + rec.directoryStart);
    f.write(all);
    f.close();
    FileSectorDevice dev(o.path, rec.sectorSize);
    QVERIFY(dev.open());
    const FirmwareState full = readFirmware(dev, recognise(dev));
    QCOMPARE(full.entryList.size(), 10);
    QVERIFY2(full.state == State::Clean, qPrintable(full.message));
    const QByteArray before = hashOf(o.path);
    const CheckRequest req = requestFor(
        dev, Operation::Install, goodFileFor(QStringLiteral("ipodnano2g")), freshBackupDir());
    const CheckReport r = runChecks(dev, req);
    QVERIFY(!r.passed);
    QCOMPARE(r.failingCheck, 2);
    QCOMPARE(r.message, QStringLiteral("check 2: the directory has no room for one more entry"));
    QCOMPARE(hashOf(o.path), before);
}

void TestChecks::dataThatDoesNotFitIn8MiB()
{
    Opened o = open(fp::FixtureId::DosIpodVideo);
    QVERIFY(o.dev);
    const FirmwareState fw = readFirmware(*o.dev, recognise(*o.dev));
    const RoomResult res = checkRoom(fw, Operation::Install, 8 * 1024 * 1024);
    QVERIFY(!res.ok);
    QCOMPARE(res.message, QStringLiteral("check 5: the data to write does not fit in 8 MiB"));
}

void TestChecks::noScanIsRefused()
{
    Opened o = open(fp::FixtureId::DosIpodVideo);
    Opened bad = open(fp::FixtureId::DosNoBanner);
    QVERIFY(o.dev && bad.dev);
    CheckRequest req = requestFor(*o.dev, Operation::Install,
                                  goodFileFor(QStringLiteral("ipodvideo")), freshBackupDir());
    // A scan of a disk that was not recognised gives no snapshot.
    req.scan = takeScanSnapshot(*bad.dev, recognise(*bad.dev));
    QVERIFY(!req.scan.valid);
    const CheckReport r = runChecks(*o.dev, req);
    QVERIFY(!r.passed);
    QCOMPARE(r.failingCheck, 0);
    // A snapshot of a disk that is another disk: check 1.
    req.scan = takeScanSnapshot(*o.dev, recognise(*o.dev));
    req.scan.sectorSize = 2048;
    QCOMPARE(runChecks(*o.dev, req).failingCheck, 1);
}

QTEST_GUILESS_MAIN(TestChecks)
#include "tst_installer_checks.moc"
