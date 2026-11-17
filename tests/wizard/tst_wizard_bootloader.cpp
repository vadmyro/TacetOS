// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md section 8: the steps `disk-mode` and
// `bootloader` of the older iPods, run by the runner as the app does, from end
// to end: the client, the helper program, images made by the fake iPod tool.

#include "backup.h"
#include "bootloader.h"
#include "bootloaderfile.h"
#include "fakepod.h"
#include "fixtures.h"
#include "helperclient.h"
#include "helperconstants.h"
#include "runner.h"
#include "testpackage.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace tacetos::wizard;
using tacetos::fakepod::BootloaderFile;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodSpec;
using tacetos::fakepod::FixtureId;
using tacetos::fakepod::Fixtures;
using tacetos::wizard::testing::makeTestPackage;
using tacetos::wizard::testing::TestPackage;
namespace device = tacetos::device;
namespace helper = tacetos::helper;

namespace {

const QString kSerial = QStringLiteral("8K7BOOT");

QByteArray fileHash(const QString &path)
{
    QFile f(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return f.open(QIODevice::ReadOnly) && hash.addData(&f) ? hash.result() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

class NoopStep : public Step
{
public:
    NoopStep(QStringList *ran, const QString &name) : m_ran(ran), m_name(name) {}
    StepOutcome run(StepContext &) override
    {
        *m_ran << m_name;
        return StepOutcome::done();
    }

private:
    QStringList *m_ran;
    QString m_name;
};

// A copy of the firmware partition made by the test, for an image the
// helper would not back up.
class FakePartition : public FirmwarePartition
{
public:
    HelperAnswer backup(const QString &dataDir) override
    {
        const QString path = QDir(dataDir).filePath(QStringLiteral("firmware-partition.bin"));
        writeFile(path, QByteArray(4096, 'F'));
        HelperAnswer answer;
        answer.data.insert(QStringLiteral("path"), path);
        return answer;
    }
    HelperAnswer restore(const QString &, const QString &) override { return {}; }
};

class BackupFrom : public BackupSources
{
public:
    QString volumeRoot() const override { return volume; }
    QString volumeLabel() const override { return QStringLiteral("IPOD"); }
    QString backupRoot() const override { return root; }
    qint64 freeBytes(const QString &) const override { return -1; }
    qint64 volumeUsedBytes() const override { return 0; }
    FirmwarePartition *firmwarePartition() const override { return partition; }
    qint64 firmwarePartitionBytes() const override { return 0; }

    QString volume;
    QString root;
    FirmwarePartition *partition = nullptr;
};

class BootFrom : public BootloaderSources
{
public:
    HelperCommands &helper() const override { return *commands; }
    VolumeWatch &volumes() const override { return *watch; }
    QString target() const override { return bootTarget; }
    qint64 returnMs() const override { return 500; }

    HelperCommands *commands = nullptr;
    VolumeWatch *watch = nullptr;
    QString bootTarget;
};

device::DetectedIpod ready(const QString &mountPoint)
{
    device::DetectedIpod ipod;
    ipod.serial = kSerial;
    ipod.state = device::IpodState::Ready;
    ipod.volume =
        device::VolumeInfo{mountPoint, QStringLiteral("vfat"), QStringLiteral("IPOD"), 0, 0};
    return ipod;
}

} // namespace

class TstWizardBootloader : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QTemporaryDir m_runtime;
    int m_count = 0;

    QString newFolder(const QString &name)
    {
        const QString path = m_tmp.filePath(name + QString::number(++m_count));
        QDir().mkpath(path);
        return path;
    }

    // An older iPod on an image, the helper, its package, and a runner whose
    // steps other than the backup, Disk Mode and the bootloader do nothing.
    struct Rig
    {
        QString image;
        int sectorSize = 512;
        QString target;
        TestPackage package;
        std::unique_ptr<helper::HelperClient> client;
        std::unique_ptr<ClientHelperCommands> commands;
        std::unique_ptr<HelperFirmwarePartition> partition;
        FakePartition fakePartition;
        std::unique_ptr<DetectionVolumeWatch> watch;
        BackupFrom backup;
        BootFrom boot;
        QStringList ran;
        bool noBackup = false;
        std::unique_ptr<WizardRunner> runner;
    };

    std::unique_ptr<Rig> rig(const QString &image, int sectorSize)
    {
        auto r = std::make_unique<Rig>();
        r->image = image;
        r->sectorSize = sectorSize;
        r->client = std::make_unique<helper::HelperClient>();
        r->client->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        const QJsonObject target = helper::HelperClient::imageTarget(image, sectorSize);
        r->commands = std::make_unique<ClientHelperCommands>(*r->client, target);
        r->partition = std::make_unique<HelperFirmwarePartition>(*r->client, target);
        const HelperAnswer state =
            r->commands->call(QString(helper::cmd::readState), QJsonObject());
        if (!state.ok())
            return nullptr;
        r->target = state.data.value(QStringLiteral("target")).toString();
        r->package =
            makeTestPackage(newFolder(QStringLiteral("package")), r->target,
                            BootloaderFile::build(r->target), {{QStringLiteral("a.txt"), "a"}});
        r->backup.volume = newFolder(QStringLiteral("volume"));
        writeFile(r->backup.volume + QStringLiteral("/Playlists/a.m3u"), "a.mp3\n");
        r->backup.partition = r->partition.get();
        r->watch = std::make_unique<DetectionVolumeWatch>(DetectionVolumeWatch::Mounter());
        r->watch->seen(ready(r->backup.volume));
        r->boot.commands = r->commands.get();
        r->boot.watch = r->watch.get();
        r->boot.bootTarget = r->target;
        return r;
    }

    void newRunner(Rig &r)
    {
        // Each flow keeps its backups apart: two in the same second would
        // share a dated folder.
        r.backup.root = newFolder(QStringLiteral("backups"));
        r.runner.reset();
        r.runner = std::make_unique<WizardRunner>(
            m_tmp.filePath(QStringLiteral("data")),
            [&r](const QString &name) -> std::unique_ptr<Step> {
                if (name == step::backupKeep && !r.noBackup)
                    return std::make_unique<BackupStep>(BackupKind::Keep, r.backup);
                if (name == step::diskMode)
                    return std::make_unique<DiskModeStep>(r.boot);
                if (name == step::bootloader)
                    return std::make_unique<BootloaderStep>(r.boot);
                return std::make_unique<NoopStep>(&r.ran, name);
            });
        // The user presses "The iPod is in Disk Mode".
        WizardRunner *runner = r.runner.get();
        QObject::connect(runner, &WizardRunner::stepWaiting, runner,
                         [runner](const QString &, WaitKind kind) {
                             if (kind == WaitKind::DiskMode)
                                 runner->answer(UserAnswer());
                         });
    }

    static QString runFlow(Rig &r, const QString &packageSha = QString())
    {
        WizardRunner::Start s;
        s.serial = kSerial;
        s.model = QStringLiteral("ipod");
        s.shape = {Family::Partition, FlowKind::Install, InstallPath::Keep, false};
        s.packageManifest = r.package.manifest;
        s.packageSha256 = packageSha.isEmpty() ? r.package.sha256 : packageSha;
        QSignalSpy finished(r.runner.get(), &WizardRunner::flowFinished);
        QSignalSpy stopped(r.runner.get(), &WizardRunner::flowStopped);
        if (!r.runner->start(s).isEmpty())
            return QStringLiteral("not started");
        if (!QTest::qWaitFor([&] { return !finished.isEmpty() || !stopped.isEmpty(); }, 60000))
            return QStringLiteral("timeout");
        r.runner->waitFinished(10000);
        return stopped.isEmpty() ? QString() : stopped.first().first().toString();
    }

    QString stateOf(Rig &r)
    {
        return r.commands->call(QString(helper::cmd::readState), QJsonObject())
            .data.value(QStringLiteral("state"))
            .toString();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QVERIFY(m_runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
    }

    // Section 8.2 on each family of older iPods: install on a clean iPod, then
    // update.
    void installThenUpdate_data()
    {
        QTest::addColumn<QString>("model");
        for (const char *model : {"ipod-1g2g", "ipod-3g", "ipod-4g", "ipod-color", "mini-1g",
                                  "mini-2g", "ipod-video", "nano-1g", "nano-2g"})
            QTest::newRow(model) << QString::fromLatin1(model);
    }

    void installThenUpdate()
    {
        QFETCH(QString, model);
        FakePodSpec spec;
        spec.modelId = model;
        spec.path = m_tmp.filePath(QStringLiteral("%1-%2.img").arg(model).arg(++m_count));
        const auto pod = FakePod::build(spec);
        QVERIFY2(pod.ok, qPrintable(pod.error));
        auto r = rig(spec.path, pod.sectorSize);
        QVERIFY(r);
        QCOMPARE(stateOf(*r), QStringLiteral("clean"));

        newRunner(*r);
        QCOMPARE(runFlow(*r), QString());
        QCOMPARE(stateOf(*r), QStringLiteral("installed"));
        QVERIFY(r->ran.contains(QStringLiteral("firmware")));

        newRunner(*r);
        QCOMPARE(runFlow(*r), QString());
        QCOMPARE(stateOf(*r), QStringLiteral("installed"));
        r->client->shutdown();
    }

    // Section 8.2 step 2: an inconsistent iPod is not written to.
    void inconsistentStops()
    {
        const QString image = m_tmp.filePath(QStringLiteral("overlap.img"));
        const auto built = Fixtures::build(FixtureId::DosOverlap, image);
        QVERIFY2(built.ok, qPrintable(built.error));
        auto r = rig(image, built.pod.sectorSize);
        QVERIFY(r);
        QCOMPARE(stateOf(*r), QStringLiteral("inconsistent"));
        r->backup.partition = &r->fakePartition;
        const QByteArray before = fileHash(image);
        newRunner(*r);
        QCOMPARE(runFlow(*r), QString(helper::err::checkFailed));
        QCOMPARE(fileHash(image), before);
        r->client->shutdown();
    }

    // Section 8.2 step 1: no backup of the firmware partition, or a package
    // that changed since the start: nothing is sent.
    void refusedBeforeWriting_data()
    {
        QTest::addColumn<bool>("noBackup");
        QTest::addColumn<QString>("code");
        QTest::newRow("no backup") << true << QString(error::backupNotVerified);
        QTest::newRow("package changed") << false << QString(error::packageChanged);
    }

    void refusedBeforeWriting()
    {
        QFETCH(bool, noBackup);
        QFETCH(QString, code);
        FakePodSpec spec;
        spec.modelId = QStringLiteral("ipod-video");
        spec.path = m_tmp.filePath(QStringLiteral("refused%1.img").arg(++m_count));
        const auto pod = FakePod::build(spec);
        QVERIFY(pod.ok);
        auto r = rig(spec.path, pod.sectorSize);
        QVERIFY(r);
        r->noBackup = noBackup;
        const QByteArray before = fileHash(spec.path);
        newRunner(*r);
        const QString sha = noBackup ? QString() : testing::sha256Of("another manifest");
        QCOMPARE(runFlow(*r, sha), code);
        QCOMPARE(fileHash(spec.path), before);
        r->client->shutdown();
    }

    // Section 8.1: the iPod does not come back after the button.
    void diskModeNotBack()
    {
        FakePodSpec spec;
        spec.modelId = QStringLiteral("ipod-video");
        spec.path = m_tmp.filePath(QStringLiteral("gone%1.img").arg(++m_count));
        const auto pod = FakePod::build(spec);
        QVERIFY(pod.ok);
        auto r = rig(spec.path, pod.sectorSize);
        QVERIFY(r);
        // Detection shows it without its volume.
        device::DetectedIpod gone = ready(QString());
        gone.state = device::IpodState::NoVolume;
        gone.volume.reset();
        r->watch->seen(gone);
        const QByteArray before = fileHash(spec.path);
        newRunner(*r);
        QCOMPARE(runFlow(*r), QString(error::ipodNotBack));
        QVERIFY(!r->ran.contains(QStringLiteral("firmware")));
        QCOMPARE(fileHash(spec.path), before);
        r->client->shutdown();
    }
};

QTEST_GUILESS_MAIN(TstWizardBootloader)
#include "tst_wizard_bootloader.moc"
