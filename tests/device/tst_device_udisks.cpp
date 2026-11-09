// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Mount, unmount and eject through the disk service (udisks.h). The service is
// replaced by one that answers from memory and writes down the calls it gets,
// so nothing touches a real disk.

#include "udisks.h"

#include <QTest>

using namespace tacetos::device;

namespace {

const QString kErr = QStringLiteral("org.freedesktop.UDisks2.Error.");

QVariantMap blockProps(const QString &node, int major, int minor, const QString &drive,
                       const QString &type)
{
    QVariantMap props;
    props[QStringLiteral("Device")] = node.toUtf8() + '\0';
    props[QStringLiteral("DeviceNumber")] = qulonglong(makeDeviceNumber(major, minor));
    props[QStringLiteral("Drive")] = drive;
    props[QStringLiteral("IdType")] = type;
    props[QStringLiteral("Size")] = qulonglong(1000);
    return props;
}

class FakeService : public UdisksBackend
{
public:
    // Disk sdb of an older iPod: slot 1 is the firmware (no filesystem), slot 2
    // is FAT32. A USB stick sits at sdc.
    FakeService()
    {
        const QString drive = QStringLiteral("/org/freedesktop/UDisks2/drives/iPod");
        const QString stick = QStringLiteral("/org/freedesktop/UDisks2/drives/Stick");
        objects[drive][QStringLiteral("org.freedesktop.UDisks2.Drive")] = {
            {"Vendor", "Apple"},  {"Model", "iPod"},     {"ConnectionBus", "usb"},
            {"Ejectable", false}, {"CanPowerOff", true},
        };
        objects[stick][QStringLiteral("org.freedesktop.UDisks2.Drive")] = {
            {"ConnectionBus", "usb"},
            {"Ejectable", true},
            {"CanPowerOff", true},
        };
        addBlock(QStringLiteral("sdb"), blockProps("/dev/sdb", 8, 16, drive, QString()));
        addBlock(QStringLiteral("sdb1"), blockProps("/dev/sdb1", 8, 17, drive, QString()), 1);
        addBlock(QStringLiteral("sdb2"), blockProps("/dev/sdb2", 8, 18, drive, "vfat"), 2, true);
        addBlock(QStringLiteral("sdc"), blockProps("/dev/sdc", 8, 32, stick, "vfat"), 0, true);
    }

    void addBlock(const QString &name, const QVariantMap &props, int number = 0,
                  bool filesystem = false)
    {
        const QString path = QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + name;
        objects[path][QStringLiteral("org.freedesktop.UDisks2.Block")] = props;
        if (number > 0)
            objects[path][QStringLiteral("org.freedesktop.UDisks2.Partition")] = {
                {"Number", number}};
        if (filesystem)
            objects[path][QStringLiteral("org.freedesktop.UDisks2.Filesystem")] = {
                {"MountPoints", QVariant::fromValue(QByteArrayList())}};
    }

    QString path(const QString &name) const
    {
        return QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + name;
    }

    void setMounted(const QString &name, const QString &where)
    {
        QVariantMap &fs = objects[path(name)][QStringLiteral("org.freedesktop.UDisks2.Filesystem")];
        fs["MountPoints"] = where.isEmpty()
                                ? QVariant::fromValue(QByteArrayList())
                                : QVariant::fromValue(QByteArrayList{where.toUtf8() + '\0'});
    }

    std::optional<UdisksObjects> managedObjects(UdisksReply *error) override
    {
        if (down) {
            if (error)
                *error = UdisksReply{"org.freedesktop.DBus.Error.ServiceUnknown", "gone", {}};
            return std::nullopt;
        }
        return objects;
    }

    UdisksReply mount(const QString &blockPath, bool readOnly) override
    {
        calls << QStringLiteral("mount %1 %2").arg(blockPath).arg(readOnly ? "ro" : "rw");
        if (!refuse.isEmpty())
            return UdisksReply{refuse, "refused", {}};
        setMounted(blockPath.section('/', -1), QStringLiteral("/run/media/u/IPOD"));
        return UdisksReply{{}, {}, QStringLiteral("/run/media/u/IPOD")};
    }

    UdisksReply unmount(const QString &blockPath) override
    {
        calls << QStringLiteral("unmount %1").arg(blockPath);
        if (!refuse.isEmpty())
            return UdisksReply{refuse, "refused", {}};
        setMounted(blockPath.section('/', -1), QString());
        return UdisksReply();
    }

    UdisksReply eject(const QString &drivePath) override
    {
        calls << QStringLiteral("eject %1").arg(drivePath);
        return UdisksReply();
    }

    UdisksReply powerOff(const QString &drivePath) override
    {
        calls << QStringLiteral("poweroff %1").arg(drivePath);
        return UdisksReply();
    }

    UdisksObjects objects;
    QStringList calls;
    bool down = false;
    // When set, mount and unmount answer with this error.
    QString refuse;
};

LinuxDisk ipodDisk()
{
    LinuxDisk disk;
    disk.name = QStringLiteral("sdb");
    disk.node = QStringLiteral("/dev/sdb");
    disk.partitions = {{QStringLiteral("sdb1"), 1, 8, 17}, {QStringLiteral("sdb2"), 2, 8, 18}};
    return disk;
}

struct Rig
{
    Rig()
    {
        auto owned = std::make_unique<FakeService>();
        service = owned.get();
        control = std::make_unique<DiskControl>(std::move(owned));
    }

    FakeService *service = nullptr;
    std::unique_ptr<DiskControl> control;
};

} // namespace

class TestUdisks : public QObject
{
    Q_OBJECT

private slots:
    void deviceNumber();
    void parse();
    void errorNames();
    void mountPicksTheMusicSlot();
    void mountReadOnly();
    void mountAlreadyMounted();
    void mountRaceAlreadyMounted();
    void mountNoVolume();
    void mountRefused();
    void unmountAll();
    void unmountNothingMounted();
    void unmountBusy();
    void ejectPowersOff();
    void ejectEjectsWhenNoPowerOff();
    void ejectStopsWhenBusy();
    void serviceMissing();
    void unknownDisk();
    void otherBusIsRefused();
    void otherDriveIsNeverTouched();
};

void TestUdisks::deviceNumber()
{
    QCOMPARE(makeDeviceNumber(8, 17), quint64(0x811));
    // Large numbers use the high bits, like dev_t.
    QCOMPARE(makeDeviceNumber(259, 300), quint64((259u << 8) | 44u | (256u << 12)));
}

void TestUdisks::parse()
{
    FakeService service;
    service.setMounted(QStringLiteral("sdb2"), QStringLiteral("/run/media/u/My iPod"));
    const UdisksSnapshot snapshot = parseUdisksObjects(service.objects);
    QCOMPARE(snapshot.blocks.size(), 4);
    QCOMPARE(snapshot.drives.size(), 2);
    for (const UdisksBlock &block : snapshot.blocks) {
        if (block.node != QLatin1String("/dev/sdb2"))
            continue;
        QCOMPARE(block.partitionNumber, 2);
        QCOMPARE(block.filesystem, QStringLiteral("vfat"));
        QVERIFY(block.hasFilesystem);
        QCOMPARE(block.mountPoints, QStringList{QStringLiteral("/run/media/u/My iPod")});
        QCOMPARE(block.deviceNumber, makeDeviceNumber(8, 18));
    }
    for (const UdisksDrive &drive : snapshot.drives) {
        if (drive.vendor == QLatin1String("Apple")) {
            QCOMPARE(drive.connectionBus, QStringLiteral("usb"));
            QVERIFY(drive.canPowerOff);
            QVERIFY(!drive.ejectable);
        }
    }
}

void TestUdisks::errorNames()
{
    QCOMPARE(diskErrorFromName(QString()), DiskError::None);
    QCOMPARE(diskErrorFromName(kErr + "NotAuthorizedCanObtain"), DiskError::NotAuthorized);
    QCOMPARE(diskErrorFromName(kErr + "NotAuthorizedDismissed"), DiskError::NotAuthorized);
    QCOMPARE(diskErrorFromName(kErr + "DeviceBusy"), DiskError::Busy);
    QCOMPARE(diskErrorFromName(kErr + "Failed"), DiskError::Failed);
    QCOMPARE(diskErrorFromName(QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown")),
             DiskError::ServiceMissing);
    QCOMPARE(diskErrorFromName(QStringLiteral("something.else")), DiskError::Failed);
    QVERIFY(diskErrorMessage(DiskError::None).isEmpty());
    QVERIFY(!diskErrorMessage(DiskError::Busy).isEmpty());
}

void TestUdisks::mountPicksTheMusicSlot()
{
    Rig rig;
    const DiskResult result = rig.control->mount(ipodDisk());
    QVERIFY(result.ok());
    QVERIFY(!result.alreadyMounted);
    QCOMPARE(result.mountPoint, QStringLiteral("/run/media/u/IPOD"));
    // Slot 1 has no filesystem and the stick is not ours: only sdb2 is asked.
    QCOMPARE(rig.service->calls,
             QStringList{QStringLiteral("mount %1 rw").arg(rig.service->path("sdb2"))});
}

void TestUdisks::mountReadOnly()
{
    Rig rig;
    QVERIFY(rig.control->mount(ipodDisk(), true).ok());
    QVERIFY(rig.service->calls.first().endsWith(QLatin1String(" ro")));
}

void TestUdisks::mountAlreadyMounted()
{
    Rig rig;
    rig.service->setMounted(QStringLiteral("sdb2"), QStringLiteral("/mnt/ipod"));
    const DiskResult result = rig.control->mount(ipodDisk());
    QVERIFY(result.ok());
    QVERIFY(result.alreadyMounted);
    QCOMPARE(result.mountPoint, QStringLiteral("/mnt/ipod"));
    QVERIFY(rig.service->calls.isEmpty());
}

void TestUdisks::mountRaceAlreadyMounted()
{
    // The service says "already mounted" for a volume that looked free.
    struct Racing : FakeService
    {
        UdisksReply mount(const QString &blockPath, bool) override
        {
            setMounted(blockPath.section('/', -1), QStringLiteral("/mnt/other"));
            return UdisksReply{"org.freedesktop.UDisks2.Error.AlreadyMounted", "already", {}};
        }
    };
    auto owned = std::make_unique<Racing>();
    DiskControl control(std::move(owned));
    const DiskResult result = control.mount(ipodDisk());
    QVERIFY(result.ok());
    QVERIFY(result.alreadyMounted);
    QCOMPARE(result.mountPoint, QStringLiteral("/mnt/other"));
}

void TestUdisks::mountNoVolume()
{
    Rig rig;
    // Only the firmware slot: nothing to mount.
    LinuxDisk disk = ipodDisk();
    disk.partitions = {{QStringLiteral("sdb1"), 1, 8, 17}};
    rig.service->objects.remove(rig.service->path(QStringLiteral("sdb2")));
    const DiskResult result = rig.control->mount(disk);
    QCOMPARE(result.error, DiskError::NoVolume);
    QVERIFY(rig.service->calls.isEmpty());

    // A filesystem that TacetOS does not know is not mounted either.
    Rig other;
    other.service->objects[other.service->path(QStringLiteral("sdb2"))]
                          [QStringLiteral("org.freedesktop.UDisks2.Block")]["IdType"] =
        QStringLiteral("ext4");
    QCOMPARE(other.control->mount(ipodDisk()).error, DiskError::NoVolume);
    QVERIFY(other.service->calls.isEmpty());
}

void TestUdisks::mountRefused()
{
    Rig rig;
    rig.service->refuse = kErr + "NotAuthorizedCanObtain";
    const DiskResult result = rig.control->mount(ipodDisk());
    QCOMPARE(result.error, DiskError::NotAuthorized);
    QCOMPARE(result.detail, QStringLiteral("refused"));
    QVERIFY(result.mountPoint.isEmpty());
}

void TestUdisks::unmountAll()
{
    Rig rig;
    rig.service->setMounted(QStringLiteral("sdb2"), QStringLiteral("/mnt/ipod"));
    const DiskResult result = rig.control->unmount(ipodDisk());
    QVERIFY(result.ok());
    QCOMPARE(result.unmounted, 1);
    QCOMPARE(rig.service->calls,
             QStringList{QStringLiteral("unmount %1").arg(rig.service->path("sdb2"))});
}

void TestUdisks::unmountNothingMounted()
{
    Rig rig;
    const DiskResult result = rig.control->unmount(ipodDisk());
    QVERIFY(result.ok());
    QCOMPARE(result.unmounted, 0);
    QVERIFY(rig.service->calls.isEmpty());
}

void TestUdisks::unmountBusy()
{
    Rig rig;
    rig.service->setMounted(QStringLiteral("sdb2"), QStringLiteral("/mnt/ipod"));
    rig.service->refuse = kErr + "DeviceBusy";
    const DiskResult result = rig.control->unmount(ipodDisk());
    QCOMPARE(result.error, DiskError::Busy);
    QCOMPARE(result.unmounted, 0);
}

void TestUdisks::ejectPowersOff()
{
    Rig rig;
    rig.service->setMounted(QStringLiteral("sdb2"), QStringLiteral("/mnt/ipod"));
    const DiskResult result = rig.control->eject(ipodDisk());
    QVERIFY(result.ok());
    QVERIFY(result.released);
    QCOMPARE(result.unmounted, 1);
    QCOMPARE(rig.service->calls,
             (QStringList{QStringLiteral("unmount %1").arg(rig.service->path("sdb2")),
                          QStringLiteral("poweroff /org/freedesktop/UDisks2/drives/iPod")}));
}

void TestUdisks::ejectEjectsWhenNoPowerOff()
{
    Rig rig;
    QVariantMap &drive =
        rig.service->objects[QStringLiteral("/org/freedesktop/UDisks2/drives/iPod")]
                            [QStringLiteral("org.freedesktop.UDisks2.Drive")];
    drive["CanPowerOff"] = false;
    drive["Ejectable"] = true;
    const DiskResult result = rig.control->eject(ipodDisk());
    QVERIFY(result.released);
    QCOMPARE(rig.service->calls,
             QStringList{QStringLiteral("eject /org/freedesktop/UDisks2/drives/iPod")});

    // Neither: the volumes are unmounted and that is all there is to do.
    Rig bare;
    QVariantMap &none =
        bare.service->objects[QStringLiteral("/org/freedesktop/UDisks2/drives/iPod")]
                             [QStringLiteral("org.freedesktop.UDisks2.Drive")];
    none["CanPowerOff"] = false;
    const DiskResult plain = bare.control->eject(ipodDisk());
    QVERIFY(plain.ok());
    QVERIFY(!plain.released);
    QVERIFY(bare.service->calls.isEmpty());
}

void TestUdisks::ejectStopsWhenBusy()
{
    Rig rig;
    rig.service->setMounted(QStringLiteral("sdb2"), QStringLiteral("/mnt/ipod"));
    rig.service->refuse = kErr + "DeviceBusy";
    const DiskResult result = rig.control->eject(ipodDisk());
    QCOMPARE(result.error, DiskError::Busy);
    QVERIFY(!result.released);
    // The drive is never powered off under a volume that stayed.
    QCOMPARE(rig.service->calls.size(), 1);
}

void TestUdisks::serviceMissing()
{
    Rig rig;
    rig.service->down = true;
    QCOMPARE(rig.control->mount(ipodDisk()).error, DiskError::ServiceMissing);
    QCOMPARE(rig.control->unmount(ipodDisk()).error, DiskError::ServiceMissing);
    QCOMPARE(rig.control->eject(ipodDisk()).error, DiskError::ServiceMissing);
}

void TestUdisks::unknownDisk()
{
    Rig rig;
    LinuxDisk disk = ipodDisk();
    disk.node = QStringLiteral("/dev/sdz");
    QCOMPARE(rig.control->eject(disk).error, DiskError::NotAnIpodDisk);
    QVERIFY(rig.service->calls.isEmpty());
}

void TestUdisks::otherBusIsRefused()
{
    Rig rig;
    rig.service->objects[QStringLiteral("/org/freedesktop/UDisks2/drives/iPod")]
                        [QStringLiteral("org.freedesktop.UDisks2.Drive")]["ConnectionBus"] =
        QStringLiteral("ata");
    QCOMPARE(rig.control->mount(ipodDisk()).error, DiskError::NotAnIpodDisk);
    QCOMPARE(rig.control->eject(ipodDisk()).error, DiskError::NotAnIpodDisk);
    QVERIFY(rig.service->calls.isEmpty());
}

void TestUdisks::otherDriveIsNeverTouched()
{
    // A partition that carries the right name but belongs to another drive is
    // not ours: here sdb2 is moved to the stick.
    Rig rig;
    rig.service->objects[rig.service->path(QStringLiteral("sdb2"))]
                        [QStringLiteral("org.freedesktop.UDisks2.Block")]["Drive"] =
        QStringLiteral("/org/freedesktop/UDisks2/drives/Stick");
    QCOMPARE(rig.control->mount(ipodDisk()).error, DiskError::NoVolume);
    QVERIFY(rig.service->calls.isEmpty());
}

QTEST_GUILESS_MAIN(TestUdisks)
#include "tst_device_udisks.moc"
