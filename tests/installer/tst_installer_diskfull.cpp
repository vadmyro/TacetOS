// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// A full disk on the computer (docs/specs/bootloader-legacy.md section 11 check 6,
// sections 18 and 23): the folder of the backup answers "no space left" and
// nothing is written to the iPod; the folder of the install record answers it after
// a successful install and the iPod stays as the install left it.

#include "bootloaderfile.h"
#include "constants.h"
#include "fakepod.h"
#include "faultdevice.h"
#include "operation.h"
#include "support.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

QByteArray hashOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

QByteArray fileFor(const QString &target, int length)
{
    const QByteArray program = fp::BootloaderFile::program(target, length);
    const fp::BootTarget *t = fp::BootloaderFile::target(target);
    return fp::BootloaderFile::assemble(fp::BootloaderFile::checksum(t->number, program),
                                        QByteArray(t->tag), program);
}

QString targetOf(const QString &modelId)
{
    return fp::BootloaderFile::targetForOsValue(fp::FakePod::model(modelId)->osValue);
}

} // namespace

class TestDiskFull : public QObject
{
    Q_OBJECT

private slots:
    void fullBackupFolderWritesNothing_data();
    void fullBackupFolderWritesNothing();
    void fullRecordFolderAfterAnInstall();

private:
    fp::FakePodResult make(const QString &path, bool installed)
    {
        fp::FakePodSpec spec;
        spec.modelId = QStringLiteral("ipod-video");
        spec.path = path;
        spec.bootloaderPresent = installed;
        return fp::FakePod::build(spec);
    }
    OperationResult run(const QString &path, const fp::FakePodResult &pod, Operation op,
                        const QString &backups, const FileFault &backupFault,
                        const FileFault &recordFault, FaultSectorDevice **watchedOut = nullptr)
    {
        m_file.reset(new FileSectorDevice(path, pod.sectorSize, FileSectorDevice::Mode::ReadWrite));
        if (!m_file->open())
            return {};
        m_watched.reset(new FaultSectorDevice(*m_file));
        m_watched->failAtWrite(0);
        if (watchedOut)
            *watchedOut = m_watched.get();
        OperationRequest req;
        req.op = op;
        req.scan = takeScanSnapshot(*m_watched, recognise(*m_watched));
        req.bootloaderFile = fileFor(targetOf(QStringLiteral("ipod-video")), 3000);
        req.backupDir = backups;
        req.recordDir = backups;
        req.backupFault = backupFault;
        req.recordFault = recordFault;
        req.allowRealWrites = false;
        return runOperation(*m_watched, req);
    }

    QTemporaryDir m_dir;
    std::unique_ptr<FileSectorDevice> m_file;
    std::unique_ptr<FaultSectorDevice> m_watched;
};

void TestDiskFull::fullBackupFolderWritesNothing_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<bool>("installed");
    QTest::addColumn<bool>("afterFirstPiece");
    QTest::newRow("install, full at once") << int(Operation::Install) << false << false;
    QTest::newRow("install, full after the first piece")
        << int(Operation::Install) << false << true;
    QTest::newRow("update, full after the first piece") << int(Operation::Update) << true << true;
    QTest::newRow("uninstall, full after the first piece")
        << int(Operation::Uninstall) << true << true;
}

// Check 6: nothing is written to the iPod, the message names the backup, and the
// copy that was cut is not left behind.
void TestDiskFull::fullBackupFolderWritesNothing()
{
    QFETCH(int, operation);
    QFETCH(bool, installed);
    QFETCH(bool, afterFirstPiece);
    const QString path = m_dir.filePath(QStringLiteral("pod.img"));
    const fp::FakePodResult pod = make(path, installed);
    QVERIFY(pod.ok);
    const QByteArray before = hashOf(path);
    const QString backups = m_dir.filePath(QStringLiteral("backups"));
    FaultSectorDevice *watched = nullptr;
    const OperationResult res = run(
        path, pod, Operation(operation), backups,
        [afterFirstPiece](const QString &, qint64 written) {
            return (!afterFirstPiece || written > 0) ? QString::fromLatin1(kNoSpace) : QString();
        },
        {}, &watched);
    QVERIFY(!res.ok);
    QCOMPARE(res.report.failingCheck, 6);
    QVERIFY2(res.message.startsWith(QStringLiteral("check 6: cannot save a backup")),
             qPrintable(res.message));
    QVERIFY2(res.report.detail.contains(QStringLiteral("No space left")),
             qPrintable(res.report.detail));
    QVERIFY(!res.wroteAnything);
    QCOMPARE(watched->writeCount(), 0);
    QCOMPARE(hashOf(path), before);
    QVERIFY(QDir(backups).entryList(QDir::Files).isEmpty());
    QVERIFY(res.report.backupPath.isEmpty());
    QVERIFY(res.recordPath.isEmpty());
}

// Section 18: the record is written after the verification. When it cannot be,
// the install is still a success, there is a message, and the iPod is exactly
// what an install with a record leaves.
void TestDiskFull::fullRecordFolderAfterAnInstall()
{
    const QString good = m_dir.filePath(QStringLiteral("good.img"));
    const QString full = m_dir.filePath(QStringLiteral("full.img"));
    const fp::FakePodResult goodPod = make(good, false);
    const fp::FakePodResult fullPod = make(full, false);
    QVERIFY(goodPod.ok && fullPod.ok);

    const OperationResult ok = run(good, goodPod, Operation::Install,
                                   m_dir.filePath(QStringLiteral("backups-good")), {}, {});
    QVERIFY2(ok.ok, qPrintable(ok.message));
    QVERIFY(!ok.recordPath.isEmpty());

    const QString backups = m_dir.filePath(QStringLiteral("backups-full"));
    const OperationResult res =
        run(full, fullPod, Operation::Install, backups, {},
            [](const QString &, qint64) { return QString::fromLatin1(kNoSpace); });
    QVERIFY2(res.ok, qPrintable(res.message));
    QVERIFY(res.recordPath.isEmpty());
    QVERIFY2(res.recordMessage.contains(QStringLiteral("No space left")),
             qPrintable(res.recordMessage));
    // Verified as Installed, the same bytes as with a record, and the backup is there.
    QCOMPARE(hashOf(full), hashOf(good));
    FileSectorDevice dev(full, fullPod.sectorSize);
    QVERIFY(dev.open());
    QCOMPARE(readFirmware(dev, recognise(dev)).state, State::Installed);
    QVERIFY(QFileInfo::exists(res.report.backupPath));
    for (const QString &name : QDir(backups).entryList(QDir::Files))
        QVERIFY2(!name.startsWith(QStringLiteral("record-")), qPrintable(name));
}

QTEST_GUILESS_MAIN(TestDiskFull)
#include "tst_installer_diskfull.moc"
