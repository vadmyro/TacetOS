// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The window of the wizard over the real factory of steps (the install, the uninstall and
// the resume after the cross): the real WizardWindow and the real LinuxStepFactory on
// the fake system layer, the fake iPod in DFU mode and the disk service that
// answers from memory. The user is a test that clicks the buttons of the pages.
// Never libusb, udisks2 or a real iPod.

#include "../../src/device/constants.h"
#include "../../src/installer/constants.h"
#include "../wizard/testpackage.h"
#include "classiccmds.h"
#include "dfuimage.h"
#include "fakedfudevice.h"
#include "fakesystemlayer.h"
#include "helperclient.h"
#include "helperconstants.h"
#include "progress.h"
#include "runner.h"
#include "stepfactory.h"
#include "wizard/backuppage.h"
#include "wizard/dfututorial.h"
#include "wizard/diagnosepage.h"
#include "wizard/donepage.h"
#include "wizard/formatpage.h"
#include "wizard/installpage.h"
#include "wizard/uninstallpage.h"
#include "wizard/wizardwindow.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLineEdit>
#include <QPointer>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>

using namespace tacetos;
using namespace tacetos::wizard;
using namespace tacetos::installer;
using tacetos::wizard::testing::makeTestPackage;
using tacetos::wizard::testing::TestPackage;
namespace device = tacetos::device;
namespace helper = tacetos::helper;

namespace {

const QString kPort = QStringLiteral("1-2");
const QString kFolder = QString(device::kFirmwareFolder);

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

QPushButton *buttonNamed(QWidget *parent, const char *name)
{
    return parent->findChild<QPushButton *>(QString::fromLatin1(name));
}

// A step that does nothing: the steps before the one a test looks at.
class NoopStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

// A step that only asks the user and ends with the answer.
class AskStep : public Step
{
public:
    AskStep(WaitKind kind, const QJsonObject &info) : m_kind(kind), m_info(info) {}
    StepOutcome run(StepContext &context) override
    {
        return context.waitForUser(m_kind, m_info).cancelled ? StepOutcome::cancelled()
                                                             : StepOutcome::done();
    }

private:
    WaitKind m_kind;
    QJsonObject m_info;
};

// What the window must show while a step waits with a kind of wait.
struct WaitCase
{
    QString step;
    FlowShape shape;
    QString page;
    QJsonObject info;
    // The page of the step is asking, not only showing.
    std::function<bool(WizardWindow &)> asking;
    // The real step asks (the page needs its checks), not a step that only asks.
    bool realStep = false;
};

// A new value of WaitKind without a case here is a warning, so an error of the build.
WaitCase caseOf(WaitKind kind)
{
    switch (kind) {
    case WaitKind::Battery:
        return {QString(step::diagnose),
                {Family::Dfu, FlowKind::Install, InstallPath::Clean, false},
                QStringLiteral("diagnose"),
                {},
                [](WizardWindow &w) {
                    QPushButton *yes = buttonNamed(&w, "batteryYes");
                    return yes && yes->isEnabled();
                },
                true};
    case WaitKind::TypedName:
        return {QString(step::format),
                {Family::Dfu, FlowKind::Install, InstallPath::Clean, false},
                QStringLiteral("format"),
                {{QStringLiteral("ipodName"), QStringLiteral("IPOD")}},
                [](WizardWindow &w) { return w.formatPage()->isAsking(); }};
    case WaitKind::ReadyForDfu:
        return {QString(step::classicBootloader),
                {Family::Dfu, FlowKind::Install, InstallPath::Clean, false},
                QStringLiteral("dfu"),
                {{QStringLiteral("model"), QStringLiteral("ipod-classic")}},
                [](WizardWindow &w) { return w.dfuPage()->isAsking(); }};
    case WaitKind::DiskMode:
        return {QString(step::diskMode),
                {Family::Partition, FlowKind::Install, InstallPath::Keep, false},
                QStringLiteral("write"),
                {{QStringLiteral("model"), QStringLiteral("ipod-video")}},
                [](WizardWindow &w) { return w.installPage()->isAsking(); }};
    case WaitKind::Offer:
        // Every offer of the steps comes from a step with a page of its own: here the offer to
        // put the settings back, asked by the firmware.
        return {QString(step::firmware),
                {Family::Dfu, FlowKind::Install, InstallPath::Clean, false},
                QStringLiteral("write"),
                {{QStringLiteral("offer"), QStringLiteral("settings")},
                 {QStringLiteral("date"), QStringLiteral("2026-11-20")}},
                [](WizardWindow &w) { return w.installPage()->isAsking(); }};
    }
    return {};
}

} // namespace

class TestWizardWindowE2e : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QTemporaryDir m_runtime;
    QTemporaryDir m_parts;
    int m_count = 0;

    // The Classic of a test with everything its flow needs.
    struct Rig
    {
        QString serial;
        QString volume;
        QString music;
        QString data;
        QString etc;
        QString udevRule;
        QString backupRoot;
        TestPackage package;

        std::unique_ptr<helper::HelperClient> client;
        std::unique_ptr<FakeSystemLayer> layer;
        std::unique_ptr<FakeDfuDevice> dfu;
        std::unique_ptr<helper::SystemLayer> system;
        ClassicCommands *classicCommands = nullptr;
        std::unique_ptr<LinuxStepFactory> factory;
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
        ipod.model = QStringLiteral("ipod-classic");
        ipod.variant.key = QStringLiteral("e2e");
        ipod.state = device::IpodState::Ready;
        if (withVolume) {
            ipod.volume = device::VolumeInfo{r.volume, QStringLiteral("vfat"),
                                             QStringLiteral("IPOD"), 1000000000, 900000000};
        }
        return ipod;
    }

    // What detection tells the steps, as `connectDetection` does.
    // `fromMount`: the answer of a mount, made while the mounter holds its lock, so it does not
    // go back to the mounter (it knows the port already).
    void show(Rig &r, const device::DetectedIpod &ipod, bool fromMount = false)
    {
        if (!fromMount)
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

    std::unique_ptr<Rig> makeRig(const QString &serial)
    {
        auto r = std::make_unique<Rig>();
        r->serial = serial;
        r->volume = newFolder(QStringLiteral("cvolume"));
        r->music = r->volume + "/music";
        put(r->music + "/a.mp3", pattern(5000, 9));
        put(r->volume + "/Playlists/a.m3u", "a.mp3\n");
        put(r->volume + "/" + kFolder + "/config.cfg", "volume: 3\n");
        r->data = newFolder(QStringLiteral("data"));
        r->etc = newFolder(QStringLiteral("root"));
        put(r->etc + "/etc/os-release", "ID=fedora\nVERSION_ID=44\n");
        r->udevRule = m_tmp.filePath(QStringLiteral("udev%1.rules").arg(++m_count));
        put(r->udevRule, "# rule\n");
        r->backupRoot = newFolder(QStringLiteral("backups"));
        // Never started: the Classic goes through `classic-dfu` in this process.
        r->client = std::make_unique<helper::HelperClient>();
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
        r->system->realWrites = true;
        r->system->dfuPartsDir = m_parts.path();
        FakeSystemLayer *fake = r->layer.get();
        r->system->classicLayer = [fake](helper::SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        r->system->sleep = r->layer->options().sleep;
        r->package =
            makeTestPackage(newFolder(QStringLiteral("package")), QStringLiteral("ipod6g"),
                            classicBootFile(pattern(4096, 0x42)),
                            infoFiles(QStringLiteral("ipod6g"), {{QStringLiteral("a.txt"), "a"}}));

        LinuxSourcesConfig config;
        config.serial = r->serial;
        config.family = Family::Dfu;
        config.model.id = QStringLiteral("ipod-classic");
        config.model.name = QStringLiteral("iPod Classic");
        config.model.bootloaderTarget = QStringLiteral("ipod6g");
        device::Variant variant;
        variant.key = QStringLiteral("e2e");
        variant.capacityGb = 120;
        config.model.variants = {variant};
        config.backupRoot = r->backupRoot;
        config.dataDir = r->data;
        config.osReleaseRoot = r->etc;
        config.udevRuleFile = r->udevRule;
        config.appVersion = QStringLiteral("0.2.0");
        config.portPath = kPort;
        config.volumeReturnMs = 5000;
        config.returnMs = 5000;
        config.udisksProbe = [] { return UdisksInfo{true, true, QStringLiteral("2.10.1")}; };
        config.helloReader = [] {
            return QJsonObject{
                {"protocol", 1}, {"realWrites", true}, {"usb", true}, {"classic", true}};
        };

        auto service = std::make_unique<FakeService>();
        service->mountPoint = r->volume;
        FakeService *raw = service.get();
        auto classic = std::make_unique<ClassicCommands>();
        classic->system = r->system.get();
        classic->disk.ok = true;
        classic->disk.blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb");
        classic->disk.sizeBytes = qint64(120) * 1000 * 1000 * 1000;
        classic->disk.usbBus = 1;
        classic->disk.usbDevice = 5;
        classic->disk.usbProduct = QStringLiteral("1261");
        r->classicCommands = classic.get();
        Rig *rig = r.get();
        r->factory = std::make_unique<LinuxStepFactory>(
            config, *r->client, QJsonObject(), QString(), std::move(service),
            [] { return QList<device::LinuxDisk>{ipodDisk()}; }, std::move(classic));
        raw->onMount = [this, rig] {
            show(*rig, ipodOf(*rig, device::UsbMode::Normal, true), true);
        };
        r->classicCommands->onStage = [this, rig](const QString &name) {
            if (name == QLatin1String("wait-device")) {
                showGone(*rig);
                show(*rig, ipodOf(*rig, device::UsbMode::Dfu, false));
            }
        };
        r->classicCommands->onResult = [this, rig](const HelperAnswer &answer) {
            if (!answer.ok())
                return;
            showGone(*rig);
            device::DetectedIpod back = ipodOf(*rig, device::UsbMode::Normal, true);
            back.running = rig->classicCommands->uninstalling ? device::RunningFirmware::Apple
                                                              : device::RunningFirmware::Other;
            back.disk = device::DiskInfo{QStringLiteral("/dev/sdb"), 1000, 4096};
            show(*rig, back);
        };
        show(*r, ipodOf(*r, device::UsbMode::Normal, true));
        return r;
    }

    // A new fake iPod in DFU mode for the next flow on the same rig.
    void freshDfu(Rig &r)
    {
        r.layer = std::make_unique<FakeSystemLayer>();
        r.dfu = std::make_unique<FakeDfuDevice>();
        r.layer->device = r.dfu.get();
        r.layer->bus = {[] {
            FakeBusStep s;
            s.fromMs = 0;
            s.devices = {dfuDevice()};
            return s;
        }()};
        FakeSystemLayer *fake = r.layer.get();
        r.system->classicLayer = [fake](helper::SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        r.system->sleep = r.layer->options().sleep;
    }

    // The dated folder of the backup of the rig, empty when there is none.
    QString backupOf(const Rig &r) const
    {
        const QStringList serials = QDir(r.backupRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (serials.isEmpty())
            return QString();
        const QDir serialDir(QDir(r.backupRoot).filePath(serials.first()));
        const QStringList dated = serialDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        return dated.isEmpty() ? QString() : serialDir.filePath(dated.last());
    }

    WizardTarget targetOf(const Rig &r, Family family = Family::Dfu) const
    {
        WizardTarget t;
        t.serial = r.serial;
        t.model = QStringLiteral("ipod-classic");
        t.name = QStringLiteral("iPod Classic");
        t.bootloaderTarget = QStringLiteral("ipod6g");
        t.family = family;
        t.volumeUsed = 100000000;
        t.keepBytes = 1000000;
        return t;
    }

    WizardOptions optionsOf(const Rig &r) const
    {
        WizardOptions o;
        o.dataDir = r.data;
        o.backupRoot = r.backupRoot;
        o.realWrites = true;
        o.freeBytes = [](const QString &) { return qint64(500) << 30; };
        return o;
    }

    // The page of the step is the one that shows, and it is the widget the window says.
    static void expectPage(WizardWindow &w, const QString &name, QWidget *page)
    {
        QTRY_COMPARE(w.pageName(), name);
        QVERIFY2(page->isVisibleTo(&w), qPrintable(name));
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

    // Each kind of wait a step can make has a page that shows it and asks: a kind the window
    // does not know would leave the flow waiting behind a page with a bar (the real trial of the
    // Classic stopped so).
    void everyWaitKindHasAPage()
    {
        auto r = makeRig(QStringLiteral("8K7E2EWAITS"));
        QVERIFY(r);
        for (int i = 0; i <= int(WaitKind::Offer); ++i) {
            const WaitKind kind = WaitKind(i);
            const WaitCase c = caseOf(kind);
            QVERIFY2(!c.step.isEmpty(), qPrintable(QString::number(i)));
            const StepFactory real = r->factory->factory();
            const QString waiting = c.step;
            // The steps before the one of the test are not run: the real factory makes the others.
            const QStringList before = stepsFor(c.shape);
            const int at = before.indexOf(waiting);
            QVERIFY2(at >= 0, qPrintable(waiting));
            const StepFactory wrapped = [&](const QString &name) -> std::unique_ptr<Step> {
                if (name == waiting && !c.realStep)
                    return std::make_unique<AskStep>(kind, c.info);
                if (before.indexOf(name) < at)
                    return std::make_unique<NoopStep>();
                return real(name);
            };
            WizardRunner runner(newFolder(QStringLiteral("progress")), wrapped);
            WizardTarget t = targetOf(*r, c.shape.family);
            if (c.shape.family == Family::Partition)
                t.model = QStringLiteral("ipod-video");
            WizardOptions o = optionsOf(*r);
            o.dataDir = newFolder(QStringLiteral("window"));
            WizardWindow w(t, o, &runner, WizardEntry::Install);
            w.diagnosePage()->setPause(0);
            w.backupPage()->setPause(0);
            w.setPath(c.shape.path);
            w.setPackage(r->package.manifest);
            QVERIFY2(buttonNamed(&w, "wizardMain")->isEnabled(), qPrintable(c.step));
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            QSignalSpy ended(&w, &WizardWindow::flowEnded);
            QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
            QTRY_COMPARE_WITH_TIMEOUT(w.pageName(), c.page, 10000);
            QTRY_VERIFY2_WITH_TIMEOUT(c.asking(w), qPrintable(c.step), 5000);
            QCOMPARE(w.stepState(at), QStringLiteral("waiting"));
            runner.cancel();
            QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 10000);
            QVERIFY(runner.waitFinished(10000));
        }
    }

    // The Classic, clean install, by the clicks of the user: the checks with the question of the
    // battery, the backup, the name typed, the erase, the firmware, the DFU guide, the end. The
    // page of each step shows when the step runs.
    void classicCleanInstall()
    {
        auto r = makeRig(QStringLiteral("8K7E2ECLASSIC"));
        QVERIFY(r);
        WizardRunner runner(r->data, r->factory->factory());
        WizardWindow w(targetOf(*r), optionsOf(*r), &runner, WizardEntry::Install);
        w.diagnosePage()->setPause(0);
        w.backupPage()->setPause(0);
        w.setPackage(r->package.manifest);
        QVERIFY2(buttonNamed(&w, "wizardMain")->isEnabled(), "package refused");
        // Shown, so the pages have a layout and the clicks land on the buttons.
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QCOMPARE(w.pageName(), QStringLiteral("install"));
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);

        // The checks, then the question of the battery on their page.
        expectPage(w, QStringLiteral("diagnose"), w.diagnosePage());
        QTRY_VERIFY(buttonNamed(&w, "batteryYes") && buttonNamed(&w, "batteryYes")->isEnabled());
        QCOMPARE(w.stepState(0), QStringLiteral("waiting"));
        QTest::mouseClick(buttonNamed(&w, "batteryYes"), Qt::LeftButton);

        // The erase asks for the name on its own page (the backup went by before it).
        expectPage(w, QStringLiteral("format"), w.formatPage());
        QTRY_VERIFY(w.formatPage()->isAsking());
        auto *field = w.formatPage()->findChild<QLineEdit *>();
        QVERIFY(field);
        field->setText(QStringLiteral("IPOD"));
        QVERIFY(w.formatPage()->confirmButton()->isEnabled());
        QTest::mouseClick(w.formatPage()->confirmButton(), Qt::LeftButton);

        // The DFU guide, and Start once the iPod is held as the guide says.
        expectPage(w, QStringLiteral("dfu"), w.dfuPage());
        QTRY_VERIFY(w.dfuPage()->isAsking());
        QTest::mouseClick(w.dfuPage()->startButton(), Qt::LeftButton);

        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 120000);
        QVERIFY(runner.waitFinished(10000));
        expectPage(w, QStringLiteral("done"), w.donePage());
        for (int i = 0; i < w.stepLabels().size(); ++i)
            QCOMPARE(w.stepState(i), QStringLiteral("done"));
        QVERIFY(!r->dfu->received().isEmpty());
        QVERIFY(QFile::exists(r->volume + "/" + kFolder + "/a.txt"));
        QVERIFY(!QFile::exists(r->music + "/a.mp3"));
    }

    // The Classic installed, then taken back to Apple's firmware by the clicks of the user: the
    // uninstall page, the checks, the DFU guide, the end. The music goes back from the backup, so
    // the volume is the one of before by its hashes.
    void classicInstallThenUninstall()
    {
        auto r = makeRig(QStringLiteral("8K7E2EUNINST"));
        QVERIFY(r);
        const QByteArray before = treeHash(r->music);
        QVERIFY(!before.isEmpty());
        {
            WizardRunner runner(r->data, r->factory->factory());
            WizardWindow w(targetOf(*r), optionsOf(*r), &runner, WizardEntry::Install);
            w.diagnosePage()->setPause(0);
            w.backupPage()->setPause(0);
            w.setPackage(r->package.manifest);
            QVERIFY2(buttonNamed(&w, "wizardMain")->isEnabled(), "package refused");
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            QSignalSpy ended(&w, &WizardWindow::flowEnded);
            QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
            expectPage(w, QStringLiteral("diagnose"), w.diagnosePage());
            QTRY_VERIFY(buttonNamed(&w, "batteryYes") &&
                        buttonNamed(&w, "batteryYes")->isEnabled());
            QTest::mouseClick(buttonNamed(&w, "batteryYes"), Qt::LeftButton);
            expectPage(w, QStringLiteral("format"), w.formatPage());
            QTRY_VERIFY(w.formatPage()->isAsking());
            w.formatPage()->findChild<QLineEdit *>()->setText(QStringLiteral("IPOD"));
            QTest::mouseClick(w.formatPage()->confirmButton(), Qt::LeftButton);
            expectPage(w, QStringLiteral("dfu"), w.dfuPage());
            QTRY_VERIFY(w.dfuPage()->isAsking());
            QTest::mouseClick(w.dfuPage()->startButton(), Qt::LeftButton);
            QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 120000);
            QVERIFY(runner.waitFinished(10000));
            expectPage(w, QStringLiteral("done"), w.donePage());
        }
        // The full backup holds the music, and the volume was emptied by files.
        const QString backup = backupOf(*r);
        QVERIFY(!backup.isEmpty());
        QVERIFY(!QFile::exists(r->music + "/a.mp3"));
        QCOMPARE(treeHash(backupVolumeFolder(backup) + "/music"), before);
        QVERIFY(QFile::exists(r->volume + "/" + kFolder + "/a.txt"));

        // The uninstall, on a new fake iPod in DFU mode.
        freshDfu(*r);
        {
            WizardRunner runner(r->data, r->factory->factory());
            WizardWindow w(targetOf(*r), optionsOf(*r), &runner, WizardEntry::Uninstall);
            w.diagnosePage()->setPause(0);
            w.backupPage()->setPause(0);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            QCOMPARE(w.pageName(), QStringLiteral("uninstall"));
            QVERIFY(buttonNamed(&w, "wizardMain")->isEnabled());
            QSignalSpy ended(&w, &WizardWindow::flowEnded);
            QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);

            expectPage(w, QStringLiteral("diagnose"), w.diagnosePage());
            QTRY_VERIFY(buttonNamed(&w, "batteryYes") &&
                        buttonNamed(&w, "batteryYes")->isEnabled());
            QTest::mouseClick(buttonNamed(&w, "batteryYes"), Qt::LeftButton);

            expectPage(w, QStringLiteral("dfu"), w.dfuPage());
            QTRY_VERIFY(w.dfuPage()->isAsking());
            QTest::mouseClick(w.dfuPage()->startButton(), Qt::LeftButton);

            QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 120000);
            QVERIFY(runner.waitFinished(10000));
            expectPage(w, QStringLiteral("remove"), w.uninstallPage());
            QTRY_VERIFY(w.uninstallPage()->isFinished());
            for (int i = 0; i < w.stepLabels().size(); ++i)
                QCOMPARE(w.stepState(i), QStringLiteral("done"));
        }
        QVERIFY(!r->dfu->received().isEmpty());
        QVERIFY(!QDir(r->volume).exists(kFolder));
        // The music comes back from the backup: the volume is the one of the start.
        QVERIFY(QDir().mkpath(r->music));
        QDirIterator it(backupVolumeFolder(backup) + "/music", QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString from = it.next();
            QVERIFY(QFile::copy(from, r->music + "/" + QFileInfo(from).fileName()));
        }
        QCOMPARE(treeHash(r->music), before);
    }

    // The cross during a wait, then Resume from the card: the checks run again, then the flow
    // goes on at the step that waited, on its page, and ends.
    void crossThenResume()
    {
        auto r = makeRig(QStringLiteral("8K7E2ECROSS"));
        QVERIFY(r);
        const QByteArray before = treeHash(r->music);
        {
            WizardRunner runner(r->data, r->factory->factory());
            WizardWindow w(targetOf(*r), optionsOf(*r), &runner, WizardEntry::Install);
            w.diagnosePage()->setPause(0);
            w.backupPage()->setPause(0);
            w.setPackage(r->package.manifest);
            QVERIFY2(buttonNamed(&w, "wizardMain")->isEnabled(), "package refused");
            bool asked = false;
            w.setAsker([&](const QString &, const QString &) {
                asked = true;
                return true;
            });
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            QSignalSpy ended(&w, &WizardWindow::flowEnded);
            QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
            expectPage(w, QStringLiteral("diagnose"), w.diagnosePage());
            QTRY_VERIFY(buttonNamed(&w, "batteryYes") &&
                        buttonNamed(&w, "batteryYes")->isEnabled());
            QTest::mouseClick(buttonNamed(&w, "batteryYes"), Qt::LeftButton);
            // The erase waits for the name: nothing is written yet, the cross asks and closes.
            expectPage(w, QStringLiteral("format"), w.formatPage());
            QTRY_VERIFY(w.formatPage()->isAsking());
            QVERIFY(!w.isWriting());
            QVERIFY(w.close());
            QVERIFY(asked);
            QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 10000);
            QVERIFY(runner.waitFinished(10000));
        }
        // The backup is complete and the volume untouched: the music is still on the iPod.
        QCOMPARE(treeHash(r->music), before);
        QVERIFY(ProgressStore(r->data).exists(r->serial));

        // The card offers Resume: the window of the entry Resume shows the saved flow.
        WizardRunner runner(r->data, r->factory->factory());
        WizardWindow w(targetOf(*r), optionsOf(*r), &runner, WizardEntry::Resume);
        w.diagnosePage()->setPause(0);
        w.backupPage()->setPause(0);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QCOMPARE(w.pageName(), QStringLiteral("resume"));
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "resumeFlow"), Qt::LeftButton);

        // A resume runs the checks again, then goes on at the erase, not at the backup.
        expectPage(w, QStringLiteral("diagnose"), w.diagnosePage());
        QTRY_VERIFY(buttonNamed(&w, "batteryYes") && buttonNamed(&w, "batteryYes")->isEnabled());
        QTest::mouseClick(buttonNamed(&w, "batteryYes"), Qt::LeftButton);
        expectPage(w, QStringLiteral("format"), w.formatPage());
        QTRY_VERIFY(w.formatPage()->isAsking());
        w.formatPage()->findChild<QLineEdit *>()->setText(QStringLiteral("IPOD"));
        QTest::mouseClick(w.formatPage()->confirmButton(), Qt::LeftButton);
        expectPage(w, QStringLiteral("dfu"), w.dfuPage());
        QTRY_VERIFY(w.dfuPage()->isAsking());
        QTest::mouseClick(w.dfuPage()->startButton(), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 120000);
        QVERIFY(runner.waitFinished(10000));
        expectPage(w, QStringLiteral("done"), w.donePage());
        QVERIFY(QFile::exists(r->volume + "/" + kFolder + "/a.txt"));
        QVERIFY(!ProgressStore(r->data).exists(r->serial));
        const QString backup = backupOf(*r);
        QVERIFY(!backup.isEmpty());
        QCOMPARE(treeHash(backupVolumeFolder(backup) + "/music"), before);
    }
};

QTEST_MAIN(TestWizardWindowE2e)
#include "tst_wizardwindow_e2e.moc"
