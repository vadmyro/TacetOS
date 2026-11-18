// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The real sources of Linux (sources.h) and the factory of steps
// (stepfactory.h): the disk service answers from memory, the helper is not
// started, and the volume is a temporary folder.

#include "backup.h"
#include "constants.h"
#include "helperclient.h"
#include "sources.h"
#include "stepfactory.h"
#include "udisks.h"
#include "wizardconstants.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos;
using namespace tacetos::wizard;

namespace {

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
    device::UdisksReply mount(const QString &blockPath, bool) override
    {
        calls->append(QStringLiteral("mount ") + blockPath.section('/', -1));
        return {{}, {}, QStringLiteral("/run/media/u/IPOD")};
    }
    device::UdisksReply unmount(const QString &) override { return {}; }
    device::UdisksReply eject(const QString &) override { return {}; }
    device::UdisksReply powerOff(const QString &) override { return {}; }

    device::UdisksObjects objects;
    QStringList *calls = nullptr;
};

device::LinuxDisk ipodDisk(const QString &port)
{
    device::LinuxDisk disk;
    disk.name = QStringLiteral("sdb");
    disk.node = QStringLiteral("/dev/sdb");
    disk.portPath = port;
    disk.partitions = {{QStringLiteral("sdb2"), 2, 8, 18}};
    return disk;
}

// A helper that answers from memory.
class FakeCommands : public HelperCommands
{
public:
    HelperAnswer call(const QString &cmd, const QJsonObject &, const HelperWatch &) override
    {
        calls << cmd;
        return answers.value(cmd, HelperAnswer{QStringLiteral("unavailable"), {}, {}});
    }
    QMap<QString, HelperAnswer> answers;
    QStringList calls;
};

device::DetectedIpod detected(const QString &serial, const QString &port, const QString &mount)
{
    device::DetectedIpod ipod;
    ipod.serial = serial;
    ipod.portPath = port;
    ipod.model = QStringLiteral("ipod-video");
    ipod.state = device::IpodState::Ready;
    device::VolumeInfo volume;
    volume.mountPoint = mount;
    volume.filesystem = QStringLiteral("vfat");
    volume.label = QStringLiteral("IPOD");
    ipod.volume = volume;
    return ipod;
}

LinuxSourcesConfig configFor(Family family, const QString &serial, const QString &backupRoot)
{
    LinuxSourcesConfig config;
    config.serial = serial;
    config.family = family;
    config.model.id = QStringLiteral("ipod-video");
    config.model.name = QStringLiteral("iPod Video");
    config.model.bootloaderTarget = QStringLiteral("ipodvideo");
    config.backupRoot = backupRoot;
    config.udisksProbe = [] { return UdisksInfo{true, true, QStringLiteral("2.10.1")}; };
    config.helloReader = [] {
        return QJsonObject{
            {"protocol", 1}, {"realWrites", false}, {"usb", true}, {"classic", true}};
    };
    return config;
}

} // namespace

class TestWizardSources : public QObject
{
    Q_OBJECT

private slots:
    void helloAndSheet()
    {
        const HelperInfo info = helperInfoFromHello(
            {{"protocol", 1}, {"realWrites", true}, {"usb", true}, {"classic", false}});
        QVERIFY(info.found && info.protocolOk && info.realWrites && info.usb && !info.classic);
        QVERIFY(!helperInfoFromHello({}).found);
        QVERIFY(!helperInfoFromHello({{"protocol", 9}}).protocolOk);
        QCOMPARE(
            visibleBuildId(QStringLiteral("<dict><key>BuildID</key><string>9.0.1</string>"
                                          "<key>VisibleBuildID</key>\n<string>2.0.1</string>")),
            QStringLiteral("2.0.1"));
        QVERIFY(visibleBuildId(QStringLiteral("<dict/>")).isEmpty());
    }

    void mounterMountsTheDiskOfTheSerial()
    {
        QStringList calls;
        auto backend = std::make_unique<FakeService>();
        backend->calls = &calls;
        UdisksMounter mounter(std::move(backend), [] {
            return QList<device::LinuxDisk>{ipodDisk(QStringLiteral("1-2"))};
        });

        // Nothing is known of this serial yet.
        QCOMPARE(mounter.mount(QStringLiteral("S1")).error, device::DiskError::NotAnIpodDisk);
        QVERIFY(calls.isEmpty());

        mounter.seen(detected(QStringLiteral("S1"), QStringLiteral("1-2"), QString()));
        const device::DiskResult result = mounter.mount(QStringLiteral("S1"));
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(calls, QStringList{QStringLiteral("mount sdb2")});
        QCOMPARE(mounter.last().mountPoint, QStringLiteral("/run/media/u/IPOD"));

        // Another port: no disk of it.
        mounter.seen(detected(QStringLiteral("S2"), QStringLiteral("1-9"), QString()));
        QCOMPARE(mounter.mount(QStringLiteral("S2")).error, device::DiskError::NotAnIpodDisk);
        QCOMPARE(calls.size(), 1);
    }

    void diagnoseSourcesReadWhatDetectionAndHelperSay()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("volume/Music")));
        QFile song(dir.filePath(QStringLiteral("volume/Music/a.mp3")));
        QVERIFY(song.open(QIODevice::WriteOnly));
        song.write(QByteArray(4096, 'x'));
        song.close();
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("etc")));
        QFile release(dir.filePath(QStringLiteral("etc/os-release")));
        QVERIFY(release.open(QIODevice::WriteOnly));
        release.write("ID=fedora\n");
        release.close();

        helper::HelperClient client;
        FakeCommands commands;
        commands.answers[QStringLiteral("read-state")] = {
            {},
            {},
            {{"method", "partition"},
             {"state", "clean"},
             {"entries", QJsonArray{QJsonObject{{"type", "OSOS"}}}},
             {"slots", QJsonArray{QJsonObject{{"size", 8192}}}}}};
        IpodView view(QStringLiteral("S1"));
        view.seen(detected(QStringLiteral("S1"), QStringLiteral("1-2"),
                           dir.filePath(QStringLiteral("volume"))));
        view.seen(detected(QStringLiteral("S2"), QStringLiteral("1-3"), QString()));

        LinuxSourcesConfig config = configFor(Family::Partition, QStringLiteral("S1"),
                                              dir.filePath(QStringLiteral("backups")));
        config.osReleaseRoot = dir.path();
        config.udevRuleFile = dir.filePath(QStringLiteral("70-test.rules"));
        LinuxDiagnoseSources sources(config, view, commands, client);

        QCOMPARE(sources.os(), Os::Linux);
        QVERIFY(sources.osRelease().contains(QStringLiteral("fedora")));
        QVERIFY(sources.udisks().running);
        QVERIFY(sources.helper().protocolOk && sources.helper().usb);
        QVERIFY(!sources.udevRulePresent());
        QFile rule(config.udevRuleFile);
        QVERIFY(rule.open(QIODevice::WriteOnly));
        rule.close();
        QVERIFY(sources.udevRulePresent());

        const FolderInfo folder = sources.backupFolder();
        QVERIFY(folder.writable);
        QVERIFY(!folder.onIpod);

        const IpodFacts facts = sources.ipod();
        QVERIFY(facts.helperRead);
        QVERIFY(facts.modelSupported && facts.methodAgrees);
        QCOMPARE(facts.serial, QStringLiteral("S1"));
        QCOMPARE(facts.otherIpods, 1);
        QVERIFY(facts.volumeReady);
        QCOMPARE(facts.filesystem, Filesystem::Fat32);
        QCOMPARE(facts.bootloaderState, QStringLiteral("clean"));
        QVERIFY(facts.firmwarePartitionOk);

        // The room: the used space (clean), the files (keep), the partition, the margin.
        QVERIFY(sources.backupNeedBytes(InstallPath::Keep) >= 8192 + kBackupMargin);
        QVERIFY(sources.backupNeedBytes(InstallPath::Clean) >= 8192 + kBackupMargin);

        // No package: the check says why.
        const PackageCheck package = sources.package(dir.filePath(QStringLiteral("none.json")));
        QVERIFY(!package.code.isEmpty());

        // A read that fails.
        commands.answers.clear();
        const IpodFacts failed = sources.ipod();
        QVERIFY(!failed.helperRead);
        QCOMPARE(failed.readError, QStringLiteral("unavailable"));
    }

    // A volume that is a folder on another disk, as in the tests: the system
    // figures are those of that disk, so the sizes come from detection.
    void aVolumeFolderTakesTheSizesOfDetection()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("volume")));
        device::DetectedIpod ipod = detected(QStringLiteral("S1"), QStringLiteral("1-2"),
                                             dir.filePath(QStringLiteral("volume")));
        ipod.volume->totalBytes = 4000000;
        ipod.volume->freeBytes = 3000000;
        IpodView view(QStringLiteral("S1"));
        view.seen(ipod);

        helper::HelperClient client;
        FakeCommands commands;
        LinuxDiagnoseSources sources(
            configFor(Family::Dfu, QStringLiteral("S1"), dir.filePath(QStringLiteral("backups"))),
            view, commands, client);

        const IpodFacts facts = sources.ipod();
        QCOMPARE(facts.volumeSize, qint64(4000000));
        QCOMPARE(facts.volumeFree, qint64(3000000));
        QCOMPARE(sources.backupNeedBytes(InstallPath::Clean), qint64(1000000) + kBackupMargin);
    }

    void classicReadsTheAppleVersion()
    {
        helper::HelperClient client;
        FakeCommands commands;
        commands.answers[QStringLiteral("read-state")] = {{}, {}, {{"method", "dfu"}}};
        commands.answers[QStringLiteral("read-device-sheet")] = {
            {}, {}, {{"text", "<key>VisibleBuildID</key><string>2.0.1</string>"}}};
        IpodView view;
        view.seen(detected(QStringLiteral("C1"), QStringLiteral("1-4"), QString()));
        LinuxDiagnoseSources sources(configFor(Family::Dfu, QString(), QString()), view, commands,
                                     client);
        const IpodFacts facts = sources.ipod();
        QVERIFY(facts.helperRead && facts.methodAgrees);
        QCOMPARE(facts.appleVersion, QStringLiteral("2.0.1"));
    }

    void stepSourcesAndFactory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString volume = dir.filePath(QStringLiteral("volume"));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("volume")));
        const QString backups = dir.filePath(QStringLiteral("backups"));

        // The newest complete settings backup of the serial is the one offered.
        QStringList calls;
        for (const QString &date :
             {QStringLiteral("2026-01-01_10-00-00"), QStringLiteral("2026-02-01_10-00-00")}) {
            const QString folder = backups + QLatin1Char('/') +
                                   serialFolderName(QStringLiteral("S1")) + QLatin1Char('/') + date;
            QVERIFY(QDir().mkpath(folder));
            BackupManifest manifest;
            manifest.kind = BackupKind::Settings;
            manifest.serial = QStringLiteral("S1");
            manifest.created = QDateTime::fromString(date, QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
            QFile file(folder + QLatin1Char('/') + QString(kManifestFile));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(manifestJson(manifest));
        }

        helper::HelperClient client;
        auto backend = std::make_unique<FakeService>();
        backend->calls = &calls;
        LinuxSourcesConfig config = configFor(Family::Partition, QStringLiteral("S1"), backups);
        config.volumeReturnMs = 5;
        config.returnMs = 7;
        LinuxStepFactory factory(config, client, QJsonObject(), QString(), std::move(backend), [] {
            return QList<device::LinuxDisk>{ipodDisk(QStringLiteral("1-2"))};
        });
        factory.view().seen(detected(QStringLiteral("S1"), QStringLiteral("1-2"), volume));
        factory.mounter().seen(detected(QStringLiteral("S1"), QStringLiteral("1-2"), volume));

        LinuxStepSources &sources = factory.stepSources();
        QCOMPARE(sources.volumeRoot(), volume);
        QCOMPARE(sources.volumeLabel(), QStringLiteral("IPOD"));
        QCOMPARE(sources.target(), QStringLiteral("ipodvideo"));
        QCOMPARE(sources.modelName(), QStringLiteral("iPod Video"));
        QCOMPARE(sources.backupRoot(), backups);
        QVERIFY(sources.freeBytes() > 0);
        QVERIFY(sources.freeBytes(dir.path()) > 0);
        QVERIFY(sources.firmwarePartition() == &factory.partition());
        QCOMPARE(sources.volumeReturnMs(), qint64(5));
        QCOMPARE(sources.returnMs(), qint64(7));
        QVERIFY(sources.settingsBackup().endsWith(QStringLiteral("2026-02-01_10-00-00")));

        // The volume watch asks the disk service through the mounter.
        factory.volumes().mount(QStringLiteral("S1"));
        QCOMPARE(calls, QStringList{QStringLiteral("mount sdb2")});

        // Every name of section 2.3 has a step, and an unknown one has none.
        const QStringList names = {"diagnose",
                                   "backup-keep",
                                   "backup-full",
                                   "backup-settings",
                                   "disk-mode",
                                   "format",
                                   "convert",
                                   "firmware",
                                   "bootloader",
                                   "classic-bootloader",
                                   "remove-bootloader",
                                   "classic-remove",
                                   "remove-firmware",
                                   "verify",
                                   "finish"};
        const StepFactory make = factory.factory();
        for (const QString &name : names)
            QVERIFY2(make(name) != nullptr, qPrintable(name));
        QVERIFY(make(QStringLiteral("nothing")) == nullptr);

        // Every name that stepsFor() gives, for every shape, is known.
        for (Family family : {Family::Partition, Family::Dfu}) {
            for (FlowKind kind : {FlowKind::Install, FlowKind::Uninstall}) {
                for (InstallPath path :
                     {InstallPath::None, InstallPath::Clean, InstallPath::Keep}) {
                    for (bool mac : {false, true}) {
                        for (const QString &name : stepsFor({family, kind, path, mac}))
                            QVERIFY2(make(name) != nullptr, qPrintable(name));
                    }
                }
            }
        }
    }

    void classicHasNoFirmwarePartition()
    {
        helper::HelperClient client;
        LinuxStepFactory factory(configFor(Family::Dfu, QString(), QString()), client,
                                 QJsonObject(), QString(), std::make_unique<FakeService>());
        QVERIFY(factory.stepSources().firmwarePartition() == nullptr);
        QCOMPARE(factory.stepSources().firmwarePartitionBytes(), qint64(0));
        QCOMPARE(factory.stepSources().returnMs(), kRestartWindowMs);
    }
};

QTEST_GUILESS_MAIN(TestWizardSources)
#include "tst_wizard_sources.moc"
