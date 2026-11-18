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
#include "progress.h"
#include "runner.h"
#include "testpackage.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
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

// The real commands, but some of them answer what the test says: the
// failures the helper only gives after a real write.
class Scripted : public HelperCommands
{
public:
    explicit Scripted(HelperCommands &inner) : m_inner(inner) {}
    HelperAnswer call(const QString &cmd, const QJsonObject &args,
                      const HelperWatch &watch = HelperWatch()) override
    {
        asked << cmd;
        const auto it = answers.constFind(cmd);
        return it == answers.constEnd() ? m_inner.call(cmd, args, watch) : it.value();
    }

    QMap<QString, HelperAnswer> answers;
    QStringList asked;

private:
    HelperCommands &m_inner;
};

HelperAnswer failure(const QString &code, const QString &next, const QString &state)
{
    HelperAnswer answer;
    answer.code = code;
    answer.detail = QStringLiteral("scripted");
    answer.data.insert(QStringLiteral("next"), next);
    answer.data.insert(QStringLiteral("state"), state);
    return answer;
}

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
        // The backup of the partition always goes through the normal helper.
        std::unique_ptr<helper::HelperClient> backupClient;
        std::unique_ptr<ClientHelperCommands> commands;
        std::unique_ptr<Scripted> scripted;
        std::unique_ptr<HelperFirmwarePartition> partition;
        FakePartition fakePartition;
        std::unique_ptr<DetectionVolumeWatch> watch;
        BackupFrom backup;
        BootFrom boot;
        QStringList ran;
        bool noBackup = false;
        // What the user answers to an offer, and how many were made.
        bool offerYes = true;
        int offers = 0;
        std::unique_ptr<WizardRunner> runner;

        void shutdown()
        {
            client->shutdown();
            backupClient->shutdown();
        }
    };

    // `kill` is a point of `tacet-helper-killtest` ("write-after:2"): the
    // helper of the commands then dies there.
    std::unique_ptr<Rig> rig(const QString &image, int sectorSize, const QString &kill = QString())
    {
        auto r = std::make_unique<Rig>();
        r->image = image;
        r->sectorSize = sectorSize;
        r->client = std::make_unique<helper::HelperClient>();
        r->backupClient = std::make_unique<helper::HelperClient>();
        r->backupClient->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        r->client->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
#ifdef TACETOS_HELPER_KILLTEST_PROGRAM
        if (!kill.isEmpty()) {
            r->client->setProgram(QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM));
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("TACETOS_TEST_KILL"), kill);
            r->client->setEnvironment(env);
        }
#else
        Q_UNUSED(kill)
#endif
        const QJsonObject target = helper::HelperClient::imageTarget(image, sectorSize);
        r->commands = std::make_unique<ClientHelperCommands>(*r->client, target);
        r->scripted = std::make_unique<Scripted>(*r->commands);
        r->partition = std::make_unique<HelperFirmwarePartition>(*r->backupClient, target);
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
        r->boot.commands = r->scripted.get();
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
                         [runner, &r](const QString &, WaitKind kind) {
                             if (kind == WaitKind::DiskMode) {
                                 runner->answer(UserAnswer());
                             } else if (kind == WaitKind::Offer) {
                                 ++r.offers;
                                 UserAnswer answer;
                                 answer.yes = r.offerYes;
                                 runner->answer(answer);
                             }
                         });
    }

    static QString runFlow(Rig &r, const QString &packageSha = QString(), bool resume = false)
    {
        WizardRunner::Start s;
        s.serial = kSerial;
        s.model = QStringLiteral("ipod");
        s.shape = {Family::Partition, FlowKind::Install, InstallPath::Keep, false};
        s.packageManifest = r.package.manifest;
        s.packageSha256 = packageSha.isEmpty() ? r.package.sha256 : packageSha;
        QSignalSpy finished(r.runner.get(), &WizardRunner::flowFinished);
        QSignalSpy stopped(r.runner.get(), &WizardRunner::flowStopped);
        const QString refused =
            resume ? r.runner->resume(kSerial, s.model, Family::Partition) : r.runner->start(s);
        if (!refused.isEmpty())
            return QStringLiteral("not started: ") + refused;
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
        r->shutdown();
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
        r->shutdown();
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
        r->shutdown();
    }

    // The facts of the step `bootloader` in the progress file.
    QJsonObject factsOf()
    {
        const ProgressRead read =
            ProgressStore(m_tmp.filePath(QStringLiteral("data"))).load(kSerial);
        return read.progress.facts.value(QString(step::bootloader)).toObject();
    }

    QString videoImage(const QString &name, bool installed, int *sectorSize)
    {
        FakePodSpec spec;
        spec.modelId = QStringLiteral("ipod-video");
        spec.path = m_tmp.filePath(QStringLiteral("%1%2.img").arg(name).arg(++m_count));
        spec.bootloaderPresent = installed;
        const auto pod = FakePod::build(spec);
        if (!pod.ok)
            return QString();
        *sectorSize = pod.sectorSize;
        return spec.path;
    }

    // Section 8.2, last paragraph: what the helper says to do after a failed
    // write, and what the step does with it.
    void failedWriteNext_data()
    {
        QTest::addColumn<QString>("next");
        QTest::addColumn<QString>("state");
        QTest::addColumn<bool>("yes");
        QTest::addColumn<int>("offers");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("fact");
        QTest::newRow("offer-retry") << "offer-retry" << "clean" << true << 0
                                     << QString(helper::err::writeFailed) << "offer-retry";
        QTest::newRow("report-state") << "report-state" << "installed" << true << 0
                                      << QString(helper::err::verifyFailed) << "report-state";
        QTest::newRow("restored") << "restored" << "clean" << true << 0
                                  << QString(helper::err::writeFailed) << "restored";
        QTest::newRow("restore-failed") << "restore-failed" << "inconsistent" << true << 0
                                        << QString(helper::err::writeFailed) << "restore-failed";
        QTest::newRow("offer-restore, yes") << "offer-restore" << "inconsistent" << true << 1
                                            << QString(error::bootloaderRestored) << "restored";
        QTest::newRow("offer-restore, no") << "offer-restore" << "inconsistent" << false << 1
                                           << QString(helper::err::checkFailed) << "";
    }

    void failedWriteNext()
    {
        QFETCH(QString, next);
        QFETCH(QString, state);
        QFETCH(bool, yes);
        QFETCH(int, offers);
        QFETCH(QString, code);
        QFETCH(QString, fact);
        int ss = 512;
        const QString image = videoImage(QStringLiteral("next"), false, &ss);
        QVERIFY(!image.isEmpty());
        auto r = rig(image, ss);
        QVERIFY(r);
        r->offerYes = yes;
        // The command is not sent to the helper: the answer is the test's.
        r->scripted->answers.insert(QString(helper::cmd::installBootloader),
                                    failure(code == QString(error::bootloaderRestored) ||
                                                    code == QString(helper::err::checkFailed)
                                                ? QString(helper::err::writeFailed)
                                                : code,
                                            next, state));
        // `restore-backup` is the real one, on the image that was not touched.
        newRunner(*r);
        const QByteArray before = fileHash(image);
        QCOMPARE(runFlow(*r), code);
        QCOMPARE(r->offers, offers);
        QCOMPARE(r->scripted->asked.contains(QString(helper::cmd::restoreBackup)),
                 next == QLatin1String("offer-restore") && yes);
        QCOMPARE(factsOf().value(QStringLiteral("next")).toString(), fact);
        QCOMPARE(fileHash(image), before);
        r->shutdown();
    }

    // Section 3.3, `bootloader`: `installed` is done, `clean` runs again,
    // `inconsistent` offers the backup. The first run is stopped at the step
    // (the state cannot be read), then the flow is resumed.
    void resumeByState_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<bool>("yes");
        QTest::addColumn<QString>("stopped");
        QTest::addColumn<int>("offers");
        QTest::newRow("clean") << "clean" << true << QString() << 0;
        QTest::newRow("installed") << "installed" << true << QString() << 0;
        QTest::newRow("inconsistent, yes")
            << "inconsistent" << true << QString(error::bootloaderRestored) << 1;
        QTest::newRow("inconsistent, no")
            << "inconsistent" << false << QString(helper::err::checkFailed) << 1;
    }

    void resumeByState()
    {
        QFETCH(QString, state);
        QFETCH(bool, yes);
        QFETCH(QString, stopped);
        QFETCH(int, offers);
        int ss = 512;
        const bool overlap = state == QLatin1String("inconsistent");
        QString image;
        if (overlap) {
            image = m_tmp.filePath(QStringLiteral("resume%1.img").arg(++m_count));
            const auto built = Fixtures::build(FixtureId::DosOverlap, image);
            QVERIFY2(built.ok, qPrintable(built.error));
            ss = built.pod.sectorSize;
        } else {
            image = videoImage(QStringLiteral("resume"), state == QLatin1String("installed"), &ss);
            QVERIFY(!image.isEmpty());
        }
        auto r = rig(image, ss);
        QVERIFY(r);
        QCOMPARE(stateOf(*r), state);
        newRunner(*r);
        r->scripted->answers.insert(
            QString(helper::cmd::readState),
            failure(QString(helper::err::helperCrashed), QString(), QString()));
        QCOMPARE(runFlow(*r), QString(helper::err::helperCrashed));
        QVERIFY(r->offers == 0);
        r->scripted->answers.clear();
        r->scripted->asked.clear();
        r->offerYes = yes;
        const QByteArray before = fileHash(image);
        QCOMPARE(runFlow(*r, QString(), true), stopped);
        QCOMPARE(r->offers, offers);
        if (state == QLatin1String("installed")) {
            QVERIFY(!r->scripted->asked.contains(QString(helper::cmd::installBootloader)));
            QVERIFY(!r->scripted->asked.contains(QString(helper::cmd::updateBootloader)));
            QCOMPARE(fileHash(image), before);
        } else if (state == QLatin1String("clean")) {
            QCOMPARE(stateOf(*r), QStringLiteral("installed"));
        }
        r->shutdown();
    }

    // Section 3.3 and bootloader-legacy.md section 19, from end to end: the
    // helper is killed at each write point of the install and of the update
    // (`tacet-helper-killtest`), then the flow is resumed with the normal
    // helper. The iPod ends `installed`, or as the backup of the flow has it
    // after the user's yes to the offer.
    void cutThenResume_data()
    {
        QTest::addColumn<QString>("model");
        QTest::addColumn<bool>("update");
        QTest::newRow("video install") << "ipod-video" << false;
        QTest::newRow("video update") << "ipod-video" << true;
        QTest::newRow("nano-2g install") << "nano-2g" << false;
        QTest::newRow("nano-2g update") << "nano-2g" << true;
    }

    void cutThenResume()
    {
#if !defined(Q_OS_LINUX) || !defined(TACETOS_HELPER_KILLTEST_PROGRAM)
        QSKIP("the cut program is built on Linux");
#else
        QFETCH(QString, model);
        QFETCH(bool, update);
        // The writes of the command, counted at each point: the same everywhere
        // (an install writes the program and the directory, an update one more).
        QList<int> perPoint;
        for (const char *point : {"write-before", "write-torn", "write-after", "flush-after"}) {
            int cuts = 0;
            for (int n = 1;; ++n) {
                const QString kill = QStringLiteral("%1:%2").arg(QLatin1String(point)).arg(n);
                const QString where =
                    QStringLiteral("%1 %2 %3").arg(model, update ? "update" : "install", kill);
                FakePodSpec spec;
                spec.modelId = model;
                spec.path = m_tmp.filePath(QStringLiteral("cut%1.img").arg(++m_count));
                spec.bootloaderPresent = update;
                const auto pod = FakePod::build(spec);
                QVERIFY2(pod.ok, qPrintable(pod.error));
                const QByteArray original = fileHash(spec.path);

                // The cut: the flow stops on `helper-crashed`, or the command ends
                // before the point and there is nothing more to cut.
                auto cutRig = rig(spec.path, pod.sectorSize, kill);
                QVERIFY2(cutRig, qPrintable(where));
                newRunner(*cutRig);
                const QString first = runFlow(*cutRig);
                cutRig->shutdown();
                if (first.isEmpty()) {
                    QVERIFY2(n > 1, qPrintable(where + ": no write at all"));
                    break;
                }
                QVERIFY2(first == QString(helper::err::helperCrashed),
                         qPrintable(where + ": " + first));
                ++cuts;
                QCOMPARE(cutRig->offers, 0);

                // The next start of the app: the normal helper.
                auto r = rig(spec.path, pod.sectorSize);
                QVERIFY2(r, qPrintable(where));
                const QString stateCut = stateOf(*r);
                QVERIFY2(stateCut == QLatin1String("clean") ||
                             stateCut == QLatin1String("installed") ||
                             stateCut == QLatin1String("inconsistent"),
                         qPrintable(where + ": " + stateCut));
                newRunner(*r);
                const QString second = runFlow(*r, QString(), true);
                if (stateCut == QLatin1String("inconsistent")) {
                    // The backup is written back, after the yes, and never before.
                    QCOMPARE(r->offers, 1);
                    QVERIFY2(second == QString(error::bootloaderRestored),
                             qPrintable(where + ": " + second));
                    QVERIFY2(fileHash(spec.path) == original,
                             qPrintable(where + ": not as the backup"));
                } else {
                    QCOMPARE(r->offers, 0);
                    QVERIFY2(second.isEmpty(), qPrintable(where + ": " + second));
                    QVERIFY2(stateOf(*r) == QLatin1String("installed"), qPrintable(where));
                }
                r->shutdown();
                if (QTest::currentTestFailed())
                    return;
            }
            perPoint << cuts;
        }
        QVERIFY2(perPoint.first() >= 2, "too few write points");
        QCOMPARE(perPoint, QList<int>(4, perPoint.first()));
#endif
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
        r->shutdown();
    }
};

QTEST_GUILESS_MAIN(TstWizardBootloader)
#include "tst_wizard_bootloader.moc"
