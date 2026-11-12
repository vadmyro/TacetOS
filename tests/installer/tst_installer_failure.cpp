// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The final verification, the install record and the failures of
// docs/specs/bootloader-legacy.md sections 16 to 19 (tests of section 26.3): a
// fault injected after every single write of every operation, for both layouts,
// and the state read again must be the one of the table of section 19; writing the
// backup back must give the original image byte for byte. Plus the uninstall with
// a record, without one, and with one that no longer matches the disk.

#include "constants.h"
#include "fakepod.h"
#include "faultdevice.h"
#include "fixtures.h"
#include "operation.h"
#include "record.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

QByteArray readAll(const QString &path, qint64 offset = 0, qint64 length = -1)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return QByteArray();
    return length < 0 ? f.readAll() : f.read(length);
}

QByteArray fileFor(const QString &target, int length)
{
    const QByteArray program = fp::BootloaderFile::program(target, length);
    const fp::BootTarget *t = fp::BootloaderFile::target(target);
    return fp::BootloaderFile::assemble(fp::BootloaderFile::checksum(t->number, program),
                                        QByteArray(t->tag), program);
}

QByteArray programFor(const QString &target, int length)
{
    return fp::BootloaderFile::program(target, length);
}

// A disk that gives a wrong byte in one sector once `trigger` writes were made:
// the final verification must see it.
class CorruptingDevice : public SectorDevice
{
public:
    CorruptingDevice(SectorDevice &inner, int trigger, qint64 sector)
        : m_inner(inner), m_trigger(trigger), m_sector(sector)
    {
    }
    int sectorSize() const override { return m_inner.sectorSize(); }
    qint64 sectorCount() const override { return m_inner.sectorCount(); }
    bool isRealDisk() const override { return false; }
    IoResult readSectors(qint64 first, qint64 count, QByteArray &out) override
    {
        const IoResult io = m_inner.readSectors(first, count, out);
        if (io && m_writes >= m_trigger && m_sector >= first && m_sector < first + count)
            out[int((m_sector - first) * sectorSize())] ^= 0x01;
        return io;
    }
    IoResult writeSectors(qint64 first, const QByteArray &data) override
    {
        ++m_writes;
        return m_inner.writeSectors(first, data);
    }
    IoResult flush() override { return m_inner.flush(); }

private:
    SectorDevice &m_inner;
    int m_trigger;
    qint64 m_sector;
    int m_writes = 0;
};

struct Scenario
{
    const char *name;
    const char *model;
    const char *target;
    Operation op;
    bool follow;
    // The program already on the iPod (0: none, a clean iPod) and the one written.
    int installedLength;
    int fileLength;
};

const Scenario kScenarios[] = {
    {"appended install, images move", "ipod-video", "ipodvideo", Operation::Install, true, 0, 0},
    {"appended install, no image to move", "ipod-video", "ipodvideo", Operation::Install, false, 0,
     0},
    {"appended update, larger, images move", "ipod-video", "ipodvideo", Operation::Update, true,
     3000, 9000},
    {"appended update, smaller", "ipod-video", "ipodvideo", Operation::Update, false, 9000, 1000},
    {"appended uninstall", "ipod-video", "ipodvideo", Operation::Uninstall, true, 3000, 0},
    {"nano install", "nano-2g", "ipodnano2g", Operation::Install, true, 0, 0},
    {"nano update", "nano-2g", "ipodnano2g", Operation::Update, true, 3000, 5000},
    {"nano uninstall", "nano-2g", "ipodnano2g", Operation::Uninstall, true, 3000, 0},
};

} // namespace

class TestFailure : public QObject
{
    Q_OBJECT

private slots:
    void aFaultAfterEveryWrite_data();
    void aFaultAfterEveryWrite();
    void verificationSeesWhatTheDiskHolds_data();
    void verificationSeesWhatTheDiskHolds();
    void restoreRefusesAnotherDisk();
    void recordIsWrittenAfterASuccessAndOnlyThen();
    void uninstallWithARecordGivesTheEntryOfApple();
    void uninstallWithoutARecordComputesItFromTheDisk();
    void uninstallWithARecordThatNoLongerMatches();

private:
    struct Img
    {
        QString path;
        fp::FakePodResult pod;
    };
    Img make(const QString &modelId, bool installed, bool follow, int programLength = 0)
    {
        Img i;
        i.path = m_dir.filePath(QStringLiteral("img%1.bin").arg(++m_count));
        fp::FakePodSpec spec;
        spec.modelId = modelId;
        spec.path = i.path;
        spec.bootloaderPresent = installed;
        spec.withRsrc = spec.withAupd = follow;
        if (installed && programLength > 0) {
            const QString target =
                fp::BootloaderFile::targetForOsValue(fp::FakePod::model(modelId)->osValue);
            spec.bootloader = programFor(target, programLength);
        }
        i.pod = fp::FakePod::build(spec);
        return i;
    }
    Img copyOf(const Img &img)
    {
        Img c = img;
        c.path = m_dir.filePath(QStringLiteral("img%1.bin").arg(++m_count));
        if (!QFile::copy(img.path, c.path))
            QTest::qFail("cannot copy the image", __FILE__, __LINE__);
        QFile(c.path).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        return c;
    }
    State stateOf(const Img &img)
    {
        FileSectorDevice dev(img.path, img.pod.sectorSize);
        if (!dev.open())
            return State::NotRead;
        return readFirmware(dev, recognise(dev)).state;
    }

    struct Run
    {
        OperationResult res;
        ScanSnapshot scan;
        int writes = 0;
    };
    // `failAt` 0: no fault.
    Run run(const Img &img, Operation op, const QByteArray &file, int failAt = 0, bool torn = false,
            bool stays = true, qint64 pieceBytes = 4 * 512, const QString &recordDir = QString(),
            const QString &backupDir = QString())
    {
        Run r;
        FileSectorDevice disk(img.path, img.pod.sectorSize, FileSectorDevice::Mode::ReadWrite);
        if (!disk.open())
            return r;
        FaultSectorDevice dev(disk);
        dev.failAtWrite(failAt);
        dev.setTornWrite(torn);
        dev.setStaysFailed(stays);
        r.scan = takeScanSnapshot(dev, recognise(dev));
        OperationRequest req;
        req.op = op;
        req.scan = r.scan;
        req.bootloaderFile = file;
        req.backupDir = backupDir.isEmpty()
                            ? m_dir.filePath(QStringLiteral("backup%1").arg(++m_count))
                            : backupDir;
        req.allowRealWrites = false;
        req.pieceBytes = pieceBytes;
        req.recordDir = recordDir;
        req.serial = QStringLiteral("SERIAL1");
        req.bootloaderVersion = QStringLiteral("0.1-test");
        r.res = runOperation(dev, req);
        r.writes = dev.writeCount();
        return r;
    }

    QTemporaryDir m_dir;
    int m_count = 0;
};

void TestFailure::aFaultAfterEveryWrite_data()
{
    QTest::addColumn<int>("scenario");
    for (int i = 0; i < int(std::size(kScenarios)); ++i)
        QTest::newRow(kScenarios[i].name) << i;
}

void TestFailure::aFaultAfterEveryWrite()
{
    QFETCH(int, scenario);
    const Scenario &sc = kScenarios[scenario];
    const bool installedAtStart = sc.op != Operation::Install;
    const State start = installedAtStart ? State::Installed : State::Clean;
    const QByteArray file =
        sc.op == Operation::Uninstall ? QByteArray() : fileFor(sc.target, sc.fileLength);
    const Img original = make(sc.model, installedAtStart, sc.follow, sc.installedLength);
    const QByteArray originalBytes = readAll(original.path);
    QCOMPARE(stateOf(original), start);

    // How many writes the operation makes when nothing fails.
    const Img dry = copyOf(original);
    const Run base = run(dry, sc.op, file);
    QVERIFY2(base.res.ok, qPrintable(base.res.message));
    QVERIFY(base.writes >= 1);
    // The 14.1 move is in pieces of 4 sectors: several writes, not just two.
    if (sc.follow && sc.op != Operation::Uninstall && QByteArray(sc.model) == "ipod-video")
        QVERIFY(base.writes >= 5);

    struct Variant
    {
        bool torn;
        bool stays;
    };
    for (const Variant v : {Variant{false, true}, Variant{true, true}, Variant{false, false}}) {
        for (int n = 1; n <= base.writes; ++n) {
            const QString where = QStringLiteral("write %1 of %2, torn %3, stays %4")
                                      .arg(n)
                                      .arg(base.writes)
                                      .arg(v.torn)
                                      .arg(v.stays);
            const Img img = copyOf(original);
            const Run r = run(img, sc.op, file, n, v.torn, v.stays);
            const OperationResult &res = r.res;
            QVERIFY2(!res.ok, qPrintable(where));
            QVERIFY2(res.wroteAnything, qPrintable(where));
            QVERIFY2(res.message.contains(QStringLiteral("the disk is gone")) ||
                         res.message.contains(QStringLiteral("backup")),
                     qPrintable(where + ": " + res.message));
            // What the disk says now is what the result says.
            const State now = stateOf(img);
            QVERIFY2(res.stateAfter == now, qPrintable(where));

            const bool moveStep = res.failedStep == WriteStep::MoveImages ||
                                  res.failedStep == WriteStep::MoveDirectory;
            if (moveStep) {
                // Section 19: broken until restored, so restored at once. With a
                // disk that stays gone, the restore fails and the app says so.
                QVERIFY2(res.next == (v.stays ? NextAction::RestoreFailed : NextAction::Restored),
                         qPrintable(where));
                QVERIFY2(res.restoredBackup == !v.stays, qPrintable(where));
                if (!v.stays) {
                    QCOMPARE(now, start);
                    QVERIFY2(readAll(img.path) == originalBytes, qPrintable(where));
                }
                if (v.stays)
                    QVERIFY2(res.message.contains(QStringLiteral("Disk Mode")), qPrintable(where));
            } else if (res.failedStep == WriteStep::WriteProgram) {
                // The directory does not point at the new bytes.
                QCOMPARE(now, State::Clean);
                QVERIFY2(res.next == NextAction::OfferRetry, qPrintable(where));
            } else if (res.failedStep == WriteStep::RemoveBootloader) {
                // The first directory write of an update failed, nothing changed.
                QCOMPARE(now, State::Installed);
                QVERIFY2(res.next == NextAction::ReportState, qPrintable(where));
            } else {
                QCOMPARE(int(res.failedStep), int(WriteStep::WriteDirectory));
                // The failing directory write put nothing on the disk.
                if (sc.op == Operation::Uninstall) {
                    QCOMPARE(now, State::Installed);
                    QVERIFY2(res.next == NextAction::ReportState, qPrintable(where));
                } else {
                    QCOMPARE(now, State::Clean);
                    QVERIFY2(res.next == (sc.op == Operation::Update ? NextAction::OfferRetry
                                                                     : NextAction::ReportState),
                             qPrintable(where));
                }
            }

            // Writing the backup back, on a disk that works again, gives the
            // original image byte for byte.
            FileSectorDevice disk(img.path, img.pod.sectorSize, FileSectorDevice::Mode::ReadWrite);
            QVERIFY(disk.open());
            const RestoreResult back = restoreBackup(disk, r.scan, res.report.backupPath, 4 * 512);
            QVERIFY2(back.ok, qPrintable(where + ": " + back.message));
            QVERIFY2(readAll(img.path) == originalBytes, qPrintable(where));
            QVERIFY(QFile::exists(res.report.backupPath));
        }
    }
}

void TestFailure::verificationSeesWhatTheDiskHolds_data()
{
    QTest::addColumn<int>("scenario");
    for (int i = 0; i < int(std::size(kScenarios)); ++i) {
        if (kScenarios[i].op != Operation::Uninstall)
            QTest::newRow(kScenarios[i].name) << i;
    }
}

void TestFailure::verificationSeesWhatTheDiskHolds()
{
    QFETCH(int, scenario);
    const Scenario &sc = kScenarios[scenario];
    const QByteArray file = fileFor(sc.target, sc.fileLength);
    const Img original = make(sc.model, sc.op == Operation::Update, sc.follow, sc.installedLength);
    const Img dry = copyOf(original);
    const Run base = run(dry, sc.op, file);
    QVERIFY2(base.res.ok, qPrintable(base.res.message));
    const RoomPlan &plan = base.res.report.plan;
    // The first sector of the new program, as it is on the disk.
    const int ss = original.pod.sectorSize;
    const qint64 programSector = (original.pod.firmwareStart + plan.dataStart) / ss;

    const Img img = copyOf(original);
    FileSectorDevice disk(img.path, ss, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(disk.open());
    CorruptingDevice dev(disk, base.writes, programSector);
    OperationRequest req;
    req.op = sc.op;
    req.scan = takeScanSnapshot(dev, recognise(dev));
    req.bootloaderFile = file;
    req.backupDir = m_dir.filePath(QStringLiteral("backup%1").arg(++m_count));
    req.allowRealWrites = false;
    req.pieceBytes = 4 * 512;
    const OperationResult res = runOperation(dev, req);
    QVERIFY(!res.ok);
    QCOMPARE(int(res.failedStep), int(WriteStep::Verify));
    QVERIFY2(res.message.contains(QStringLiteral("the check after the writes failed")),
             qPrintable(res.message));
    // The directory itself is coherent: say the state, do not restore.
    QCOMPARE(res.stateAfter, State::Installed);
    QCOMPARE(int(res.next), int(NextAction::ReportState));
    QVERIFY(res.recordPath.isEmpty());
}

void TestFailure::restoreRefusesAnotherDisk()
{
    const Img a = make("ipod-video", false, true);
    const Img b = make("ipod-3g", false, true);
    const Run r = run(a, Operation::Install, fileFor("ipodvideo", 0));
    QVERIFY(r.res.ok);
    FileSectorDevice other(b.path, b.pod.sectorSize, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(other.open());
    const QByteArray before = readAll(b.path);
    const RestoreResult back = restoreBackup(other, r.scan, r.res.report.backupPath);
    QVERIFY(!back.ok);
    QVERIFY2(readAll(b.path) == before, "nothing is written on a disk that is not the one");
}

void TestFailure::recordIsWrittenAfterASuccessAndOnlyThen()
{
    for (const char *model : {"ipod-video", "nano-2g"}) {
        const QString target =
            fp::BootloaderFile::targetForOsValue(fp::FakePod::model(model)->osValue);
        const Img img = make(model, false, true);
        const QByteArray entryBefore =
            readAll(img.path, img.pod.firmwareStart + img.pod.directoryOffset, kDirectoryEntrySize);
        const QString records = m_dir.filePath(QStringLiteral("records%1").arg(++m_count));

        // A failed operation leaves no record.
        const Run failed =
            run(img, Operation::Install, fileFor(target, 0), 2, false, true, 4 * 512, records);
        QVERIFY(!failed.res.ok);
        QVERIFY(failed.res.recordPath.isEmpty());
        QVERIFY(!QDir(records).exists() || QDir(records).entryList(QDir::Files).isEmpty());
        FileSectorDevice disk(img.path, img.pod.sectorSize, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(disk.open());
        QVERIFY(restoreBackup(disk, failed.scan, failed.res.report.backupPath).ok);

        const QString backups = m_dir.filePath(QStringLiteral("backups%1").arg(++m_count));
        const Run good = run(img, Operation::Install, fileFor(target, 0), 0, false, true, 4 * 512,
                             records, backups);
        QVERIFY2(good.res.ok, qPrintable(good.res.message));
        QVERIFY(!good.res.recordPath.isEmpty());
        QVERIFY(good.res.recordMessage.isEmpty());
        QCOMPARE(QDir(records).entryList(QDir::Files).size(), 1);
        const std::optional<InstallRecord> rec = loadRecord(good.res.recordPath);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->operation, QStringLiteral("install"));
        QCOMPARE(rec->target, target);
        QCOMPARE(rec->serial, QStringLiteral("SERIAL1"));
        QCOMPARE(rec->diskBytes, qint64(QFileInfo(img.path).size()));
        QCOMPARE(rec->osEntryBefore, entryBefore);
        QCOMPARE(rec->bootloaderVersion, QStringLiteral("0.1-test"));
        QCOMPARE(rec->bootloaderChecksum, good.res.report.file.headerChecksum);
        QCOMPARE(rec->delta, good.res.report.plan.moveDelta);
        QVERIFY(QFile::exists(QDir(backups).filePath(rec->backupFile)));
        // The record is on the computer, not in the disk image.
        QVERIFY(!good.res.recordPath.contains(img.path));
    }
}

void TestFailure::uninstallWithARecordGivesTheEntryOfApple()
{
    const Img img = make("ipod-video", false, true);
    const qint64 entryAt = img.pod.firmwareStart + img.pod.directoryOffset;
    const QByteArray entryBefore = readAll(img.path, entryAt, kDirectoryEntrySize);
    const QString records = m_dir.filePath(QStringLiteral("records%1").arg(++m_count));
    QVERIFY(
        run(img, Operation::Install, fileFor("ipodvideo", 3000), 0, false, true, 4 * 512, records)
            .res.ok);
    // An update keeps its own record, which is not the one an uninstall goes back to.
    QVERIFY(
        run(img, Operation::Update, fileFor("ipodvideo", 9000), 0, false, true, 4 * 512, records)
            .res.ok);
    QCOMPARE(QDir(records).entryList(QDir::Files).size(), 2);

    const Run r = run(img, Operation::Uninstall, QByteArray(), 0, false, true, 4 * 512, records);
    QVERIFY2(r.res.ok, qPrintable(r.res.message));
    QVERIFY(r.res.usedRecord);
    QCOMPARE(stateOf(img), State::Clean);
    QCOMPARE(readAll(img.path, entryAt, kDirectoryEntrySize), entryBefore);
    QCOMPARE(QDir(records).entryList(QDir::Files).size(), 3);
}

void TestFailure::uninstallWithoutARecordComputesItFromTheDisk()
{
    const Img img = make("ipod-video", false, true);
    const qint64 entryAt = img.pod.firmwareStart + img.pod.directoryOffset;
    const QByteArray entryBefore = readAll(img.path, entryAt, kDirectoryEntrySize);
    QVERIFY(run(img, Operation::Install, fileFor("ipodvideo", 3000)).res.ok);
    const Run r = run(img, Operation::Uninstall, QByteArray());
    QVERIFY2(r.res.ok, qPrintable(r.res.message));
    QVERIFY(!r.res.usedRecord);
    QCOMPARE(stateOf(img), State::Clean);
    // The length is the padded one and the flash field is the one the install wrote.
    const QByteArray now = readAll(img.path, entryAt, kDirectoryEntrySize);
    QVERIFY(now != entryBefore);
    QCOMPARE(now.mid(kEntryEntryOffsetOffset, 4), QByteArray(4, '\0'));
    QCOMPARE(now.mid(kEntryChecksumOffset, 4), entryBefore.mid(kEntryChecksumOffset, 4));
    QCOMPARE(now.mid(kEntryFlashOffset, 4), QByteArray(4, '\xFF'));
}

void TestFailure::uninstallWithARecordThatNoLongerMatches()
{
    const Img img = make("ipod-video", false, true);
    const qint64 entryAt = img.pod.firmwareStart + img.pod.directoryOffset;
    const QString records = m_dir.filePath(QStringLiteral("records%1").arg(++m_count));
    QVERIFY(
        run(img, Operation::Install, fileFor("ipodvideo", 3000), 0, false, true, 4 * 512, records)
            .res.ok);
    const QByteArray installed = readAll(img.path, entryAt, kDirectoryEntrySize);
    // Apple's firmware changed since the record was written (the first byte of the
    // OSOS image): the record checksum no longer matches the disk.
    const FirmwareState fw = [&] {
        FileSectorDevice d(img.path, img.pod.sectorSize);
        d.open();
        return readFirmware(d, recognise(d));
    }();
    const qint64 appleAt = img.pod.firmwareStart + fw.imageStart(fw.entryList.at(fw.osIndex));
    QByteArray first = readAll(img.path, appleAt, 1);
    first[0] = char(first[0] ^ 0x5A);
    {
        QFile f(img.path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.seek(appleAt));
        QCOMPARE(f.write(first), qint64(1));
    }
    const Run r = run(img, Operation::Uninstall, QByteArray(), 0, false, true, 4 * 512, records);
    QVERIFY2(r.res.ok, qPrintable(r.res.message));
    QVERIFY(!r.res.usedRecord);
    QCOMPARE(stateOf(img), State::Clean);
    // The target came from the disk: the checksum is the sum of the bytes there now.
    const QByteArray now = readAll(img.path, entryAt, kDirectoryEntrySize);
    quint32 sum = 0;
    const quint32 len = quint8(now[kEntryLengthOffset]) | quint8(now[kEntryLengthOffset + 1]) << 8 |
                        quint8(now[kEntryLengthOffset + 2]) << 16 |
                        quint8(now[kEntryLengthOffset + 3]) << 24;
    for (char c : readAll(img.path, appleAt, len))
        sum += quint8(c);
    QByteArray sumBytes(4, '\0');
    for (int i = 0; i < 4; ++i)
        sumBytes[i] = char((sum >> (8 * i)) & 0xFF);
    QCOMPARE(now.mid(kEntryChecksumOffset, 4), sumBytes);
    QVERIFY(now != installed);
}

QTEST_GUILESS_MAIN(TestFailure)

#include "tst_installer_failure.moc"
