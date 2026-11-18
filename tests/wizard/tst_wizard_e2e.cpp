// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The whole journey of the wizard, from the diagnosis to the uninstall, through
// the real factory of steps (stepfactory.h): older iPods on image files with the
// helper program, the Classic with `classic-dfu` run in this process on the
// fake system layer and the fake iPod in DFU mode, and the disk service that
// answers from memory. Never libusb, udisks2 or a real iPod.

#include "../../src/device/constants.h"
#include "../../src/installer/constants.h"
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
#include "stepfactory.h"
#include "testpackage.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <functional>
#include <memory>

using namespace tacetos::wizard;
using namespace tacetos::installer;
using tacetos::fakepod::BootloaderFile;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodSpec;
using tacetos::wizard::testing::makeTestPackage;
using tacetos::wizard::testing::TestPackage;
namespace device = tacetos::device;
namespace helper = tacetos::helper;

namespace {

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

// The hash of every file under a folder, with its relative path.
QByteArray treeHash(const QString &folder)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QStringList files;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    files.sort();
    for (const QString &path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        hash.addData(QDir(folder).relativeFilePath(path).toUtf8());
        hash.addData(&f);
    }
    return hash.result().toHex();
}

// The four generated parts of the DFU image, made of patterns.
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

// A bootloader file of the Classic: the checksum (big-endian), the model tag,
// the body.
QByteArray classicBootFile(const QByteArray &body)
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

// The files of a package: the info file names the model, as the firmware does.
QMap<QString, QByteArray> infoFiles(const QString &target, QMap<QString, QByteArray> files)
{
    files.insert(QString(device::kFirmwareInfoFile),
                 "Target: " + target.toUtf8() + "\nVersion: 4.0\n");
    return files;
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

// The disk service: disk sdb of an older iPod, slot 2 is FAT32.
class FakeService : public device::UdisksBackend
{
public:
    FakeService()
    {
        const QString drive = QStringLiteral("/org/freedesktop/UDisks2/drives/iPod");
        objects[drive][QStringLiteral("org.freedesktop.UDisks2.Drive")] = {
            {"Vendor", "Apple"}, {"ConnectionBus", "usb"}, {"CanPowerOff", true}};
        addBlock(QStringLiteral("sdb"), props("/dev/sdb", 16, drive, QString()));
        addBlock(QStringLiteral("sdb2"), props("/dev/sdb2", 18, drive, "vfat"), 2, true);
    }

    static QVariantMap props(const QString &node, int minor, const QString &drive,
                             const QString &type)
    {
        QVariantMap p;
        p[QStringLiteral("Device")] = node.toUtf8() + '\0';
        p[QStringLiteral("DeviceNumber")] = qulonglong(device::makeDeviceNumber(8, minor));
        p[QStringLiteral("Drive")] = drive;
        p[QStringLiteral("IdType")] = type;
        p[QStringLiteral("Size")] = qulonglong(1000);
        return p;
    }

    void addBlock(const QString &name, const QVariantMap &p, int number = 0, bool fs = false)
    {
        const QString path = QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + name;
        objects[path][QStringLiteral("org.freedesktop.UDisks2.Block")] = p;
        if (number > 0)
            objects[path][QStringLiteral("org.freedesktop.UDisks2.Partition")] = {
                {"Number", number}};
        if (fs)
            objects[path][QStringLiteral("org.freedesktop.UDisks2.Filesystem")] = {
                {"MountPoints", QVariant::fromValue(QByteArrayList())}};
    }

    std::optional<device::UdisksObjects> managedObjects(device::UdisksReply *) override
    {
        return objects;
    }
    device::UdisksReply mount(const QString &, bool) override
    {
        if (onMount)
            onMount();
        return {{}, {}, mountPoint};
    }
    device::UdisksReply unmount(const QString &) override { return {}; }
    device::UdisksReply eject(const QString &) override { return {}; }
    device::UdisksReply powerOff(const QString &) override { return {}; }

    device::UdisksObjects objects;
    QString mountPoint;
    // The iPod shows its volume again, as detection would say it.
    std::function<void()> onMount;
};

device::LinuxDisk ipodDisk()
{
    device::LinuxDisk disk;
    disk.name = QStringLiteral("sdb");
    disk.node = QStringLiteral("/dev/sdb");
    disk.portPath = kPort;
    disk.partitions = {{QStringLiteral("sdb2"), 2, 8, 18}};
    return disk;
}

// `classic-dfu` through the code of the helper; the two read commands of the
// diagnosis answered from memory, the rest never asked.
class ClassicCommands : public HelperCommands
{
public:
    HelperAnswer call(const QString &cmd, const QJsonObject &args,
                      const HelperWatch &watch = HelperWatch()) override
    {
        asked << cmd;
        if (cmd == QLatin1String(helper::cmd::readState)) {
            HelperAnswer answer;
            answer.data.insert(QStringLiteral("method"), QStringLiteral("dfu"));
            return answer;
        }
        if (cmd == QLatin1String(helper::cmd::readDeviceSheet)) {
            HelperAnswer answer;
            answer.data.insert(QStringLiteral("text"),
                               QStringLiteral("<key>VisibleBuildID</key>\n<string>2.0.1</string>"));
            return answer;
        }
        if (cmd != QLatin1String(helper::cmd::classicDfu))
            return HelperAnswer{QStringLiteral("unavailable"), QStringLiteral("not asked"), {}};
        uninstalling =
            args.value(QStringLiteral("operation")).toString() == QLatin1String("uninstall");
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
    bool uninstalling = false;
    std::function<void(const QString &)> onStage;
    std::function<void(const HelperAnswer &)> onResult;
    QStringList asked;

private:
    class Hooks : public helper::CommandHooks
    {
    public:
        Hooks(ClassicCommands &owner, const HelperWatch &watch) : m_owner(owner), m_watch(watch) {}
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
        ClassicCommands &m_owner;
        const HelperWatch &m_watch;
        QString m_stage;
    };
};

// A step that holds still at its first sign of progress until the runner is
// asked to stop: the cut of the tests falls in the middle of the work.
class HoldingContext : public StepContext
{
public:
    HoldingContext(StepContext &inner, std::atomic<bool> &reached)
        : m_inner(inner), m_reached(reached)
    {
    }
    QString serial() const override { return m_inner.serial(); }
    QString model() const override { return m_inner.model(); }
    FlowShape shape() const override { return m_inner.shape(); }
    bool isResume() const override { return m_inner.isResume(); }
    bool stopRequested() const override { return m_inner.stopRequested(); }
    void progress(const QString &stage, qint64 done, qint64 total) override
    {
        m_inner.progress(stage, done, total);
        m_reached = true;
        while (!m_inner.stopRequested())
            QThread::msleep(5);
    }
    UserAnswer waitForUser(WaitKind kind, const QJsonObject &info) override
    {
        return m_inner.waitForUser(kind, info);
    }
    QJsonValue fact(const QString &step, const QString &key) const override
    {
        return m_inner.fact(step, key);
    }
    void setFact(const QString &key, const QJsonValue &value) override
    {
        m_inner.setFact(key, value);
    }
    QString backupFolder() const override { return m_inner.backupFolder(); }
    void setBackupFolder(const QString &path) override { m_inner.setBackupFolder(path); }
    QString packageManifest() const override { return m_inner.packageManifest(); }
    QString packageSha256() const override { return m_inner.packageSha256(); }

private:
    StepContext &m_inner;
    std::atomic<bool> &m_reached;
};

class HoldingStep : public Step
{
public:
    HoldingStep(std::unique_ptr<Step> inner, std::atomic<bool> &reached)
        : m_inner(std::move(inner)), m_reached(reached)
    {
    }
    StepOutcome run(StepContext &context) override
    {
        HoldingContext held(context, m_reached);
        return m_inner->run(held);
    }

private:
    std::unique_ptr<Step> m_inner;
    std::atomic<bool> &m_reached;
};

} // namespace

class TestWizardE2e : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QTemporaryDir m_runtime;
    QTemporaryDir m_parts;
    int m_count = 0;

    // One iPod of a test with everything its flows need.
    struct Rig
    {
        bool classic = false;
        QString serial;
        QString modelId;
        QString target;
        QString image;
        int sectorSize = 512;
        QString volume;
        QString music;
        QString data;
        QString etc;
        QString udevRule;
        TestPackage package;

        std::unique_ptr<helper::HelperClient> client;
        std::unique_ptr<helper::HelperClient> spare;
        std::unique_ptr<FakeSystemLayer> layer;
        std::unique_ptr<FakeDfuDevice> dfu;
        std::unique_ptr<helper::SystemLayer> system;
        ClassicCommands *classicCommands = nullptr;
        bool realWrites = true;
        std::unique_ptr<LinuxStepFactory> factory;
        std::unique_ptr<WizardRunner> runner;
        QString backupRoot;
        QStringList installRecords;
        QStringList offers;
        QStringList waits;
        // The step the first run holds still in, and whether it got there.
        QString holdStep;
        std::atomic<bool> reached{false};

        void shutdown()
        {
            if (client)
                client->shutdown();
        }
    };

    QString newFolder(const QString &name)
    {
        const QString path = m_tmp.filePath(name + QString::number(++m_count));
        QDir().mkpath(path);
        return path;
    }

    device::DetectedIpod ipodOf(const Rig &r, device::UsbMode mode, bool withVolume) const
    {
        device::DetectedIpod ipod;
        ipod.serial = r.serial;
        ipod.portPath = kPort;
        ipod.mode = mode;
        ipod.model = r.modelId;
        ipod.variant.key = QStringLiteral("e2e");
        ipod.state = device::IpodState::Ready;
        if (withVolume) {
            ipod.volume = device::VolumeInfo{r.volume, QStringLiteral("vfat"),
                                             QStringLiteral("IPOD"), 4000000, 3000000};
        }
        return ipod;
    }

    // What detection tells the steps, as `connectDetection` does.
    void show(Rig &r, const device::DetectedIpod &ipod)
    {
        r.factory->mounter().seen(ipod);
        r.factory->view().seen(ipod);
        r.factory->volumes().seen(ipod);
        r.factory->sight().seen(ipod);
    }

    void showGone(Rig &r)
    {
        r.factory->view().gone(kPort);
        r.factory->sight().gone(kPort);
    }

    // A new factory over the same iPod: a flow of its own, with its own backup
    // folder (two in the same second would share a dated one).
    void newFactory(Rig &r)
    {
        r.runner.reset();
        r.factory.reset();
        r.backupRoot = newFolder(QStringLiteral("backups"));

        LinuxSourcesConfig config;
        config.serial = r.serial;
        config.family = r.classic ? Family::Dfu : Family::Partition;
        config.model.id = r.modelId;
        config.model.name = r.classic ? QStringLiteral("iPod Classic") : QStringLiteral("iPod");
        config.model.bootloaderTarget = r.target;
        device::Variant variant;
        variant.key = QStringLiteral("e2e");
        variant.capacityGb = r.classic ? 120 : 30;
        config.model.variants = {variant};
        config.backupRoot = r.backupRoot;
        config.dataDir = r.data;
        config.osReleaseRoot = r.etc;
        config.udevRuleFile = r.udevRule;
        config.appVersion = QStringLiteral("0.2.0");
        config.installRecords = r.installRecords;
        config.portPath = kPort;
        config.volumeReturnMs = 5000;
        config.returnMs = 5000;
        config.udisksProbe = [] { return UdisksInfo{true, true, QStringLiteral("2.10.1")}; };
        const bool realWrites = r.realWrites;
        config.helloReader = [realWrites] {
            return QJsonObject{
                {"protocol", 1}, {"realWrites", realWrites}, {"usb", true}, {"classic", true}};
        };

        auto service = std::make_unique<FakeService>();
        service->mountPoint = r.volume;
        FakeService *raw = service.get();
        Rig *rig = &r;
        std::unique_ptr<HelperCommands> commands;
        if (r.classic) {
            auto classic = std::make_unique<ClassicCommands>();
            classic->system = r.system.get();
            classic->disk.ok = true;
            classic->disk.blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb");
            classic->disk.sizeBytes = qint64(120) * 1000 * 1000 * 1000;
            classic->disk.usbBus = 1;
            classic->disk.usbDevice = 5;
            classic->disk.usbProduct = QStringLiteral("1261");
            r.classicCommands = classic.get();
            commands = std::move(classic);
        }
        const QJsonObject target =
            r.classic ? QJsonObject() : helper::HelperClient::imageTarget(r.image, r.sectorSize);
        r.factory = std::make_unique<LinuxStepFactory>(
            config, *r.client, target, QString(), std::move(service),
            [] { return QList<device::LinuxDisk>{ipodDisk()}; }, std::move(commands));
        raw->onMount = [this, rig] { show(*rig, ipodOf(*rig, device::UsbMode::Normal, true)); };
        if (r.classic) {
            r.classicCommands->onStage = [this, rig](const QString &name) {
                if (name == QLatin1String("wait-device")) {
                    showGone(*rig);
                    show(*rig, ipodOf(*rig, device::UsbMode::Dfu, false));
                }
            };
            r.classicCommands->onResult = [this, rig](const HelperAnswer &answer) {
                if (!answer.ok())
                    return;
                showGone(*rig);
                device::DetectedIpod back = ipodOf(*rig, device::UsbMode::Normal, true);
                back.running = rig->classicCommands->uninstalling ? device::RunningFirmware::Apple
                                                                  : device::RunningFirmware::Other;
                back.disk = device::DiskInfo{QStringLiteral("/dev/sdb"), 1000, 4096};
                show(*rig, back);
            };
        }
        show(r, ipodOf(r, device::UsbMode::Normal, true));
    }

    // An iPod of an older family on an image, its volume a folder.
    std::unique_ptr<Rig> legacyRig(const QString &model, const QString &serial,
                                   const QString &kill = QString())
    {
        auto r = std::make_unique<Rig>();
        r->serial = serial;
        r->modelId = model;
        FakePodSpec spec;
        spec.modelId = model;
        spec.path = m_tmp.filePath(QStringLiteral("%1-%2.img").arg(model).arg(++m_count));
        const auto pod = FakePod::build(spec);
        if (!pod.ok)
            return nullptr;
        r->image = spec.path;
        r->sectorSize = pod.sectorSize;
        r->volume = newFolder(QStringLiteral("volume"));
        r->music = r->volume + "/music";
        put(r->music + "/a.mp3", pattern(5000, 9));
        put(r->music + "/b/c.mp3", pattern(3000, 4));
        put(r->volume + "/Playlists/a.m3u", "a.mp3\n");
        openCommon(*r, kill);
        const HelperAnswer state =
            ClientHelperCommands(*r->client,
                                 helper::HelperClient::imageTarget(r->image, r->sectorSize))
                .call(QString(helper::cmd::readState), QJsonObject());
        if (!state.ok())
            return nullptr;
        r->target = state.data.value(QStringLiteral("target")).toString();
        r->package = makeTestPackage(
            newFolder(QStringLiteral("package")), r->target, BootloaderFile::build(r->target),
            infoFiles(r->target, {{QStringLiteral("a.txt"), "a"}, {QStringLiteral("b/c"), "c"}}));
        return r;
    }

    // The Classic: its volume is a folder, DFU is the fake iPod.
    std::unique_ptr<Rig> classicRig(const QString &serial, bool realWrites = true)
    {
        auto r = std::make_unique<Rig>();
        r->classic = true;
        r->serial = serial;
        r->modelId = QStringLiteral("ipod-classic");
        r->target = QStringLiteral("ipod6g");
        r->volume = newFolder(QStringLiteral("cvolume"));
        r->music = r->volume + "/music";
        put(r->music + "/a.mp3", pattern(5000, 9));
        put(r->volume + "/Playlists/a.m3u", "a.mp3\n");
        put(r->volume + "/" + kFolder + "/config.cfg", "volume: 3\n");
        openCommon(*r, QString());
        r->realWrites = realWrites;
        r->layer = std::make_unique<FakeSystemLayer>();
        r->dfu = std::make_unique<FakeDfuDevice>();
        r->layer->device = r->dfu.get();
        r->layer->bus = {[] {
            FakeBusStep s;
            s.fromMs = 0;
            s.devices = {dfuDevice()};
            return s;
        }()};
        r->system = std::make_unique<helper::SystemLayer>();
        r->system->realWrites = realWrites;
        r->system->dfuPartsDir = m_parts.path();
        FakeSystemLayer *fake = r->layer.get();
        r->system->classicLayer = [fake](helper::SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        r->system->sleep = r->layer->options().sleep;
        r->package = makeTestPackage(newFolder(QStringLiteral("package")), r->target,
                                     classicBootFile(pattern(4096, 0x42)),
                                     infoFiles(r->target, {{QStringLiteral("a.txt"), "a"}}));
        return r;
    }

    void openCommon(Rig &r, const QString &kill)
    {
        r.data = newFolder(QStringLiteral("data"));
        r.etc = newFolder(QStringLiteral("root"));
        put(r.etc + "/etc/os-release", "ID=fedora\nVERSION_ID=44\n");
        r.udevRule = m_tmp.filePath(QStringLiteral("udev%1.rules").arg(++m_count));
        put(r.udevRule, "# rule\n");
        r.client = std::make_unique<helper::HelperClient>();
        r.client->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
#ifdef TACETOS_HELPER_KILLTEST_PROGRAM
        if (!kill.isEmpty()) {
            r.client->setProgram(QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM));
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("TACETOS_TEST_KILL"), kill);
            r.client->setEnvironment(env);
        }
#else
        Q_UNUSED(kill)
#endif
    }

    // The runner of a flow: the answers of the user are given as the screens do.
    void makeRunner(Rig &r)
    {
        const bool hold = !r.holdStep.isEmpty();
        Rig *rig = &r;
        const StepFactory real = r.factory->factory();
        r.reached = false;
        r.runner = std::make_unique<WizardRunner>(
            r.data, [rig, real, hold](const QString &name) -> std::unique_ptr<Step> {
                std::unique_ptr<Step> made = real(name);
                if (hold && made && name == rig->holdStep)
                    return std::make_unique<HoldingStep>(std::move(made), rig->reached);
                return made;
            });
        WizardRunner *runner = r.runner.get();
        connect(runner, &WizardRunner::stepWaiting, runner,
                [runner, rig](const QString &name, WaitKind kind, const QJsonObject &info) {
                    rig->waits << name;
                    UserAnswer answer;
                    if (kind == WaitKind::TypedName)
                        answer.text = info.value(QStringLiteral("ipodName")).toString();
                    if (kind == WaitKind::Offer)
                        rig->offers << info.value(QStringLiteral("offer")).toString();
                    runner->answer(answer);
                });
    }

    // Runs a whole flow, or resumes it. Returns the code it stopped with, empty
    // when every step is done.
    QString runFlow(Rig &r, FlowKind kind, InstallPath path, bool resume = false)
    {
        if (!resume)
            newFactory(r);
        else if (!r.factory)
            newFactory(r);
        makeRunner(r);
        const Family family = r.classic ? Family::Dfu : Family::Partition;
        WizardRunner::Start s;
        s.serial = r.serial;
        s.model = r.modelId;
        s.shape = {family, kind, path, false};
        if (kind == FlowKind::Install) {
            s.packageManifest = r.package.manifest;
            s.packageSha256 = r.package.sha256;
        }
        QSignalSpy finished(r.runner.get(), &WizardRunner::flowFinished);
        QSignalSpy stopped(r.runner.get(), &WizardRunner::flowStopped);
        const QString refused =
            resume ? r.runner->resume(r.serial, r.modelId, family) : r.runner->start(s);
        if (!refused.isEmpty())
            return QStringLiteral("not started: ") + refused;
        if (!QTest::qWaitFor([&] { return !finished.isEmpty() || !stopped.isEmpty(); }, 120000))
            return QStringLiteral("timeout");
        r.runner->waitFinished(10000);
        return stopped.isEmpty() ? QString() : stopped.first().first().toString();
    }

    // Starts a flow and cuts it where the first run holds still.
    void runAndCut(Rig &r, FlowKind kind, InstallPath path)
    {
        newFactory(r);
        makeRunner(r);
        const Family family = r.classic ? Family::Dfu : Family::Partition;
        WizardRunner::Start s;
        s.serial = r.serial;
        s.model = r.modelId;
        s.shape = {family, kind, path, false};
        s.packageManifest = r.package.manifest;
        s.packageSha256 = r.package.sha256;
        QCOMPARE(r.runner->start(s), QString());
        QVERIFY2(QTest::qWaitFor([&r] { return r.reached.load(); }, 60000), qPrintable(r.holdStep));
        // The runner goes away in the middle of the step: a cut.
        r.runner.reset();
    }

    QString stateOf(Rig &r)
    {
        ClientHelperCommands commands(*r.client,
                                      helper::HelperClient::imageTarget(r.image, r.sectorSize));
        return commands.call(QString(helper::cmd::readState), QJsonObject())
            .data.value(QStringLiteral("state"))
            .toString();
    }

    // The newest backup folder of a flow, from the root the factory had.
    QString backupOf(const Rig &r) const
    {
        const QStringList serials = QDir(r.backupRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (serials.isEmpty())
            return QString();
        const QDir serialDir(QDir(r.backupRoot).filePath(serials.first()));
        const QStringList dated = serialDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        return dated.isEmpty() ? QString() : serialDir.filePath(dated.last());
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

    // The Classic, clean install then uninstall: diagnosis, full backup, the
    // volume emptied by files, firmware, bootloader by DFU, TacetOS detected;
    // then back to Apple's firmware with the settings in the backup.
    void classicCleanInstallThenUninstall()
    {
        auto r = classicRig(QStringLiteral("8K7E2ECLASSIC"));
        QVERIFY(r);
        const QByteArray music = treeHash(r->music);
        QVERIFY(!music.isEmpty());

        QCOMPARE(runFlow(*r, FlowKind::Install, InstallPath::Clean), QString());
        QVERIFY(r->classicCommands->asked.contains(QString(helper::cmd::classicDfu)));
        QVERIFY(!r->dfu->received().isEmpty());
        QVERIFY(QDir(r->volume).exists(kFolder));
        QVERIFY(QFile::exists(r->volume + "/" + kFolder + "/a.txt"));
        // The full backup holds the music, and the volume was emptied by files.
        const QString backup = backupOf(*r);
        QVERIFY(!backup.isEmpty());
        QVERIFY(!QFile::exists(r->music + "/a.mp3"));
        QCOMPARE(treeHash(backupVolumeFolder(backup) + "/music"), music);
        QCOMPARE(r->waits.filter(QStringLiteral("format")).size(), 1);

        // Uninstall: the layer gets a new fake iPod in DFU mode.
        r->waits.clear();
        r->layer = std::make_unique<FakeSystemLayer>();
        r->dfu = std::make_unique<FakeDfuDevice>();
        r->layer->device = r->dfu.get();
        r->layer->bus = {[] {
            FakeBusStep s;
            s.fromMs = 0;
            s.devices = {dfuDevice()};
            return s;
        }()};
        FakeSystemLayer *fake = r->layer.get();
        r->system->classicLayer = [fake](helper::SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        r->system->sleep = r->layer->options().sleep;
        put(r->music + "/a.mp3", pattern(5000, 9));
        QCOMPARE(runFlow(*r, FlowKind::Uninstall, InstallPath::None), QString());
        QVERIFY(!r->dfu->received().isEmpty());
        QVERIFY(!QDir(r->volume).exists(kFolder));
        const QString settings = backupOf(*r);
        QVERIFY(!settings.isEmpty());
        QVERIFY(QDir(backupVolumeFolder(settings)).exists());
        QCOMPARE(treeHash(r->music), music);
    }

    void legacyKeepMusicThenUninstall_data()
    {
        QTest::addColumn<QString>("model");
        QTest::newRow("video") << "ipod-video";
        QTest::newRow("nano2g") << "nano-2g";
    }

    // An older iPod, "keep my music": settings saved, firmware, bootloader,
    // state `installed`; then the uninstall back to `clean`. The music is the
    // same before and after.
    void legacyKeepMusicThenUninstall()
    {
        QFETCH(QString, model);
        auto r = legacyRig(model, QStringLiteral("8K7E2ELEGACY"));
        QVERIFY(r);
        const QByteArray music = treeHash(r->music);
        QVERIFY(!music.isEmpty());
        QCOMPARE(stateOf(*r), QStringLiteral("clean"));

        QCOMPARE(runFlow(*r, FlowKind::Install, InstallPath::Keep), QString());
        QCOMPARE(stateOf(*r), QStringLiteral("installed"));
        QVERIFY(QFile::exists(r->volume + "/" + kFolder + "/a.txt"));
        QCOMPARE(treeHash(r->music), music);

        QCOMPARE(runFlow(*r, FlowKind::Uninstall, InstallPath::None), QString());
        QCOMPARE(stateOf(*r), QStringLiteral("clean"));
        QVERIFY(!QDir(r->volume).exists(kFolder));
        QCOMPARE(treeHash(r->music), music);
        r->shutdown();
    }

    void cutThenResume_data()
    {
        QTest::addColumn<QString>("which");
        QTest::newRow("video-firmware") << "video-firmware";
        QTest::newRow("video-bootloader") << "video-bootloader";
        QTest::newRow("classic-backup") << "classic-backup";
    }

    // The journey cut in the middle, then started again from the progress file.
    void cutThenResume()
    {
        QFETCH(QString, which);
        if (which == QLatin1String("video-firmware")) {
            auto r = legacyRig(QStringLiteral("ipod-video"), QStringLiteral("8K7E2ECUTFW"));
            QVERIFY(r);
            const QByteArray music = treeHash(r->music);
            r->holdStep = QString(step::firmware);
            runAndCut(*r, FlowKind::Install, InstallPath::Keep);
            QVERIFY(ProgressStore(r->data).exists(r->serial));
            QCOMPARE(stateOf(*r), QStringLiteral("clean"));

            r->holdStep.clear();
            QCOMPARE(runFlow(*r, FlowKind::Install, InstallPath::Keep, true), QString());
            QCOMPARE(stateOf(*r), QStringLiteral("installed"));
            QVERIFY(QFile::exists(r->volume + "/" + kFolder + "/a.txt"));
            QCOMPARE(treeHash(r->music), music);
            QVERIFY(!ProgressStore(r->data).exists(r->serial));
            r->shutdown();
            return;
        }
        if (which == QLatin1String("video-bootloader")) {
#if !defined(Q_OS_LINUX) || !defined(TACETOS_HELPER_KILLTEST_PROGRAM)
            QSKIP("the cut program is built on Linux");
#else
            int cuts = 0;
            for (const char *point : {"write-before:1", "write-torn:1", "write-after:1"}) {
                auto cut = legacyRig(QStringLiteral("ipod-video"), QStringLiteral("8K7E2ECUTBL"),
                                     QString::fromLatin1(point));
                QVERIFY2(cut, point);
                const QByteArray music = treeHash(cut->music);
                const QString first = runFlow(*cut, FlowKind::Install, InstallPath::Keep);
                cut->shutdown();
                QVERIFY2(first == QString(helper::err::helperCrashed), qPrintable(first));
                ++cuts;

                // The next start of the app: the normal helper, same image.
                cut->client = std::make_unique<helper::HelperClient>();
                cut->client->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
                cut->factory.reset();
                const QString state = stateOf(*cut);
                const QString second = runFlow(*cut, FlowKind::Install, InstallPath::Keep, true);
                if (state == QLatin1String("inconsistent")) {
                    QCOMPARE(second, QString(helper::err::checkFailed));
                } else {
                    QVERIFY2(second.isEmpty(),
                             qPrintable(QString::fromLatin1(point) + ": " + state + ": " + second));
                    QCOMPARE(stateOf(*cut), QStringLiteral("installed"));
                }
                QCOMPARE(treeHash(cut->music), music);
                cut->shutdown();
                if (QTest::currentTestFailed())
                    return;
            }
            QCOMPARE(cuts, 3);
#endif
            return;
        }
        // classic-backup: the runner goes away while the volume is copied; the
        // flow starts again and ends, with the music safe in the backup.
        auto r = classicRig(QStringLiteral("8K7E2ECUTCL"));
        QVERIFY(r);
        const QByteArray music = treeHash(r->music);
        r->holdStep = QString(step::backupFull);
        runAndCut(*r, FlowKind::Install, InstallPath::Clean);
        QVERIFY(ProgressStore(r->data).exists(r->serial));
        // Nothing was emptied: the format comes after the backup.
        QCOMPARE(treeHash(r->music), music);
        QVERIFY(r->classicCommands->asked.filter(QString(helper::cmd::classicDfu)).isEmpty());

        r->holdStep.clear();
        QCOMPARE(runFlow(*r, FlowKind::Install, InstallPath::Clean, true), QString());
        const QString backup = backupOf(*r);
        QVERIFY(!backup.isEmpty());
        QCOMPARE(treeHash(backupVolumeFolder(backup) + "/music"), music);
        QVERIFY(QDir(r->volume).exists(kFolder));
        QVERIFY(!ProgressStore(r->data).exists(r->serial));
    }

    // A build that does not write to real iPods: the Classic stops before the
    // iPod is ejected, nothing is sent; the older iPod only ever writes to its
    // image.
    void devBuildNeverWritesRealDisk()
    {
        if (realWritesAllowed())
            QSKIP("this build writes to real iPods");
        auto r = classicRig(QStringLiteral("8K7E2EDEV"), false);
        QVERIFY(r);
        QCOMPARE(runFlow(*r, FlowKind::Install, InstallPath::Clean),
                 QString(helper::err::devBuild));
        QVERIFY(r->dfu->received().isEmpty());
        QCOMPARE(r->layer->unmountCalls, 0);

        auto old = legacyRig(QStringLiteral("ipod-video"), QStringLiteral("8K7E2EDEVOLD"));
        QVERIFY(old);
        old->realWrites = false;
        QCOMPARE(runFlow(*old, FlowKind::Install, InstallPath::Keep), QString());
        QCOMPARE(stateOf(*old), QStringLiteral("installed"));
        QVERIFY(!old->client->hello().value(QStringLiteral("realWrites")).toBool());
        old->shutdown();
    }
};

QTEST_GUILESS_MAIN(TestWizardE2e)
#include "tst_wizard_e2e.moc"
