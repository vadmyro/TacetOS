// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md section 10: the steps of the uninstall, run by
// the runner as the app does. Older iPods: the client, the helper program and
// images made by the fake iPod tool. Classic: `classic-dfu` run in this process
// on the fake system layer and the fake iPod in DFU mode, with detection as a
// list of recorded events. Never libusb, udisks2 or a real iPod.

#include "../../src/device/constants.h"
#include "../../src/installer/constants.h"
#include "backup.h"
#include "bootloader.h"
#include "bootloaderfile.h"
#include "classiccmds.h"
#include "dfuimage.h"
#include "fakedfudevice.h"
#include "fakepod.h"
#include "fakesystemlayer.h"
#include "helperclient.h"
#include "helperconstants.h"
#include "progress.h"
#include "runner.h"
#include "uninstall.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>

using namespace tacetos::wizard;
using namespace tacetos::installer;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodSpec;
namespace device = tacetos::device;
namespace helper = tacetos::helper;

namespace {

const QString kSerial = QStringLiteral("8K7UNINSTALL");
const QString kPort = QStringLiteral("1-2");
const QString kFolder = QString(device::kFirmwareFolder);

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
    void normal(device::RunningFirmware running, bool disk = true)
    {
        IpodSight s = event(IpodSight::Kind::Seen, device::UsbMode::Normal);
        s.ipod.running = running;
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

QByteArray fileHash(const QString &path)
{
    QFile f(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return f.open(QIODevice::ReadOnly) && hash.addData(&f) ? hash.result() : QByteArray();
}

class NoopStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

// The files an uninstall keeps, and one that it does not.
void fillFirmwareFolder(const QString &volume)
{
    put(volume + "/" + kFolder + "/config.cfg", "volume: 3\n");
    put(volume + "/" + kFolder + "/themes/blue.cfg", "theme\n");
    put(volume + "/" + kFolder + "/firmware.bin", pattern(300, 3));
}

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

// The commands of the helper, noted as they go.
class Recording : public HelperCommands
{
public:
    explicit Recording(HelperCommands &inner) : m_inner(inner) {}
    HelperAnswer call(const QString &cmd, const QJsonObject &args,
                      const HelperWatch &watch = HelperWatch()) override
    {
        asked << cmd;
        return m_inner.call(cmd, args, watch);
    }
    QStringList asked;

private:
    HelperCommands &m_inner;
};

class RemoveFrom : public UninstallSources
{
public:
    HelperCommands &helper() const override { return *commands; }
    QString volumeRoot() const override { return volume; }
    HelperCommands *commands = nullptr;
    QString volume;
};

class BootFrom : public BootloaderSources
{
public:
    HelperCommands &helper() const override { return *commands; }
    VolumeWatch &volumes() const override { return *watch; }
    QString target() const override { return QString(); }
    qint64 returnMs() const override { return 500; }
    HelperCommands *commands = nullptr;
    VolumeWatch *watch = nullptr;
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

class ClassicFrom : public ClassicDfuSources
{
public:
    HelperCommands &helper() const override { return *commands; }
    IpodSightWatch &sight() const override { return *watch; }
    const DiagnoseSources &diagnose() const override { return *checks; }
    QString target() const override { return QStringLiteral("ipod6g"); }
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

} // namespace

class TstWizardUninstall : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QTemporaryDir m_runtime;
    QTemporaryDir m_parts;
    int m_count = 0;

    QString newFolder(const QString &name)
    {
        const QString path = m_tmp.filePath(name + QString::number(++m_count));
        QDir().mkpath(path);
        return path;
    }

    QJsonObject factsOf(const QString &stepName)
    {
        const ProgressRead read =
            ProgressStore(m_tmp.filePath(QStringLiteral("data"))).load(kSerial);
        return read.progress.facts.value(stepName).toObject();
    }

    // One flow of the uninstall, as the runner runs it. `make` gives the steps
    // of the family; the other ones do nothing.
    struct Flow
    {
        Family family = Family::Partition;
        std::function<std::unique_ptr<Step>(const QString &)> make;
        bool offerYes = true;
        int offers = 0;
        QStringList offered;
        std::unique_ptr<WizardRunner> runner;
    };

    QString run(Flow &f, bool resume = false)
    {
        f.runner =
            std::make_unique<WizardRunner>(m_tmp.filePath(QStringLiteral("data")),
                                           [&f](const QString &name) -> std::unique_ptr<Step> {
                                               if (auto s = f.make(name))
                                                   return s;
                                               return std::make_unique<NoopStep>();
                                           });
        WizardRunner *runner = f.runner.get();
        connect(runner, &WizardRunner::stepWaiting, runner,
                [runner, &f](const QString &, WaitKind kind, const QJsonObject &info) {
                    UserAnswer answer;
                    if (kind == WaitKind::Offer) {
                        ++f.offers;
                        f.offered << info.value(QStringLiteral("offer")).toString();
                        answer.yes = f.offerYes;
                    }
                    runner->answer(answer);
                });
        WizardRunner::Start s;
        s.serial = kSerial;
        s.model = f.family == Family::Dfu ? QStringLiteral("ipod-classic") : QStringLiteral("ipod");
        s.shape = {f.family, FlowKind::Uninstall, InstallPath::None, false};
        QSignalSpy finished(runner, &WizardRunner::flowFinished);
        QSignalSpy stopped(runner, &WizardRunner::flowStopped);
        const QString refused =
            resume ? runner->resume(kSerial, s.model, f.family) : runner->start(s);
        if (!refused.isEmpty())
            return QStringLiteral("not started: ") + refused;
        if (!QTest::qWaitFor([&] { return !finished.isEmpty() || !stopped.isEmpty(); }, 60000))
            return QStringLiteral("timeout");
        runner->waitFinished(10000);
        return stopped.isEmpty() ? QString() : stopped.first().first().toString();
    }

    // ---- Older iPods -----------------------------------------------------

    struct Legacy
    {
        QString image;
        int sectorSize = 512;
        std::unique_ptr<helper::HelperClient> client;
        std::unique_ptr<helper::HelperClient> backupClient;
        std::unique_ptr<ClientHelperCommands> commands;
        std::unique_ptr<Recording> recording;
        std::unique_ptr<HelperFirmwarePartition> partition;
        std::unique_ptr<DetectionVolumeWatch> watch;
        BackupFrom backup;
        RemoveFrom remove;
        BootFrom boot;
        Flow flow;

        void shutdown()
        {
            client->shutdown();
            backupClient->shutdown();
        }
    };

    // An iPod on an image with its volume: the music file is `music/a.mp3`.
    std::unique_ptr<Legacy> legacy(const QString &model, bool installed, bool folder,
                                   const QString &kill = QString(), const QString &keepImage = {},
                                   const QString &keepVolume = {})
    {
        auto l = std::make_unique<Legacy>();
        if (keepImage.isEmpty()) {
            FakePodSpec spec;
            spec.modelId = model;
            spec.path = m_tmp.filePath(QStringLiteral("%1-%2.img").arg(model).arg(++m_count));
            spec.bootloaderPresent = installed;
            const auto pod = FakePod::build(spec);
            if (!pod.ok)
                return nullptr;
            l->image = spec.path;
            l->sectorSize = pod.sectorSize;
            l->backup.volume = newFolder(QStringLiteral("volume"));
            put(l->backup.volume + "/music/a.mp3", pattern(5000, 9));
            if (folder)
                fillFirmwareFolder(l->backup.volume);
        } else {
            l->image = keepImage;
            l->backup.volume = keepVolume;
            QFile f(keepImage);
            l->sectorSize = 512;
        }
        l->client = std::make_unique<helper::HelperClient>();
        l->backupClient = std::make_unique<helper::HelperClient>();
        l->client->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        l->backupClient->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
#ifdef TACETOS_HELPER_KILLTEST_PROGRAM
        if (!kill.isEmpty()) {
            l->client->setProgram(QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM));
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("TACETOS_TEST_KILL"), kill);
            l->client->setEnvironment(env);
        }
#else
        Q_UNUSED(kill)
#endif
        const QJsonObject target = helper::HelperClient::imageTarget(l->image, l->sectorSize);
        l->commands = std::make_unique<ClientHelperCommands>(*l->client, target);
        l->recording = std::make_unique<Recording>(*l->commands);
        l->partition = std::make_unique<HelperFirmwarePartition>(*l->backupClient, target);
        l->backup.partition = l->partition.get();
        l->backup.root = newFolder(QStringLiteral("backups"));
        l->watch = std::make_unique<DetectionVolumeWatch>(DetectionVolumeWatch::Mounter());
        l->watch->seen(ready(l->backup.volume));
        l->remove.commands = l->recording.get();
        l->remove.volume = l->backup.volume;
        l->boot.commands = l->recording.get();
        l->boot.watch = l->watch.get();
        Legacy *raw = l.get();
        l->flow.make = [raw](const QString &name) -> std::unique_ptr<Step> {
            if (name == step::backupSettings)
                return std::make_unique<BackupStep>(BackupKind::Settings, raw->backup);
            if (name == step::diskMode)
                return std::make_unique<DiskModeStep>(raw->boot);
            if (name == step::removeBootloader)
                return std::make_unique<RemoveBootloaderStep>(raw->remove);
            if (name == step::removeFirmware)
                return std::make_unique<RemoveFirmwareStep>(raw->remove);
            return nullptr;
        };
        return l;
    }

    QString stateOf(Legacy &l)
    {
        return l.commands->call(QString(helper::cmd::readState), QJsonObject())
            .data.value(QStringLiteral("state"))
            .toString();
    }

    // ---- The iPod Classic ------------------------------------------------

    struct Classic
    {
        std::unique_ptr<FakeSystemLayer> layer;
        std::unique_ptr<FakeDfuDevice> device;
        std::unique_ptr<helper::SystemLayer> system;
        InProcessCommands commands;
        RecordedSight watch;
        Checks checks;
        ClassicFrom sources;
        BackupFrom backup;
        RemoveFrom remove;
        QStringList stages;
        Flow flow;
    };

    std::unique_ptr<Classic> classic(bool folder, bool installRecord)
    {
        auto c = std::make_unique<Classic>();
        c->layer = std::make_unique<FakeSystemLayer>();
        c->device = std::make_unique<FakeDfuDevice>();
        c->layer->device = c->device.get();
        c->layer->bus = {[] {
            FakeBusStep s;
            s.fromMs = 0;
            s.devices = {dfuDevice()};
            return s;
        }()};
        c->system = std::make_unique<helper::SystemLayer>();
        c->system->realWrites = true;
        c->system->dfuPartsDir = m_parts.path();
        FakeSystemLayer *fake = c->layer.get();
        c->system->classicLayer = [fake](helper::SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        c->system->sleep = c->layer->options().sleep;
        c->commands.system = c->system.get();
        c->commands.disk.ok = true;
        c->commands.disk.blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb");
        c->commands.disk.sizeBytes = qint64(120) * 1000 * 1000 * 1000;
        c->commands.disk.usbBus = 1;
        c->commands.disk.usbDevice = 5;
        c->commands.disk.usbProduct = QStringLiteral("1261");
        Classic *raw = c.get();
        c->commands.onStage = [raw](const QString &name) {
            raw->stages << name;
            if (name == QLatin1String("wait-device")) {
                raw->watch.gone();
                raw->watch.dfu();
            }
        };
        c->commands.onResult = [raw](const HelperAnswer &answer) {
            if (answer.ok()) {
                raw->watch.gone();
                raw->watch.normal(device::RunningFirmware::Apple);
            }
        };
        c->checks.facts.helperRead = true;
        c->checks.facts.modelSupported = true;
        c->checks.facts.methodAgrees = true;
        c->checks.facts.serial = kSerial;
        c->checks.facts.capacityGb = 120;
        c->checks.facts.volumeReady = true;
        c->checks.facts.filesystem = Filesystem::Fat32;
        c->checks.facts.appleVersion = QStringLiteral("2.0.1");
        const QString volume = newFolder(QStringLiteral("cvolume"));
        put(volume + "/music/a.mp3", pattern(5000, 9));
        if (folder)
            fillFirmwareFolder(volume);
        c->sources.commands = &c->commands;
        c->sources.watch = &c->watch;
        c->sources.checks = &c->checks;
        c->sources.volume = volume;
        c->sources.data = newFolder(QStringLiteral("cdata"));
        if (installRecord) {
            const QString path =
                newFolder(QStringLiteral("old")) + "/" + QString(kInstallRecordFile);
            put(path, QJsonDocument(QJsonObject{{"format", 1},
                                                {"operation", "install"},
                                                {"serial", kSerial},
                                                {"date", "2026-01-01T00:00:00Z"}})
                          .toJson());
            c->sources.records = {path};
        }
        c->backup.volume = volume;
        c->backup.root = newFolder(QStringLiteral("cbackups"));
        c->remove.commands = &c->commands;
        c->remove.volume = volume;
        c->flow.family = Family::Dfu;
        c->flow.make = [raw](const QString &name) -> std::unique_ptr<Step> {
            if (name == step::backupSettings)
                return std::make_unique<BackupStep>(BackupKind::Settings, raw->backup);
            if (name == step::classicRemove)
                return std::make_unique<ClassicRemoveStep>(raw->sources);
            if (name == step::removeFirmware)
                return std::make_unique<RemoveFirmwareStep>(raw->remove);
            return nullptr;
        };
        return c;
    }

    // The folder of the settings backup of the flow, from the manifest it wrote.
    QString settingsBackupOf(const QString &root)
    {
        const QStringList dirs =
            QDir(root).entryList({QStringLiteral("*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        if (dirs.isEmpty())
            return QString();
        const QDir serialDir(QDir(root).filePath(dirs.first()));
        const QStringList dated = serialDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        return dated.isEmpty() ? QString() : serialDir.filePath(dated.first());
    }

    bool keptInBackup(const QString &root)
    {
        const QString folder = settingsBackupOf(root);
        return !folder.isEmpty() &&
               QFile::exists(backupVolumeFolder(folder) + "/" + kFolder + "/config.cfg") &&
               QFile::exists(backupVolumeFolder(folder) + "/" + kFolder + "/themes/blue.cfg");
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QVERIFY(m_runtime.isValid());
        QVERIFY(m_parts.isValid());
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
        QVERIFY(writeParts(m_parts.path()));
    }

    // Section 10, older iPod: the settings are copied, the bootloader removed
    // (state read again: `clean`), then the firmware folder. The music is the
    // same before and after.
    void legacyUninstalled_data()
    {
        QTest::addColumn<QString>("model");
        QTest::newRow("video") << "ipod-video";
        QTest::newRow("nano-2g") << "nano-2g";
    }

    void legacyUninstalled()
    {
        QFETCH(QString, model);
        auto l = legacy(model, true, true);
        QVERIFY(l);
        QCOMPARE(stateOf(*l), QStringLiteral("installed"));
        const QByteArray music = fileHash(l->backup.volume + "/music/a.mp3");
        QCOMPARE(run(l->flow), QString());
        QCOMPARE(stateOf(*l), QStringLiteral("clean"));
        QVERIFY(!QDir(l->backup.volume + "/" + kFolder).exists());
        QCOMPARE(fileHash(l->backup.volume + "/music/a.mp3"), music);
        QVERIFY(keptInBackup(l->backup.root));
        // The bootloader went before the folder.
        QVERIFY(l->recording->asked.contains(QString(helper::cmd::uninstallBootloader)));
        QCOMPARE(l->flow.offers, 0);
        l->shutdown();
    }

    // Section 10, "Nothing to do": `clean` and no firmware folder. Nothing is
    // written, and the screen can ask it before the flow starts.
    void legacyNothingToDo()
    {
        auto l = legacy(QStringLiteral("ipod-video"), false, false);
        QVERIFY(l);
        QVERIFY(::tacetos::wizard::legacyNothingToDo(stateOf(*l), l->backup.volume));
        const QByteArray before = fileHash(l->image);
        QCOMPARE(run(l->flow), QString(error::nothingToDo));
        QCOMPARE(fileHash(l->image), before);
        QVERIFY(!l->recording->asked.contains(QString(helper::cmd::uninstallBootloader)));
        l->shutdown();
    }

    // A `clean` iPod that still has its firmware folder: the folder is removed.
    void legacyFolderOnly()
    {
        auto l = legacy(QStringLiteral("ipod-video"), false, true);
        QVERIFY(l);
        QVERIFY(!::tacetos::wizard::legacyNothingToDo(stateOf(*l), l->backup.volume));
        const QByteArray before = fileHash(l->image);
        QCOMPARE(run(l->flow), QString());
        QVERIFY(!QDir(l->backup.volume + "/" + kFolder).exists());
        QVERIFY(!l->recording->asked.contains(QString(helper::cmd::uninstallBootloader)));
        QCOMPARE(fileHash(l->image), before);
        QVERIFY(keptInBackup(l->backup.root));
        l->shutdown();
    }

    // The folder is never removed without a verified settings backup.
    void firmwareNeedsBackup()
    {
        auto l = legacy(QStringLiteral("ipod-video"), false, true);
        QVERIFY(l);
        l->flow.make = [&l](const QString &name) -> std::unique_ptr<Step> {
            if (name == step::removeFirmware)
                return std::make_unique<RemoveFirmwareStep>(l->remove);
            return nullptr;
        };
        QCOMPARE(run(l->flow), QString(error::backupNotVerified));
        QVERIFY(QFile::exists(l->backup.volume + "/" + kFolder + "/config.cfg"));
        l->shutdown();
    }

    // A cut in the middle of the removal (`tacet-helper-killtest`), then the
    // flow resumed with the normal helper: the iPod ends `clean` with no folder,
    // and the music is the same.
    void legacyCutThenResume()
    {
#if !defined(Q_OS_LINUX) || !defined(TACETOS_HELPER_KILLTEST_PROGRAM)
        QSKIP("the cut program is built on Linux");
#else
        int cuts = 0;
        for (const char *point : {"write-before", "write-torn", "write-after", "flush-after"}) {
            for (int n = 1;; ++n) {
                const QString kill = QStringLiteral("%1:%2").arg(QLatin1String(point)).arg(n);
                auto cut = legacy(QStringLiteral("ipod-video"), true, true, kill);
                QVERIFY2(cut, qPrintable(kill));
                const QByteArray music = fileHash(cut->backup.volume + "/music/a.mp3");
                const QString first = run(cut->flow);
                cut->shutdown();
                if (first.isEmpty()) {
                    QVERIFY2(n > 1, qPrintable(kill + ": no write at all"));
                    break;
                }
                QVERIFY2(first == QString(helper::err::helperCrashed), qPrintable(kill + first));
                ++cuts;
                // The next start of the app: the normal helper, same image.
                auto again =
                    legacy(QString(), false, false, QString(), cut->image, cut->backup.volume);
                QVERIFY2(again, qPrintable(kill));
                again->backup.root = cut->backup.root;
                const QString state = stateOf(*again);
                const QString second = run(again->flow, true);
                if (state == QLatin1String("inconsistent")) {
                    QCOMPARE(second, QString(helper::err::checkFailed));
                } else {
                    QVERIFY2(second.isEmpty(), qPrintable(kill + ": " + state + ": " + second));
                    QCOMPARE(stateOf(*again), QStringLiteral("clean"));
                    QVERIFY(!QDir(again->backup.volume + "/" + kFolder).exists());
                }
                QCOMPARE(fileHash(again->backup.volume + "/music/a.mp3"), music);
                again->shutdown();
                if (QTest::currentTestFailed())
                    return;
            }
        }
        QVERIFY2(cuts >= 4, "too few write points");
#endif
    }

    // Section 10, Classic: `classic-dfu` with `uninstall`, the iPod back with
    // Apple's firmware, the folder removed, the settings kept, and a record of
    // the uninstall that the next `stateBefore` reads.
    void classicUninstalled()
    {
        auto c = classic(true, true);
        QVERIFY(c);
        const QByteArray music = fileHash(c->sources.volume + "/music/a.mp3");
        QCOMPARE(run(c->flow), QString());
        QVERIFY(!c->device->received().isEmpty());
        QVERIFY(c->stages.contains(QStringLiteral("send")));
        QVERIFY(!QDir(c->sources.volume + "/" + kFolder).exists());
        QVERIFY(keptInBackup(c->backup.root));
        QCOMPARE(fileHash(c->sources.volume + "/music/a.mp3"), music);
        QCOMPARE(c->flow.offers, 0);
        const QString record =
            QDir(settingsBackupOf(c->backup.root)).filePath(QString(kInstallRecordFile));
        QVERIFY(QFile::exists(record));
        QCOMPARE(classicStateBefore(c->sources.records + QStringList{record}, kSerial),
                 QStringLiteral("absent"));
    }

    // Section 10, "Nothing to do" on a Classic: no folder and no record. The
    // question is asked; no stops the flow before anything is ejected, yes runs it.
    void classicNothingToDo_data()
    {
        QTest::addColumn<bool>("yes");
        QTest::newRow("no") << false;
        QTest::newRow("yes") << true;
    }

    void classicNothingToDo()
    {
        QFETCH(bool, yes);
        auto c = classic(false, false);
        QVERIFY(c);
        c->flow.offerYes = yes;
        const QString code = run(c->flow);
        QCOMPARE(c->flow.offered, (QStringList{QStringLiteral("remove-anyway")}));
        if (yes) {
            QCOMPARE(code, QString());
            QVERIFY(!c->device->received().isEmpty());
        } else {
            QCOMPARE(code, QString(error::cancelled));
            QVERIFY(c->commands.asked.isEmpty());
            QCOMPARE(c->layer->unmountCalls, 0);
        }
    }

    // Section 10 step 3: the iPod comes back with TacetOS running. The folder
    // stays, `still-tacetos` is the code, and a second try works.
    void classicStillTacetos()
    {
        auto c = classic(true, true);
        QVERIFY(c);
        c->commands.onResult = [&c](const HelperAnswer &answer) {
            if (answer.ok()) {
                c->watch.gone();
                c->watch.normal(device::RunningFirmware::Other);
            }
        };
        QCOMPARE(run(c->flow), QString(error::stillTacetos));
        QVERIFY(QFile::exists(c->sources.volume + "/" + kFolder + "/config.cfg"));
        QCOMPARE(factsOf(QString(step::classicRemove)).value(QStringLiteral("notRemoved")).toInt(),
                 1);

        // Resume: this time Apple's firmware comes back.
        c->commands.onResult = [&c](const HelperAnswer &answer) {
            if (answer.ok()) {
                c->watch.gone();
                c->watch.normal(device::RunningFirmware::Apple);
            }
        };
        // A new fake iPod in DFU mode for the second transfer.
        c->layer = std::make_unique<FakeSystemLayer>();
        c->device = std::make_unique<FakeDfuDevice>();
        c->layer->device = c->device.get();
        c->layer->bus = {[] {
            FakeBusStep s;
            s.fromMs = 0;
            s.devices = {dfuDevice()};
            return s;
        }()};
        FakeSystemLayer *fake = c->layer.get();
        c->system->classicLayer = [fake](helper::SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        c->system->sleep = c->layer->options().sleep;
        c->watch.clear();
        QCOMPARE(run(c->flow, true), QString());
        QVERIFY(!QDir(c->sources.volume + "/" + kFolder).exists());
    }
};

QTEST_GUILESS_MAIN(TstWizardUninstall)
#include "tst_wizard_uninstall.moc"
