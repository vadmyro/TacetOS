// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The Linux source of readings (docs/specs/detection.md sections 3, 5, 6, 7).
// The USB folders come from the fixtures; the block tree, the mounts and the
// music volumes are built in a temporary folder. The real /sys and
// /proc/self/mountinfo are never read.

#include "constants.h"
#include "linuxsource.h"
#include "live.h"
#include "models.h"
#include "symlinks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::device;

namespace {

const QString kFixtures{QStringLiteral(TACETOS_DEVICE_FIXTURES)};

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(content.toUtf8());
}

class FixedClock : public Clock
{
public:
    qint64 nowMs() const override { return now; }
    qint64 now = 0;
};

} // namespace

class TstDeviceLinuxSource : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void usbListNormal();
    void usbListDfu();
    void usbListUnreadable();
    void portWithDiskVolumeAndMarkers();
    void portNotMounted();
    void portUnsupportedFilesystemIsNotRead();
    void portNoDiskYet();
    void portUnreadableBlockRoot();
    void portMissingMountinfo();
    void diskReadingIsNotMade();
    void feedsTheTracker();

private:
    // A disk `name` under the USB folder `port`, partitions 1 and 2, with the
    // links of the fake /sys/class/block.
    void makeIpodDisk(const QString &port, const QString &name, int major);
    // Makes the music volume of the folder `name` and returns its path.
    QString makeVolume(const QString &name);
    void writeMountinfo(const QString &lines);
    LinuxSourcePaths paths(const QString &usbRoot = QString()) const;
    UsbDevice classicOn(const QString &port) const;

    QList<Model> models_;
    QTemporaryDir dir_;
    QString sys_;
};

void TstDeviceLinuxSource::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    models_ = list.models;
    QVERIFY(dir_.isValid());
}

void TstDeviceLinuxSource::init()
{
    // A new tree for each test.
    QDir(dir_.path()).removeRecursively();
    QDir().mkpath(dir_.path());
    sys_ = dir_.path() + QStringLiteral("/sys");
    QDir().mkpath(sys_ + QStringLiteral("/class/block"));
    writeMountinfo(QString());
}

void TstDeviceLinuxSource::makeIpodDisk(const QString &port, const QString &name, int major)
{
    const QString usb = sys_ + QStringLiteral("/devices/pci0000:00/0000:00:14.0/usb1/") + port;
    writeFile(usb + QStringLiteral("/idVendor"), QStringLiteral("05ac\n"));
    writeFile(usb + QStringLiteral("/idProduct"), QStringLiteral("1261\n"));
    const QString disk = usb + QStringLiteral("/1-2:1.0/host6/target6:0:0/6:0:0:0/block/") + name;
    writeFile(disk + QStringLiteral("/size"), QStringLiteral("62500000\n"));
    writeFile(disk + QStringLiteral("/queue/logical_block_size"), QStringLiteral("512\n"));
    writeFile(disk + QStringLiteral("/dev"), QStringLiteral("%1:16\n").arg(major));
    QVERIFY(QFile::link(disk, sys_ + QStringLiteral("/class/block/") + name));
    for (int number = 1; number <= 2; ++number) {
        const QString part = disk + QLatin1Char('/') + name + QString::number(number);
        writeFile(part + QStringLiteral("/partition"), QString::number(number) + QLatin1Char('\n'));
        writeFile(part + QStringLiteral("/dev"),
                  QStringLiteral("%1:%2\n").arg(major).arg(16 + number));
    }
}

QString TstDeviceLinuxSource::makeVolume(const QString &name)
{
    const QString root = dir_.path() + QStringLiteral("/media/") + name;
    QDir().mkpath(root + QStringLiteral("/") + QString(kAppleRootFolder));
    return root;
}

void TstDeviceLinuxSource::writeMountinfo(const QString &lines)
{
    writeFile(dir_.path() + QStringLiteral("/mountinfo"), lines);
}

LinuxSourcePaths TstDeviceLinuxSource::paths(const QString &usbRoot) const
{
    LinuxSourcePaths p;
    p.usbDevices = usbRoot.isEmpty() ? kFixtures + QStringLiteral("/usb-linux/normal") : usbRoot;
    p.classBlock = sys_ + QStringLiteral("/class/block");
    p.mountinfo = dir_.path() + QStringLiteral("/mountinfo");
    return p;
}

UsbDevice TstDeviceLinuxSource::classicOn(const QString &port) const
{
    UsbDevice device;
    device.usbId = QStringLiteral("05ac:1261");
    device.mode = UsbMode::Normal;
    device.portPath = port;
    return device;
}

void TstDeviceLinuxSource::usbListNormal()
{
    LinuxDeviceSource source(models_, paths());
    const auto list = source.readUsb();
    QVERIFY(list.has_value());
    // The host controller and the other vendor are dropped; the iPod stays.
    QCOMPARE(list->size(), 1);
    QCOMPARE(list->at(0).usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(list->at(0).mode, UsbMode::Normal);
    QCOMPARE(list->at(0).portPath, QStringLiteral("1-2"));
    QCOMPARE(list->at(0).serial, QStringLiteral("000A2700F1E2D3C4"));
    QVERIFY(list->at(0).candidates.contains(QStringLiteral("classic-6g")));
}

void TstDeviceLinuxSource::usbListDfu()
{
    LinuxDeviceSource source(models_, paths(kFixtures + QStringLiteral("/usb-linux/dfu")));
    const auto list = source.readUsb();
    QVERIFY(list.has_value());
    QCOMPARE(list->size(), 1);
    QCOMPARE(list->at(0).mode, UsbMode::Dfu);
}

void TstDeviceLinuxSource::usbListUnreadable()
{
    // An unreadable list is not an empty list: the screen must keep its state.
    LinuxDeviceSource source(models_, paths(dir_.path() + QStringLiteral("/nothing-here")));
    QVERIFY(!source.readUsb().has_value());
}

void TstDeviceLinuxSource::portWithDiskVolumeAndMarkers()
{
    TACETOS_NEEDS_SYMLINKS();
    makeIpodDisk(QStringLiteral("1-2"), QStringLiteral("sdb"), 8);
    const QString volume = makeVolume(QStringLiteral("IPOD"));
    writeMountinfo(
        QStringLiteral("120 29 8:18 / %1 rw,relatime shared:60 - vfat /dev/sdb2 rw\n").arg(volume));

    LinuxDeviceSource source(models_, paths());
    source.setReadVolumeSpace(false);
    const PortReading reading = source.readPort(classicOn(QStringLiteral("1-2")));

    QVERIFY(reading.disk.has_value());
    QCOMPARE(reading.disk->node, QStringLiteral("/dev/sdb"));
    QCOMPARE(reading.disk->sizeBytes, qint64(62500000) * 512);
    QCOMPARE(reading.disk->sectorSize, 512);
    QVERIFY(reading.volume.has_value());
    QCOMPARE(reading.volume->mountPoint, volume);
    QCOMPARE(reading.volume->filesystem, QStringLiteral("vfat"));
    QCOMPARE(reading.volume->totalBytes, qint64(0));
    // The markers were read from the volume.
    QVERIFY(reading.markers.appleFolder);
    QVERIFY(!reading.markers.appleDatabase);
}

void TstDeviceLinuxSource::portNotMounted()
{
    TACETOS_NEEDS_SYMLINKS();
    makeIpodDisk(QStringLiteral("1-2"), QStringLiteral("sdb"), 8);
    LinuxDeviceSource source(models_, paths());
    const PortReading reading = source.readPort(classicOn(QStringLiteral("1-2")));
    QVERIFY(reading.disk.has_value());
    QVERIFY(!reading.volume.has_value());
    QVERIFY(!reading.markers.appleFolder);
}

void TstDeviceLinuxSource::portUnsupportedFilesystemIsNotRead()
{
    TACETOS_NEEDS_SYMLINKS();
    makeIpodDisk(QStringLiteral("1-2"), QStringLiteral("sdb"), 8);
    const QString volume = makeVolume(QStringLiteral("EXFAT"));
    writeMountinfo(
        QStringLiteral("150 29 8:18 / %1 rw,relatime - exfat /dev/sdb2 rw\n").arg(volume));

    LinuxDeviceSource source(models_, paths());
    source.setReadVolumeSpace(false);
    const PortReading reading = source.readPort(classicOn(QStringLiteral("1-2")));
    // Kept in the result, but nothing is read from it (section 6.2).
    QVERIFY(reading.volume.has_value());
    QCOMPARE(reading.volume->filesystem, QStringLiteral("exfat"));
    QVERIFY(!reading.markers.appleFolder);
}

void TstDeviceLinuxSource::portNoDiskYet()
{
    TACETOS_NEEDS_SYMLINKS();
    // The iPod is on 1-2 but the block tree has a disk only for another port.
    makeIpodDisk(QStringLiteral("1-7"), QStringLiteral("sdc"), 8);
    LinuxDeviceSource source(models_, paths());
    const PortReading reading = source.readPort(classicOn(QStringLiteral("1-2")));
    QVERIFY(!reading.disk.has_value());
    QVERIFY(!reading.volume.has_value());
}

void TstDeviceLinuxSource::portUnreadableBlockRoot()
{
    LinuxSourcePaths p = paths();
    p.classBlock = dir_.path() + QStringLiteral("/nothing-here");
    LinuxDeviceSource source(models_, p);
    const PortReading reading = source.readPort(classicOn(QStringLiteral("1-2")));
    QVERIFY(!reading.disk.has_value());
    QVERIFY(!reading.volume.has_value());
}

void TstDeviceLinuxSource::portMissingMountinfo()
{
    TACETOS_NEEDS_SYMLINKS();
    makeIpodDisk(QStringLiteral("1-2"), QStringLiteral("sdb"), 8);
    LinuxSourcePaths p = paths();
    p.mountinfo = dir_.path() + QStringLiteral("/no-such-mountinfo");
    LinuxDeviceSource source(models_, p);
    const PortReading reading = source.readPort(classicOn(QStringLiteral("1-2")));
    QVERIFY(reading.disk.has_value());
    QVERIFY(!reading.volume.has_value());
}

void TstDeviceLinuxSource::diskReadingIsNotMade()
{
    TACETOS_NEEDS_SYMLINKS();
    makeIpodDisk(QStringLiteral("1-2"), QStringLiteral("sdb"), 8);
    LinuxDeviceSource source(models_, paths());
    const PortReading reading = source.readPort(classicOn(QStringLiteral("1-2")));
    QCOMPARE(reading.diskReading.status, DiskReadStatus::NotRead);
}

// The source behind the tracker: one refresh gives a ready iPod, and a second
// one after the unmount shows the volume gone.
void TstDeviceLinuxSource::feedsTheTracker()
{
    TACETOS_NEEDS_SYMLINKS();
    makeIpodDisk(QStringLiteral("1-2"), QStringLiteral("sdb"), 8);
    const QString volume = makeVolume(QStringLiteral("IPOD"));
    writeMountinfo(
        QStringLiteral("120 29 8:18 / %1 rw,relatime - vfat /dev/sdb2 rw\n").arg(volume));

    LinuxDeviceSource source(models_, paths());
    source.setReadVolumeSpace(false);
    FixedClock clock;
    LiveTracker tracker(models_, &clock, &source);
    tracker.refresh();

    QCOMPARE(tracker.ipods().size(), 1);
    QCOMPARE(tracker.ipods().at(0).portPath, QStringLiteral("1-2"));
    QCOMPARE(tracker.ipods().at(0).model, QStringLiteral("classic-6g"));
    QCOMPARE(tracker.ipods().at(0).state, IpodState::Ready);

    // Unmounted: the volume is gone at once, and once the wait is over the
    // state says there is no volume.
    writeMountinfo(QString());
    tracker.refresh();
    clock.now = kDiskWaitMs + 1;
    tracker.tick();
    QCOMPARE(tracker.ipods().size(), 1);
    QVERIFY(!tracker.ipods().at(0).volume.has_value());
    QCOMPARE(tracker.ipods().at(0).state, IpodState::NoVolume);
}

QTEST_GUILESS_MAIN(TstDeviceLinuxSource)
#include "tst_device_linuxsource.moc"
