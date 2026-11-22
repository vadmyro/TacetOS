// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md section 9: the step `classic-bootloader`, run by
// the runner as the app does. `classic-dfu` runs in this process on the fake
// system layer and the fake iPod in DFU mode of the installer tests, with
// synthetic parts of the image. Detection is a list of recorded events that the
// test gives at the stage where they would come. Never libusb, udisks2 or a real
// iPod.

#include "../../src/device/constants.h"
#include "../../src/installer/constants.h"
#include "classiccmds.h"
#include "classicdfu.h"
#include "dfuimage.h"
#include "fakedfudevice.h"
#include "fakesystemlayer.h"
#include "helperconstants.h"
#include "progress.h"
#include "runner.h"
#include "testpackage.h"
#include "wizarderrors.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>

using namespace tacetos::wizard;
using namespace tacetos::installer;
using tacetos::wizard::testing::makeTestPackage;
using tacetos::wizard::testing::TestPackage;
namespace device = tacetos::device;
namespace helper = tacetos::helper;

namespace {

const QString kSerial = QStringLiteral("8K7CLASSIC");
const QString kPort = QStringLiteral("1-2");
const QString kTarget = QStringLiteral("ipod6g");

QByteArray pattern(int size, int seed)
{
    QByteArray b(size, '\0');
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 7 + seed) & 0xFF);
    return b;
}

bool put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// The info file of the firmware that the step `firmware` left on the volume.
QString infoFile(const QString &volume)
{
    return volume + QLatin1Char('/') + QString(device::kFirmwareFolder) + QLatin1Char('/') +
           QString(device::kFirmwareInfoFile);
}

// The four generated parts of the image, made of patterns.
bool writeParts(const QString &dir)
{
    QByteArray installer = pattern(5400, 0x11);
    installer.replace(5400 - 0x50, 0x50, QByteArray(0x50, '\0'));
    const QDir d(dir);
    return put(d.filePath(QString(kDfuPartSignature)), pattern(16, 0x31)) &&
           put(d.filePath(QString(kDfuPartCertificate)), pattern(698, 0x55)) &&
           put(d.filePath(QString(kDfuPartInstaller)), installer) &&
           put(d.filePath(QString(kDfuPartUninstaller)), pattern(6000, 0x77));
}

// A bootloader file: the checksum (big-endian), the model tag, the body.
QByteArray bootFile(const QByteArray &body)
{
    quint32 sum = 71;
    for (char c : body)
        sum += quint8(c);
    QByteArray f;
    for (int shift = 24; shift >= 0; shift -= 8)
        f.append(char((sum >> shift) & 0xFF));
    f.append("ip6g", 4);
    return f + body;
}

UsbDeviceInfo dfuDevice()
{
    UsbDeviceInfo d;
    d.id = {0x05AC, 0x1223};
    d.bus = 1;
    d.address = 7;
    return d;
}

class Borrowed : public ClassicSystemLayer
{
public:
    explicit Borrowed(FakeSystemLayer &fake) : m_fake(fake) {}
    HostSystem hostSystem() const override { return m_fake.hostSystem(); }
    QList<UsbDeviceInfo> listUsbDevices() override { return m_fake.listUsbDevices(); }
    std::unique_ptr<UsbDfuLink> makeLink(const UsbDeviceInfo &d) override
    {
        return m_fake.makeLink(d);
    }
    bool unmountIpod(QString *error) override { return m_fake.unmountIpod(error); }
    bool ipodListed() override { return m_fake.ipodListed(); }
    QList<SystemProcess> findAppleProcesses() override { return m_fake.findAppleProcesses(); }
    bool pauseProcess(const SystemProcess &p, QString *e) override
    {
        return m_fake.pauseProcess(p, e);
    }
    bool resumeProcess(const SystemProcess &p, QString *e) override
    {
        return m_fake.resumeProcess(p, e);
    }

private:
    FakeSystemLayer &m_fake;
};

// Detection as a list of events that the test gives.
class RecordedSight : public IpodSightWatch
{
public:
    void clear() override { queue.clear(); }
    std::optional<IpodSight> next(qint64, const std::function<bool()> &) override
    {
        if (queue.isEmpty())
            return std::nullopt;
        return queue.takeFirst();
    }

    void gone() { queue.append(event(IpodSight::Kind::Gone, device::UsbMode::None)); }
    void dfu() { queue.append(event(IpodSight::Kind::Seen, device::UsbMode::Dfu)); }
    void normal(device::RunningFirmware running, bool disk = true,
                const QString &serial = QString())
    {
        IpodSight s = event(IpodSight::Kind::Seen, device::UsbMode::Normal);
        s.ipod.running = running;
        s.ipod.serial = serial;
        if (disk)
            s.ipod.disk = device::DiskInfo{QStringLiteral("/dev/sdb"), 1000, 4096};
        queue.append(s);
    }

    QList<IpodSight> queue;

private:
    static IpodSight event(IpodSight::Kind kind, device::UsbMode mode)
    {
        IpodSight s;
        s.kind = kind;
        s.portPath = kPort;
        s.ipod.portPath = kPort;
        s.ipod.mode = mode;
        return s;
    }
};

// `classic-dfu` through the code of the helper, the rest of the commands never
// asked.
class InProcessCommands : public HelperCommands
{
public:
    HelperAnswer call(const QString &cmd, const QJsonObject &args,
                      const HelperWatch &watch = HelperWatch()) override
    {
        asked << cmd;
        if (diskGone > 0) {
            // As the helper does when udisks2 does not know the disk: before anything.
            --diskGone;
            HelperAnswer gone;
            gone.code = QString(helper::err::diskGone);
            if (onResult)
                onResult(gone);
            return gone;
        }
        Hooks hooks(*this, watch);
        const helper::CommandOutcome o = helper::runClassicDfuOnDisk(args, *system, disk, hooks);
        HelperAnswer answer;
        answer.code = o.ok ? QString() : o.error;
        answer.detail = o.detail;
        answer.data = o.data;
        if (onResult)
            onResult(answer);
        return answer;
    }

    helper::SystemLayer *system = nullptr;
    helper::RealDisk disk;
    // Called when a stage starts, and when the result is there.
    std::function<void(const QString &)> onStage;
    std::function<void(const HelperAnswer &)> onResult;
    QStringList asked;
    // The next calls answer `disk-gone`, as the helper does before it looks at anything.
    int diskGone = 0;

private:
    class Hooks : public helper::CommandHooks
    {
    public:
        Hooks(InProcessCommands &owner, const HelperWatch &watch) : m_owner(owner), m_watch(watch)
        {
        }
        void log(const QString &, const QString &) override {}
        void stage(const QString &name) override
        {
            m_stage = name;
            emitEvent(0);
            if (m_owner.onStage)
                m_owner.onStage(name);
        }
        void progress(qint64 done, qint64) override { emitEvent(done); }
        bool cancelRequested() override { return m_watch.stop && m_watch.stop(); }
        bool beforeFirstWrite() override { return true; }

    private:
        void emitEvent(qint64 done)
        {
            QJsonObject event;
            event.insert(QStringLiteral("stage"), m_stage);
            event.insert(QStringLiteral("done"), double(done));
            event.insert(QStringLiteral("total"), 0.0);
            if (m_watch.progress)
                m_watch.progress(event);
        }
        InProcessCommands &m_owner;
        const HelperWatch &m_watch;
        QString m_stage;
    };
};

class Checks : public DiagnoseSources
{
public:
    Os os() const override { return Os::Linux; }
    QString osRelease() const override { return QString(); }
    UdisksInfo udisks() const override { return {true, true, QStringLiteral("2.10.1")}; }
    HelperInfo helper() const override { return {true, true, true, true, true}; }
    bool udevRulePresent() const override { return true; }
    FolderInfo backupFolder() const override { return {}; }
    qint64 backupNeedBytes(InstallPath) const override { return 0; }
    IpodFacts ipod() const override { return facts; }
    PackageCheck package(const QString &) const override { return {}; }
    qint64 firmwareNeedBytes(const PackageCheck &) const override { return 0; }

    IpodFacts facts;
};

class Sources : public ClassicDfuSources
{
public:
    HelperCommands &helper() const override { return *commands; }
    IpodSightWatch &sight() const override { return *watch; }
    const DiagnoseSources &diagnose() const override { return *checks; }
    QString target() const override { return kTarget; }
    QString volumeRoot() const override { return volume; }
    QString dataDir() const override { return data; }
    QString portPath() const override { return kPort; }
    QStringList installRecords() const override { return records; }
    qint64 returnMs() const override { return 200; }

    HelperCommands *commands = nullptr;
    RecordedSight *watch = nullptr;
    Checks *checks = nullptr;
    QString volume;
    QString data;
    QStringList records;
};

class SetBackupFolder : public Step
{
public:
    explicit SetBackupFolder(const QString &folder) : m_folder(folder) {}
    StepOutcome run(StepContext &context) override
    {
        context.setBackupFolder(m_folder);
        return StepOutcome::done();
    }

private:
    QString m_folder;
};

class NoopStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

// In place of `verify`: what `classic-bootloader` told the steps after it.
class ReadReturned : public Step
{
public:
    explicit ReadReturned(QString *seen) : m_seen(seen) {}
    StepOutcome run(StepContext &context) override
    {
        *m_seen =
            context.fact(QString(step::classicBootloader), QStringLiteral("returned")).toString();
        return StepOutcome::done();
    }

private:
    QString *m_seen;
};

} // namespace

class TstWizardClassicDfu : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QTemporaryDir m_parts;
    int m_count = 0;
    // What a run is made of.
    std::unique_ptr<FakeSystemLayer> m_layer;
    std::unique_ptr<FakeDfuDevice> m_device;
    std::unique_ptr<helper::SystemLayer> m_system;
    InProcessCommands m_commands;
    RecordedSight m_watch;
    Checks m_checks;
    Sources m_sources;
    TestPackage m_package;
    QString m_backup;
    QStringList m_stages;
    // The stages the step gave the runner.
    QStringList m_progress;
    QList<QString> m_reasons;
    // The fact `returned` as the step after `classic-bootloader` reads it.
    QString m_returned;
    // The USB detail of each "ready" wait.
    QList<QString> m_usb;
    int m_waits = 0;
    // What the user does at each "ready" wait: true presses Start.
    QList<bool> m_start;
    std::unique_ptr<WizardRunner> m_runner;

    void setUpRun(bool realWrites = true)
    {
        m_layer = std::make_unique<FakeSystemLayer>();
        m_device = std::make_unique<FakeDfuDevice>();
        m_layer->device = m_device.get();
        m_layer->bus = {[] {
            FakeBusStep s;
            s.fromMs = 0;
            s.devices = {dfuDevice()};
            return s;
        }()};
        m_system = std::make_unique<helper::SystemLayer>();
        m_system->realWrites = realWrites;
        m_system->dfuPartsDir = m_parts.path();
        FakeSystemLayer *fake = m_layer.get();
        m_system->classicLayer = [fake](helper::SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        m_system->sleep = m_layer->options().sleep;

        m_commands = InProcessCommands();
        m_commands.system = m_system.get();
        m_commands.disk.ok = true;
        m_commands.disk.blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb");
        m_commands.disk.sizeBytes = qint64(120) * 1000 * 1000 * 1000;
        m_commands.disk.usbBus = 1;
        m_commands.disk.usbDevice = 5;
        m_commands.disk.usbProduct = QStringLiteral("1261");
        m_commands.onStage = [this](const QString &name) { m_stages << name; };
        m_watch = RecordedSight();

        m_checks.facts = IpodFacts();
        m_checks.facts.helperRead = true;
        m_checks.facts.modelSupported = true;
        m_checks.facts.methodAgrees = true;
        m_checks.facts.serial = kSerial;
        m_checks.facts.capacityGb = 120;
        m_checks.facts.volumeReady = true;
        m_checks.facts.filesystem = Filesystem::Fat32;
        m_checks.facts.appleVersion = QStringLiteral("2.0.1");

        const QString volume = newFolder(QStringLiteral("volume"));
        QVERIFY(put(infoFile(volume), ("Target: " + kTarget + "\n").toUtf8()));
        m_sources = Sources();
        m_sources.commands = &m_commands;
        m_sources.watch = &m_watch;
        m_sources.checks = &m_checks;
        m_sources.volume = volume;
        m_sources.data = newFolder(QStringLiteral("data"));
        m_package = makeTestPackage(newFolder(QStringLiteral("package")), kTarget,
                                    bootFile(pattern(4096, 0x42)), {{QStringLiteral("a"), "a"}});
        m_backup = newFolder(QStringLiteral("backup"));
        m_stages.clear();
        m_progress.clear();
        m_reasons.clear();
        m_returned.clear();
        m_usb.clear();
        m_waits = 0;
        m_start.clear();
        m_runner.reset();
    }

    QString newFolder(const QString &name)
    {
        const QString path = m_tmp.filePath(name + QString::number(++m_count));
        QDir().mkpath(path);
        return path;
    }

    // The whole flow of a Classic, the other steps doing nothing. Returns the
    // code the flow stopped with, empty when it finished.
    QString runFlow()
    {
        m_runner = std::make_unique<WizardRunner>(
            m_tmp.filePath(QStringLiteral("progress")),
            [this](const QString &name) -> std::unique_ptr<Step> {
                if (name == step::backupKeep)
                    return std::make_unique<SetBackupFolder>(m_backup);
                if (name == step::classicBootloader)
                    return std::make_unique<ClassicDfuStep>(m_sources);
                if (name == step::verify)
                    return std::make_unique<ReadReturned>(&m_returned);
                return std::make_unique<NoopStep>();
            });
        WizardRunner *runner = m_runner.get();
        connect(runner, &WizardRunner::stepProgress, runner,
                [this](const QString &, const QString &stage) { m_progress << stage; });
        connect(runner, &WizardRunner::stepWaiting, runner,
                [this, runner](const QString &, WaitKind kind, const QJsonObject &info) {
                    if (kind != WaitKind::ReadyForDfu)
                        return;
                    m_reasons << info.value(QStringLiteral("reason")).toString();
                    m_usb << info.value(QStringLiteral("usb")).toString();
                    UserAnswer answer;
                    answer.cancelled = m_waits >= m_start.size() || !m_start.at(m_waits);
                    ++m_waits;
                    runner->answer(answer);
                });
        WizardRunner::Start s;
        s.serial = kSerial;
        s.model = QStringLiteral("ipod-classic");
        s.shape = {Family::Dfu, FlowKind::Install, InstallPath::Keep, false};
        s.packageManifest = m_package.manifest;
        s.packageSha256 = m_package.sha256;
        QSignalSpy finished(runner, &WizardRunner::flowFinished);
        QSignalSpy stopped(runner, &WizardRunner::flowStopped);
        const QString refused = runner->start(s);
        if (!refused.isEmpty())
            return QStringLiteral("not started: ") + refused;
        if (!QTest::qWaitFor([&] { return !finished.isEmpty() || !stopped.isEmpty(); }, 60000))
            return QStringLiteral("timeout");
        runner->waitFinished(10000);
        return stopped.isEmpty() ? QString() : stopped.first().first().toString();
    }

    QString recordPath() const { return QDir(m_backup).filePath(QString(kInstallRecordFile)); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QVERIFY(m_parts.isValid());
        QVERIFY(writeParts(m_parts.path()));
    }

    // Section 9.2: the iPod is in DFU mode, gets the image, and comes back with
    // something other than Apple's firmware. The record is written.
    void success()
    {
        setUpRun();
        m_start = {true};
        m_commands.onStage = [this](const QString &name) {
            m_stages << name;
            if (name == QLatin1String("wait-device")) {
                m_watch.gone();
                m_watch.dfu();
            }
        };
        // Under TacetOS the Classic gives another serial (D45).
        m_commands.onResult = [this](const HelperAnswer &) {
            m_watch.gone();
            m_watch.normal(device::RunningFirmware::Other, true, QStringLiteral("T1"));
        };
        QCOMPARE(runFlow(), QString());
        QVERIFY(!m_device->received().isEmpty());
        QVERIFY(m_stages.contains(QStringLiteral("send")));
        QCOMPARE(m_reasons, (QStringList{QString()}));
        // Back under TacetOS: `verify` may then see Apple's firmware (Menu held).
        QCOMPARE(m_returned, QStringLiteral("tacetos"));
        QFile file(recordPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject record = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(record.value("operation").toString(), QString("install"));
        QCOMPARE(record.value("serial").toString(), kSerial);
        QCOMPARE(record.value("format").toInt(), kInstallRecordFormat);
        QCOMPARE(record.value("bootloaderSha256").toString().size(), 64);
        QVERIFY(QDateTime::fromString(record.value("date").toString(), Qt::ISODate).isValid());
        // The record is what the next `stateBefore` reads.
        QCOMPARE(classicStateBefore({recordPath()}, kSerial), QString("present"));
        QCOMPARE(classicStateBefore({recordPath()}, QString("other")), QString("unknown"));
        // The second serial, and the way back to the first one.
        QCOMPARE(record.value("serialUnderTacetos").toString(), QStringLiteral("T1"));
        QCOMPARE(appleSerialOf({recordPath()}, QStringLiteral("T1")), kSerial);
        QVERIFY(appleSerialOf({recordPath()}, kSerial).isEmpty());
        QVERIFY(appleSerialOf({recordPath()}, QString()).isEmpty());
    }

    // Section 9.4: the iPod comes back as a disk instead of DFU mode. The
    // helper is cancelled, nothing is sent, and the step waits for the user
    // again; the second try works.
    void diskModeInstead()
    {
        setUpRun();
        m_start = {true, true};
        int attempt = 0;
        m_commands.onStage = [this, &attempt](const QString &name) {
            m_stages << name;
            if (name != QLatin1String("wait-device"))
                return;
            if (++attempt == 1) {
                m_watch.gone();
                m_watch.normal(device::RunningFirmware::Apple);
            } else {
                m_watch.gone();
                m_watch.dfu();
            }
        };
        m_commands.onResult = [this](const HelperAnswer &answer) {
            if (answer.ok()) {
                m_watch.gone();
                m_watch.normal(device::RunningFirmware::Other);
            }
        };
        QCOMPARE(runFlow(), QString());
        QCOMPARE(attempt, 2);
        QCOMPARE(m_reasons, (QStringList{QString(), QString(error::diskModeInstead)}));
        QVERIFY(QFile::exists(recordPath()));
        QVERIFY(!m_device->received().isEmpty());
    }

    // Section 9.4: Start while the iPod restarts, so udisks2 does not know its
    // disk yet. The step does not fail: it waits for detection to show the disk,
    // then sends again by itself, without a second Start.
    void startWithoutTheDiskWaitsForIt()
    {
        setUpRun();
        m_start = {true};
        m_commands.diskGone = 2;
        m_commands.onStage = [this](const QString &name) {
            m_stages << name;
            if (name == QLatin1String("wait-device")) {
                m_watch.gone();
                m_watch.dfu();
            }
        };
        m_commands.onResult = [this](const HelperAnswer &answer) {
            // The disk shows up again after each refusal, then the iPod restarts.
            m_watch.gone();
            m_watch.normal(answer.ok() ? device::RunningFirmware::Other
                                       : device::RunningFirmware::Apple);
        };
        QCOMPARE(runFlow(), QString());
        QCOMPARE(m_commands.asked.size(), 3);
        QCOMPARE(m_reasons, (QStringList{QString()}));
        QCOMPARE(m_progress.count(QStringLiteral("wait-disk")), 2);
        QVERIFY(!m_device->received().isEmpty());
        QVERIFY(QFile::exists(recordPath()));
    }

    // The disk does not come back in time (the iPod stays in DFU mode): the
    // step asks for Start again, with the reason `no-disk`. Nothing was ejected.
    void startWithoutTheDiskAsksAgainAfterAWhile()
    {
        setUpRun();
        m_start = {true};
        m_commands.diskGone = 1000000;
        QCOMPARE(runFlow(), QString(error::cancelled));
        QCOMPARE(m_reasons, (QStringList{QString(), QString(error::noDisk)}));
        QVERIFY(m_commands.asked.size() >= 1);
        QCOMPARE(m_layer->unmountCalls, 0);
        QVERIFY(m_device->received().isEmpty());
    }

    // Section 9.4: no DFU device in the wait of the helper. The step waits for
    // the user again with the hint; here the user stops.
    void noDfuInTime()
    {
        setUpRun();
        m_layer->bus.clear();
        m_start = {true};
        QCOMPARE(runFlow(), QString(error::cancelled));
        QCOMPARE(m_reasons, (QStringList{QString(), QString(error::dfuFailed)}));
        QVERIFY(m_device->received().isEmpty());
        QVERIFY(!QFile::exists(recordPath()));
    }

    // The iPod in DFU mode cannot be opened: the step waits again with the USB
    // detail of the helper for the log; here the user stops.
    void openFailureGivesTheUsbDetail()
    {
        setUpRun();
        FakeDfuScript script;
        script.openFails = true;
        script.openCode = UsbAccessError::Other;
        m_device = std::make_unique<FakeDfuDevice>(script);
        m_layer->device = m_device.get();
        m_start = {true};
        QCOMPARE(runFlow(), QString(error::cancelled));
        QCOMPARE(m_reasons, (QStringList{QString(), QString(error::dfuFailed)}));
        QCOMPARE(m_usb, (QStringList{QString(), QStringLiteral("access denied")}));
        QVERIFY(m_device->received().isEmpty());
    }

    // Nothing is ejected before the user says "Start".
    void cancelBeforeStart()
    {
        setUpRun();
        m_start = {false};
        QCOMPARE(runFlow(), QString(error::cancelled));
        QVERIFY(m_commands.asked.isEmpty());
        QCOMPARE(m_layer->unmountCalls, 0);
    }

    // Section 9.4: the image went, the iPod does not come back.
    void ipodDoesNotComeBack()
    {
        setUpRun();
        m_start = {true};
        m_commands.onResult = [this](const HelperAnswer &) { m_watch.gone(); };
        QCOMPARE(runFlow(), QString(error::ipodNotBack));
        QVERIFY(!QFile::exists(recordPath()));
    }

    // Section 9.4: the iPod comes back with Apple's firmware: the bootloader
    // did not take. The count of the facts goes up.
    void backWithAppleFirmware()
    {
        setUpRun();
        m_start = {true};
        m_commands.onResult = [this](const HelperAnswer &) {
            m_watch.gone();
            m_watch.normal(device::RunningFirmware::Apple);
        };
        QCOMPARE(runFlow(), QString(error::bootloaderNotTaken));
        QVERIFY(!QFile::exists(recordPath()));
    }

    // Section 9.4: DFU mode again after the iPod had left: the buttons were held.
    void backInDfu()
    {
        setUpRun();
        m_start = {true};
        m_commands.onResult = [this](const HelperAnswer &) {
            m_watch.gone();
            m_watch.dfu();
        };
        QCOMPARE(runFlow(), QString(error::backInDfu));
    }

    // Section 9.1: a condition that fails stops the step before the eject.
    void conditionsAreCheckedFirst()
    {
        setUpRun();
        m_checks.facts.appleVersion = QStringLiteral("1.0.0");
        m_start = {true};
        QCOMPARE(runFlow(), QString(error::checkBlocking));
        QVERIFY(m_commands.asked.isEmpty());
        QCOMPARE(m_layer->unmountCalls, 0);
        QVERIFY(m_reasons.isEmpty());
    }

    // Section 9.1: the firmware has to be on the volume already.
    void firmwareMustBeCopied()
    {
        setUpRun();
        QVERIFY(QFile::remove(infoFile(m_sources.volume)));
        m_start = {true};
        QCOMPARE(runFlow(), QString(error::firmwareWriteFailed));
        QVERIFY(m_commands.asked.isEmpty());
    }

    // helper.md section 9.4: a development build stops before the eject.
    void devBuildStopsBeforeTheEject()
    {
        setUpRun(false);
        m_start = {true};
        QCOMPARE(runFlow(), QString(helper::err::devBuild));
        QCOMPARE(m_layer->unmountCalls, 0);
    }

    // Section 9.2 step 2: the last record of the serial decides.
    void stateBeforeFromRecords()
    {
        const auto record = [this](const QString &op, const QString &date, const QString &serial) {
            const QString path = newFolder(QStringLiteral("rec")) + QStringLiteral("/r.json");
            const QByteArray json =
                QJsonDocument(QJsonObject{{"operation", op}, {"date", date}, {"serial", serial}})
                    .toJson();
            put(path, json);
            return path;
        };
        QCOMPARE(classicStateBefore({}, kSerial), QString("unknown"));
        const QString a = record("install", "2026-01-01T10:00:00Z", kSerial);
        const QString b = record("uninstall", "2026-02-01T10:00:00Z", kSerial);
        const QString c = record("install", "2026-03-01T10:00:00Z", "other");
        QCOMPARE(classicStateBefore({a}, kSerial), QString("present"));
        QCOMPARE(classicStateBefore({a, b, c}, kSerial), QString("absent"));
        QCOMPARE(classicStateBefore({b, a}, kSerial), QString("absent"));
    }
};

QTEST_GUILESS_MAIN(TstWizardClassicDfu)
#include "tst_wizard_classicdfu.moc"
