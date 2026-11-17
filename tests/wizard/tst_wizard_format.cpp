// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md sections 6 and 15.4: the steps `format` and
// `convert` of a clean install. The runner runs them as the app does, the
// helper program writes images of older iPods made by the fake iPod tool, and
// the volume that comes back is a temporary folder announced by recorded
// detection events.

#include "backup.h"
#include "constants.h"
#include "fakepod.h"
#include "format.h"
#include "helperclient.h"
#include "helperconstants.h"
#include "runner.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

using namespace tacetos::wizard;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodSpec;
using tacetos::fakepod::TableKind;
namespace device = tacetos::device;
namespace helper = tacetos::helper;

namespace {

const QString kSerial = QStringLiteral("8K7TESTPOD");
const QString kModel = QStringLiteral("ipod-video");
const QString kLabel = QStringLiteral("IPOD");

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray bytesAt(const QString &path, qint64 offset, qint64 count)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return QByteArray();
    return f.read(count);
}

QByteArray fileHash(const QString &path)
{
    QFile f(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return f.open(QIODevice::ReadOnly) && hash.addData(&f) ? hash.result() : QByteArray();
}

// One event of detection about the iPod of the tests.
device::DetectedIpod detected(device::IpodState state, const QString &mountPoint = QString(),
                              const QString &serial = kSerial)
{
    device::DetectedIpod ipod;
    ipod.serial = serial;
    ipod.state = state;
    if (!mountPoint.isEmpty())
        ipod.volume = device::VolumeInfo{mountPoint, QStringLiteral("vfat"), kLabel, 0, 0};
    return ipod;
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

class BackupFrom : public BackupSources
{
public:
    QString volumeRoot() const override { return volume; }
    QString volumeLabel() const override { return kLabel; }
    QString backupRoot() const override { return root; }
    qint64 freeBytes(const QString &) const override { return -1; }
    qint64 volumeUsedBytes() const override { return 0; }
    FirmwarePartition *firmwarePartition() const override { return partition; }
    qint64 firmwarePartitionBytes() const override { return 0; }

    QString volume;
    QString root;
    FirmwarePartition *partition = nullptr;
};

class FormatFrom : public FormatSources
{
public:
    HelperCommands &helper() const override { return *commands; }
    VolumeWatch &volumes() const override { return *watch; }
    QString volumeLabel() const override { return label; }
    QString modelName() const override { return QStringLiteral("iPod Video"); }
    qint64 volumeReturnMs() const override { return 1500; }

    HelperCommands *commands = nullptr;
    VolumeWatch *watch = nullptr;
    QString label = kLabel;
};

// The helper of a Classic flow: never asked.
class NoHelper : public HelperCommands
{
public:
    HelperAnswer call(const QString &cmd, const QJsonObject &, const HelperWatch &) override
    {
        asked << cmd;
        HelperAnswer answer;
        answer.code = QStringLiteral("test-no-helper");
        return answer;
    }
    QStringList asked;
};

QMap<QString, QByteArray> snapshot(const QString &root)
{
    QMap<QString, QByteArray> all;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        all.insert(QDir(root).relativeFilePath(path),
                   f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray());
    }
    return all;
}

} // namespace

class TstWizardFormat : public QObject
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

    // An install on one image: the helper, the volume to back up, detection,
    // and a runner whose steps other than the backup and the format do nothing.
    struct Rig
    {
        QString image;
        tacetos::fakepod::FakePodResult pod;
        QByteArray firmware;
        std::unique_ptr<helper::HelperClient> client;
        std::unique_ptr<ClientHelperCommands> commands;
        std::unique_ptr<HelperFirmwarePartition> partition;
        std::unique_ptr<DetectionVolumeWatch> watch;
        BackupFrom backup;
        FormatFrom format;
        // What detection shows after the n-th mount (from 1): an empty folder
        // when the volume comes back, nothing when it does not.
        std::function<QString(int)> volumeAfterMount;
        std::atomic<int> mounts{0};
        QStringList ran;
        bool noBackup = false;
        std::unique_ptr<WizardRunner> runner;
    };

    // An iPod Video, with a music partition large enough for FAT32.
    std::unique_ptr<Rig> rig(TableKind table, const QString &program = QString(),
                             const QByteArray &kill = QByteArray())
    {
        auto r = std::make_unique<Rig>();
        FakePodSpec spec;
        spec.modelId = kModel;
        spec.path = m_tmp.filePath(QStringLiteral("pod%1.img").arg(++m_count));
        spec.table = table;
        r->image = spec.path;
        r->pod = FakePod::build(spec);
        if (!r->pod.ok || !FakePod::growMusic(r->image, r->pod.musicStart / 512, table))
            return nullptr;
        r->firmware = bytesAt(r->image, r->pod.firmwareStart, r->pod.firmwareBytes);
        useHelper(*r, program, kill);
        r->watch = std::make_unique<DetectionVolumeWatch>([this, raw = r.get()](const QString &) {
            const int n = ++raw->mounts;
            // Detection runs in the thread of the screen: the events come there.
            QMetaObject::invokeMethod(
                raw->watch.get(),
                [raw, n] {
                    raw->watch->seen(detected(device::IpodState::NoVolume));
                    const QString back =
                        raw->volumeAfterMount ? raw->volumeAfterMount(n) : QString();
                    if (!back.isEmpty())
                        raw->watch->seen(detected(device::IpodState::Ready, back));
                },
                Qt::QueuedConnection);
        });
        const QString empty = newFolder(QStringLiteral("fresh"));
        r->volumeAfterMount = [empty](int) { return empty; };
        r->backup.volume = newFolder(QStringLiteral("volume"));
        writeFile(r->backup.volume + QStringLiteral("/Music/a.mp3"), QByteArray(5000, 'm'));
        writeFile(r->backup.volume + QStringLiteral("/Music/Été/b.mp3"), QByteArray(3000, 'n'));
        writeFile(r->backup.volume + QStringLiteral("/notes.txt"), "notes");
        r->backup.root = newFolder(QStringLiteral("backups"));
        r->format.watch = r->watch.get();
        newRunner(*r);
        return r;
    }

    static void useHelper(Rig &r, const QString &program, const QByteArray &kill)
    {
        r.client = std::make_unique<helper::HelperClient>();
        r.client->setProgram(program.isEmpty() ? QStringLiteral(TACETOS_HELPER_PROGRAM) : program);
        if (!kill.isEmpty()) {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("TACETOS_TEST_KILL"), QString::fromLatin1(kill));
            r.client->setEnvironment(env);
        }
        const QJsonObject target = helper::HelperClient::imageTarget(r.image, r.pod.sectorSize);
        r.commands = std::make_unique<ClientHelperCommands>(*r.client, target);
        r.partition = std::make_unique<HelperFirmwarePartition>(*r.client, target);
        r.backup.partition = r.partition.get();
        r.format.commands = r.commands.get();
    }

    void newRunner(Rig &r)
    {
        r.runner.reset();
        r.runner = std::make_unique<WizardRunner>(
            m_tmp.filePath(QStringLiteral("data")),
            [&r](const QString &name) -> std::unique_ptr<Step> {
                if (name == step::backupFull && !r.noBackup)
                    return std::make_unique<BackupStep>(BackupKind::Full, r.backup);
                if (name == step::format)
                    return std::make_unique<FormatStep>(false, r.format);
                if (name == step::convert)
                    return std::make_unique<FormatStep>(true, r.format);
                return std::make_unique<NoopStep>(&r.ran, name);
            });
    }

    static WizardRunner::Start start(bool mac)
    {
        WizardRunner::Start s;
        s.serial = kSerial;
        s.model = kModel;
        s.shape = {Family::Partition, FlowKind::Install, InstallPath::Clean, mac};
        return s;
    }

    // Answers each wait for the typed name with `typed`, and counts them.
    static std::shared_ptr<int> answerTyped(WizardRunner &runner, const QString &typed,
                                            QJsonObject *info = nullptr)
    {
        auto asked = std::make_shared<int>(0);
        QObject::connect(
            &runner, &WizardRunner::stepWaiting, &runner,
            [&runner, typed, info, asked](const QString &, WaitKind kind, const QJsonObject &got) {
                if (kind != WaitKind::TypedName)
                    return;
                ++*asked;
                if (info)
                    *info = got;
                UserAnswer answer;
                answer.text = typed;
                runner.answer(answer);
            });
        return asked;
    }

    // Runs the flow to its end. Empty when it finished, else the code it
    // stopped on.
    static QString finish(WizardRunner &runner)
    {
        QSignalSpy finished(&runner, &WizardRunner::flowFinished);
        QSignalSpy stopped(&runner, &WizardRunner::flowStopped);
        QSignalSpy failed(&runner, &WizardRunner::stepFailed);
        if (!QTest::qWaitFor([&] { return !finished.isEmpty() || !stopped.isEmpty(); }, 120000))
            return QStringLiteral("timeout");
        runner.waitFinished(10000);
        for (const QList<QVariant> &f : std::as_const(failed))
            qInfo() << "step failed:" << f.at(1).toString() << f.at(2).toString()
                    << f.at(3).toString();
        return stopped.isEmpty() ? QString() : stopped.first().first().toString();
    }

    // Section 6.4: an iPod Classic, emptied through the file system. The volume
    // the backup reads is the one the format empties.
    struct ClassicRig
    {
        QString volume;
        NoHelper helper;
        std::unique_ptr<DetectionVolumeWatch> watch;
        BackupFrom backup;
        FormatFrom format;
        QStringList ran;
        std::unique_ptr<WizardRunner> runner;
    };

    std::unique_ptr<ClassicRig> classicRig()
    {
        auto r = std::make_unique<ClassicRig>();
        r->volume = newFolder(QStringLiteral("classic"));
        writeFile(r->volume + QStringLiteral("/Music/a.mp3"), QByteArray(5000, 'm'));
        writeFile(r->volume + QLatin1Char('/') + QString(device::kFirmwareFolder) +
                      QStringLiteral("/config.cfg"),
                  "old settings");
        writeFile(r->volume + QStringLiteral("/System Volume Information/x"), "made by Windows");
        writeFile(r->volume + QStringLiteral("/.metadata"), "hidden");
        QDir().mkpath(r->volume + QStringLiteral("/Podcasts"));
        r->watch = std::make_unique<DetectionVolumeWatch>([raw = r.get()](const QString &) {
            QMetaObject::invokeMethod(
                raw->watch.get(),
                [raw] { raw->watch->seen(detected(device::IpodState::Ready, raw->volume)); },
                Qt::QueuedConnection);
        });
        r->watch->seen(detected(device::IpodState::Ready, r->volume));
        r->backup.volume = r->volume;
        r->backup.root = newFolder(QStringLiteral("backups"));
        r->format.commands = &r->helper;
        r->format.watch = r->watch.get();
        classicRunner(*r);
        return r;
    }

    void classicRunner(ClassicRig &r)
    {
        r.runner.reset();
        r.runner = std::make_unique<WizardRunner>(
            m_tmp.filePath(QStringLiteral("data")),
            [&r](const QString &name) -> std::unique_ptr<Step> {
                if (name == step::backupFull)
                    return std::make_unique<BackupStep>(BackupKind::Full, r.backup);
                if (name == step::format)
                    return std::make_unique<FormatStep>(false, r.format);
                return std::make_unique<NoopStep>(&r.ran, name);
            });
    }

    static WizardRunner::Start classicStart()
    {
        WizardRunner::Start s;
        s.serial = kSerial;
        s.model = QStringLiteral("classic-6g");
        s.shape = {Family::Dfu, FlowKind::Install, InstallPath::Clean, false};
        return s;
    }

    bool isFormatted(const Rig &r, const QString &label = kLabel) const
    {
        return bytesAt(r.image, r.pod.musicStart + 0x52, 8) == QByteArray("FAT32   ") &&
               bytesAt(r.image, r.pod.musicStart + 0x47, 11) == label.toLatin1().leftJustified(11);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QVERIFY(m_runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
    }

    // Section 6.2: the label of the new file system.
    void labels_data()
    {
        QTest::addColumn<QString>("old");
        QTest::addColumn<QString>("label");
        QTest::newRow("kept") << "IPOD" << "IPOD";
        QTest::newRow("to uppercase") << "My iPod 2" << "MY IPOD 2";
        QTest::newRow("underscore") << "tacet_pod" << "TACET_POD";
        QTest::newRow("eleven") << "ABCDEFGHIJK" << "ABCDEFGHIJK";
        QTest::newRow("too long") << "ABCDEFGHIJKL" << "IPOD";
        QTest::newRow("none") << "" << "IPOD";
        QTest::newRow("spaces only") << "   " << "IPOD";
        QTest::newRow("accent") << QStringLiteral("Été") << "IPOD";
        QTest::newRow("dash") << "A-B" << "IPOD";
    }

    void labels()
    {
        QFETCH(QString, old);
        QFETCH(QString, label);
        QCOMPARE(newVolumeLabel(old), label);
    }

    // Section 6.3: empty, but for what the systems make there.
    void emptyVolume()
    {
        const QString root = newFolder(QStringLiteral("empty"));
        MusicVolume volume{root, QStringLiteral("vfat"), kLabel};
        QVERIFY(isEmptyFat32(volume));
        QDir().mkpath(root + QStringLiteral("/System Volume Information"));
        QDir().mkpath(root + QStringLiteral("/.fseventsd"));
        QDir().mkpath(root + QStringLiteral("/.Trash-1000/files"));
        QVERIFY(isEmptyFat32(volume));
        QVERIFY(!isEmptyFat32({root, QStringLiteral("hfsplus"), kLabel}));
        QVERIFY(!isEmptyFat32(
            {m_tmp.filePath(QStringLiteral("nowhere")), QStringLiteral("vfat"), kLabel}));
        QDir().mkpath(root + QLatin1Char('/') + QString(device::kFirmwareFolder));
        QVERIFY(!isEmptyFat32(volume));
        QDir(root + QLatin1Char('/') + QString(device::kFirmwareFolder)).removeRecursively();
        writeFile(root + QStringLiteral("/song.mp3"), "x");
        QVERIFY(!isEmptyFat32(volume));
    }

    // The volume coming back, from recorded events of detection: they come in
    // the thread of the screen while the runner's thread waits.
    void detectionWatch()
    {
        QStringList mounted;
        DetectionVolumeWatch watch([&mounted](const QString &serial) { mounted << serial; });
        const QString root = newFolder(QStringLiteral("back"));
        std::optional<MusicVolume> got;
        std::atomic<bool> done{false};
        auto wait = [&](qint64 ms, const std::function<bool()> &stop = {}) {
            done = false;
            QThread *worker = QThread::create([&, ms, stop] {
                got = watch.waitReady(kSerial, ms, stop);
                done = true;
            });
            worker->start();
            return worker;
        };

        QThread *worker = wait(5000);
        QTimer::singleShot(50, &watch, [&] {
            watch.seen(detected(device::IpodState::Ready, root, QStringLiteral("other")));
            watch.seen(detected(device::IpodState::NoVolume));
        });
        QTimer::singleShot(150, &watch,
                           [&] { watch.seen(detected(device::IpodState::Ready, root)); });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        worker->wait();
        delete worker;
        QVERIFY(got);
        QCOMPARE(got->mountPoint, root);
        QCOMPARE(got->filesystem, QStringLiteral("vfat"));

        // Already ready: no event needed.
        worker = wait(5000);
        QTRY_VERIFY_WITH_TIMEOUT(done, 1000);
        worker->wait();
        delete worker;
        QVERIFY(got);

        // Forgotten before a format: the old volume does not count.
        watch.forget(kSerial);
        worker = wait(300);
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        worker->wait();
        delete worker;
        QVERIFY(!got);

        // A stop ends the wait at once.
        std::atomic<bool> stop{false};
        worker = wait(60000, [&stop] { return stop.load(); });
        QTest::qWait(100);
        stop = true;
        QTRY_VERIFY_WITH_TIMEOUT(done, 2000);
        worker->wait();
        delete worker;
        QVERIFY(!got);

        watch.mount(kSerial);
        QCOMPARE(mounted, QStringList{kSerial});
    }

    // From end to end on an older iPod in FAT32.
    void formatOlderIpod()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        auto r = rig(TableKind::Dos);
        QVERIFY(r);
        const QByteArray sector0 = bytesAt(r->image, 0, 512);
        QJsonObject info;
        const auto asked = answerTyped(*r->runner, kLabel, &info);
        QStringList stages;
        connect(r->runner.get(), &WizardRunner::stepProgress, this,
                [&stages](const QString &name, const QString &stage) {
                    if (name == step::format && !stages.contains(stage))
                        stages << stage;
                });
        QCOMPARE(r->runner->start(start(false)), QString());
        QCOMPARE(finish(*r->runner), QString());
        QCOMPARE(*asked, 1);
        QCOMPARE(info.value(QStringLiteral("ipodName")).toString(), kLabel);
        QCOMPARE(info.value(QStringLiteral("files")).toInt(), 3);
        QCOMPARE(info.value(QStringLiteral("bytes")).toInteger(), 8005);
        QVERIFY2(stages.contains(QStringLiteral("write")), qPrintable(stages.join(u',')));
        QVERIFY(stages.contains(QStringLiteral("volume")));
        QVERIFY(isFormatted(*r));
        QCOMPARE(bytesAt(r->image, 0, 512), sector0);
        QCOMPARE(bytesAt(r->image, r->pod.firmwareStart, r->pod.firmwareBytes), r->firmware);
        QCOMPARE(r->mounts.load(), 1);
        QVERIFY(r->ran.contains(QStringLiteral("firmware")));
        r->client->shutdown();
    }

    // Section 6.2: the name to type is the model's when the volume has no label,
    // and the helper refuses a wrong one before the image is opened.
    void wrongTypedName()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        auto r = rig(TableKind::Dos);
        QVERIFY(r);
        r->format.label.clear();
        const QByteArray before = fileHash(r->image);
        QJsonObject info;
        answerTyped(*r->runner, QStringLiteral("ipod video"), &info);
        QCOMPARE(r->runner->start(start(false)), QString());
        QCOMPARE(finish(*r->runner), QString(helper::err::confirmationMismatch));
        QCOMPARE(info.value(QStringLiteral("ipodName")).toString(), QStringLiteral("iPod Video"));
        QCOMPARE(fileHash(r->image), before);
        QCOMPARE(r->mounts.load(), 0);
        QVERIFY(!r->ran.contains(QStringLiteral("firmware")));
        r->client->shutdown();
    }

    // Section 6.1: no verified full backup, nothing erased, nothing asked.
    void noVerifiedBackup()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        auto r = rig(TableKind::Dos);
        QVERIFY(r);
        r->noBackup = true;
        const QByteArray before = fileHash(r->image);
        const auto asked = answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->start(start(false)), QString());
        QCOMPARE(finish(*r->runner), QString(error::backupNotVerified));
        QCOMPARE(*asked, 0);
        QCOMPARE(fileHash(r->image), before);
        r->client->shutdown();
    }

    // A cancel at the typed name: nothing written, Resume asks again.
    void cancelAtTypedName()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        auto r = rig(TableKind::Dos);
        QVERIFY(r);
        const QByteArray before = fileHash(r->image);
        WizardRunner *runner = r->runner.get();
        connect(runner, &WizardRunner::stepWaiting, this, [runner](const QString &, WaitKind kind) {
            if (kind == WaitKind::TypedName)
                runner->cancel();
        });
        QCOMPARE(runner->start(start(false)), QString());
        QCOMPARE(finish(*runner), QString(error::cancelled));
        QCOMPARE(fileHash(r->image), before);
        r->client->shutdown();
    }

    // Section 6.3 step 4, then the resume of section 3.3: an empty FAT32
    // volume means the format was done.
    void volumeNotBack()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        auto r = rig(TableKind::Dos);
        QVERIFY(r);
        const QString empty = r->volumeAfterMount(1);
        r->volumeAfterMount = [](int) { return QString(); };
        const auto asked = answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->start(start(false)), QString());
        QCOMPARE(finish(*r->runner), QString(error::volumeNotBack));
        QVERIFY(isFormatted(*r));
        const QByteArray formatted = fileHash(r->image);

        r->volumeAfterMount = [empty](int) { return empty; };
        newRunner(*r);
        const auto askedAgain = answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->resume(kSerial, kModel, Family::Partition), QString());
        QCOMPARE(finish(*r->runner), QString());
        QCOMPARE(*asked, 1);
        QCOMPARE(*askedAgain, 0);
        QCOMPARE(fileHash(r->image), formatted);
        r->client->shutdown();
    }

    // A helper cut in the middle of the format: the volume does not come back,
    // so the resume formats again from the start, the typed name included.
    void helperCutThenResume()
    {
#if !defined(Q_OS_LINUX) || !defined(TACETOS_HELPER_KILLTEST_PROGRAM)
        QSKIP("the images are large sparse files, and the cut program is built on Linux");
#else
        auto r =
            rig(TableKind::Dos, QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM), "write-after:2");
        QVERIFY(r);
        answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->start(start(false)), QString());
        QCOMPARE(finish(*r->runner), QString(helper::err::helperCrashed));
        QCOMPARE(r->mounts.load(), 0);
        r->client->shutdown();

        // The next start of the app: the normal helper. The first mount finds
        // no volume, the one after the format finds it empty.
        useHelper(*r, QString(), QByteArray());
        const QString empty = newFolder(QStringLiteral("fresh"));
        r->volumeAfterMount = [empty](int n) { return n == 1 ? QString() : empty; };
        newRunner(*r);
        const auto asked = answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->resume(kSerial, kModel, Family::Partition), QString());
        QCOMPARE(finish(*r->runner), QString());
        QCOMPARE(*asked, 1);
        QCOMPARE(r->mounts.load(), 2);
        QVERIFY(isFormatted(*r));
        QCOMPARE(bytesAt(r->image, r->pod.firmwareStart, r->pod.firmwareBytes), r->firmware);
        r->client->shutdown();
#endif
    }

    // Section 6.4: the Classic's volume emptied but for what the systems make
    // there, after a verified full backup of it. The helper is never asked.
    void classicEmptied()
    {
        auto r = classicRig();
        const auto before = snapshot(r->volume);
        QJsonObject info;
        const auto asked = answerTyped(*r->runner, kLabel, &info);
        QCOMPARE(r->runner->start(classicStart()), QString());
        QCOMPARE(finish(*r->runner), QString());
        QCOMPARE(*asked, 1);
        QCOMPARE(info.value(QStringLiteral("files")).toInt(), 4);
        QCOMPARE(QDir(r->volume).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("System Volume Information")});
        QVERIFY(r->helper.asked.isEmpty());
        QVERIFY(r->ran.contains(QStringLiteral("firmware")));

        // Everything is in the backup, which a format would have asked for.
        QDirIterator it(r->backup.root, {QString(kManifestFile)}, QDir::Files,
                        QDirIterator::Subdirectories);
        QVERIFY(it.hasNext());
        const QString folder = QFileInfo(it.next()).absolutePath();
        QVERIFY(isVerifiedFullBackup(folder, kSerial, Family::Dfu));
        QCOMPARE(snapshot(backupVolumeFolder(folder)), before);
    }

    // The name is checked by the app, since no helper is asked.
    void classicWrongName()
    {
        auto r = classicRig();
        const auto before = snapshot(r->volume);
        answerTyped(*r->runner, QStringLiteral("ipod"));
        QCOMPARE(r->runner->start(classicStart()), QString());
        QCOMPARE(finish(*r->runner), QString(helper::err::confirmationMismatch));
        QCOMPARE(snapshot(r->volume), before);
        QVERIFY(r->helper.asked.isEmpty());
    }

    // Section 3.3: a resume finds an empty FAT32 volume and counts the step
    // as done, without asking the name again.
    void classicResume()
    {
        auto r = classicRig();
        WizardRunner *runner = r->runner.get();
        connect(runner, &WizardRunner::stepWaiting, this, [runner](const QString &, WaitKind kind) {
            if (kind == WaitKind::TypedName)
                runner->cancel();
        });
        QCOMPARE(runner->start(classicStart()), QString());
        QCOMPARE(finish(*runner), QString(error::cancelled));

        for (const QString &name :
             QDir(r->volume).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
            if (name != QLatin1String("System Volume Information"))
                QVERIFY(QFileInfo(r->volume + QLatin1Char('/') + name).isDir()
                            ? QDir(r->volume + QLatin1Char('/') + name).removeRecursively()
                            : QFile::remove(r->volume + QLatin1Char('/') + name));
        }
        classicRunner(*r);
        const auto asked = answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->resume(kSerial, QStringLiteral("classic-6g"), Family::Dfu), QString());
        QCOMPARE(finish(*r->runner), QString());
        QCOMPARE(*asked, 0);
    }

    // From end to end on a Mac iPod.
    void convertMacIpod()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        auto r = rig(TableKind::Apm);
        QVERIFY(r);
        const auto asked = answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->start(start(true)), QString());
        QCOMPARE(finish(*r->runner), QString());
        QCOMPARE(*asked, 1);
        QCOMPARE(bytesAt(r->image, 0x1FE, 2), QByteArray("\x55\xAA", 2));
        QVERIFY(isFormatted(*r));
        QCOMPARE(bytesAt(r->image, r->pod.firmwareStart, r->pod.firmwareBytes), r->firmware);
        QVERIFY(r->ran.contains(QStringLiteral("firmware")));
        r->client->shutdown();
    }

    // Section 3.3: a conversion cut before its last write runs again from the
    // start; one cut after it is done.
    void convertResume_data()
    {
        QTest::addColumn<bool>("lastWriteMade");
        QTest::newRow("cut before the table") << false;
        QTest::newRow("cut after the table") << true;
    }

    void convertResume()
    {
#if !defined(Q_OS_LINUX) || !defined(TACETOS_HELPER_KILLTEST_PROGRAM)
        QSKIP("the images are large sparse files, and the cut program is built on Linux");
#else
        QFETCH(bool, lastWriteMade);
        auto r =
            rig(TableKind::Apm, QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM), "write-after:1");
        QVERIFY(r);
        answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->start(start(true)), QString());
        QCOMPARE(finish(*r->runner), QString(helper::err::helperCrashed));
        r->client->shutdown();
        useHelper(*r, QString(), QByteArray());

        if (lastWriteMade) {
            // As if the cut had come after the new table: the conversion is
            // finished outside the flow.
            QJsonObject args;
            args.insert(QStringLiteral("ipodName"), kLabel);
            args.insert(QStringLiteral("typedName"), kLabel);
            args.insert(QStringLiteral("volumeLabel"), kLabel);
            args.insert(QStringLiteral("dataDir"), newFolder(QStringLiteral("direct")));
            const HelperAnswer done =
                r->commands->callWithToken(QString(helper::cmd::convertToFat32), args);
            QVERIFY2(done.ok(), qPrintable(done.code + done.detail));
        }
        const QByteArray before = fileHash(r->image);
        newRunner(*r);
        const auto asked = answerTyped(*r->runner, kLabel);
        QCOMPARE(r->runner->resume(kSerial, kModel, Family::Partition), QString());
        QCOMPARE(finish(*r->runner), QString());
        QCOMPARE(*asked, lastWriteMade ? 0 : 1);
        if (lastWriteMade)
            QCOMPARE(fileHash(r->image), before);
        QCOMPARE(bytesAt(r->image, 0x1FE, 2), QByteArray("\x55\xAA", 2));
        QVERIFY(isFormatted(*r));
        r->client->shutdown();
#endif
    }
};

QTEST_GUILESS_MAIN(TstWizardFormat)
#include "tst_wizard_format.moc"
