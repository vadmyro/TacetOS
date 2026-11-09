// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// From the USB device to the disk, and from the disk to the volume, on Linux
// (docs/specs/detection.md sections 5, 6 and 12.2). The sysfs tree is built in
// a temporary folder, with real links, so that no iPod is needed (not on
// Windows, see symlinks.h).

#include "detected.h"
#include "linuxdisks.h"
#include "symlinks.h"
#include "usblist.h"
#include "volume.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::device;

namespace {

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(content.toUtf8());
}

struct Part
{
    QString name;
    int number;
    QString dev;
};

// One USB device folder under the fake /sys/devices, and the path of the folder
// of its disk, as the kernel nests them.
QString usbFolder(const QString &sys, const QString &port, const QString &vendor,
                  const QString &product)
{
    const QString dir = sys + QStringLiteral("/devices/pci0000:00/0000:00:14.0/usb1/") + port;
    writeFile(dir + QStringLiteral("/idVendor"), vendor + QLatin1Char('\n'));
    writeFile(dir + QStringLiteral("/idProduct"), product + QLatin1Char('\n'));
    return dir;
}

// Makes the disk `name` in `diskDir`, with its partitions below it, and the
// links of /sys/class/block.
void makeDisk(const QString &sys, const QString &diskDir, const QString &name, const QString &dev,
              qint64 sectors, const QList<Part> &parts)
{
    writeFile(diskDir + QStringLiteral("/size"), QString::number(sectors) + QLatin1Char('\n'));
    writeFile(diskDir + QStringLiteral("/queue/logical_block_size"), QStringLiteral("512\n"));
    writeFile(diskDir + QStringLiteral("/dev"), dev + QLatin1Char('\n'));
    QDir().mkpath(sys + QStringLiteral("/class/block"));
    QVERIFY(QFile::link(diskDir, sys + QStringLiteral("/class/block/") + name));
    for (const Part &p : parts) {
        const QString dir = diskDir + QLatin1Char('/') + p.name;
        writeFile(dir + QStringLiteral("/partition"),
                  QString::number(p.number) + QLatin1Char('\n'));
        writeFile(dir + QStringLiteral("/dev"), p.dev + QLatin1Char('\n'));
        writeFile(dir + QStringLiteral("/size"), QStringLiteral("1000\n"));
        QVERIFY(QFile::link(dir, sys + QStringLiteral("/class/block/") + p.name));
    }
}

QString blockFolder(const QString &usbDir, const QString &name)
{
    return usbDir + QStringLiteral("/") + name +
           QStringLiteral(":1.0/host6/target6:0:0/6:0:0:0/block/") + name;
}

UsbDevice device(const QString &id, const QString &port)
{
    UsbDevice d;
    d.usbId = id;
    d.mode = UsbMode::Normal;
    d.portPath = port;
    return d;
}

QStringList names(const QList<LinuxDisk> &disks)
{
    QStringList out;
    for (const LinuxDisk &d : disks)
        out << d.name;
    return out;
}

} // namespace

class TestDeviceDisks : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void sysfsKeepsOnlyDisksUnderApple();
    void sysfsDiskFields();
    void sysfsPartitionsAreNumbered();
    void sysfsInternalDiskIsIgnored();
    void sysfsMissingRoot();

    void twoIpodsOfOneModelGetTheirOwnDisk();
    void matchNeedsPortAndId();
    void noDiskYet();

    void mountinfoParsing();
    void mountinfoEscapes();
    void mountinfoSkipsBadLines();
    void volumeFat32();
    void volumeHfsPlus();
    void volumeBySourceLink();
    void volumeBindMountLoses();
    void volumeUnsupportedIsKept();
    void volumeNotMounted();
    void volumeSlotIsThePartitionNumber();
    void notMountedDiskStillThere();
    void readMountinfoFiles();

    void filesystem_data();
    void filesystem();
    void volumeSpace();

private:
    QTemporaryDir m_dir;
    QString m_sys;
    QList<MountEntry> m_mounts;
    QList<LinuxDisk> m_disks;
};

void TestDeviceDisks::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_sys = m_dir.path();

#ifndef Q_OS_WIN
    // An iPod in normal mode on 1-2: sdb, 32 GB, two partitions.
    const QString ipod1 =
        usbFolder(m_sys, QStringLiteral("1-2"), QStringLiteral("05ac"), QStringLiteral("1261"));
    makeDisk(m_sys, blockFolder(ipod1, QStringLiteral("sdb")), QStringLiteral("sdb"),
             QStringLiteral("8:16"), 62500000,
             {{QStringLiteral("sdb1"), 1, QStringLiteral("8:17")},
              {QStringLiteral("sdb2"), 2, QStringLiteral("8:18")}});
    // A second iPod of the same model on 1-4, a Mac one (HFS+).
    const QString ipod2 =
        usbFolder(m_sys, QStringLiteral("1-4"), QStringLiteral("05AC"), QStringLiteral("1261"));
    makeDisk(m_sys, blockFolder(ipod2, QStringLiteral("sdc")), QStringLiteral("sdc"),
             QStringLiteral("8:32"), 31250000,
             {{QStringLiteral("sdc1"), 1, QStringLiteral("8:33")},
              {QStringLiteral("sdc2"), 2, QStringLiteral("8:34")}});
    // A USB stick of another vendor on 1-3.
    const QString stick =
        usbFolder(m_sys, QStringLiteral("1-3"), QStringLiteral("0781"), QStringLiteral("5581"));
    makeDisk(m_sys, blockFolder(stick, QStringLiteral("sdd")), QStringLiteral("sdd"),
             QStringLiteral("8:48"), 1000000,
             {{QStringLiteral("sdd1"), 1, QStringLiteral("8:49")}});
    // An internal disk: no USB device above it.
    makeDisk(m_sys,
             m_sys +
                 QStringLiteral(
                     "/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0:0:0/0:0:0:0/block/sda"),
             QStringLiteral("sda"), QStringLiteral("8:0"), 976773168,
             {{QStringLiteral("sda1"), 1, QStringLiteral("8:1")},
              {QStringLiteral("sda2"), 2, QStringLiteral("8:2")}});
    // An Apple USB device that has no disk (an iPod in DFU, a phone) on 1-5.
    usbFolder(m_sys, QStringLiteral("1-5"), QStringLiteral("05ac"), QStringLiteral("1225"));

    const LinuxDiskList list = readLinuxDisks(m_sys + QStringLiteral("/class/block"));
    QVERIFY(list.error.isEmpty());
    m_disks = list.disks;
#endif

    m_mounts = readMountinfo(QStringLiteral(TACETOS_DEVICE_FIXTURES "/mountinfo-linux.txt"));
    QVERIFY(!m_mounts.isEmpty());
}

void TestDeviceDisks::sysfsKeepsOnlyDisksUnderApple()
{
    TACETOS_NEEDS_SYMLINKS();
    // The stick (other vendor) and the internal disk are never listed.
    QCOMPARE(names(m_disks), (QStringList{QStringLiteral("sdb"), QStringLiteral("sdc")}));
}

void TestDeviceDisks::sysfsDiskFields()
{
    TACETOS_NEEDS_SYMLINKS();
    const LinuxDisk &d = m_disks[0];
    QCOMPARE(d.node, QStringLiteral("/dev/sdb"));
    QCOMPARE(d.usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(d.portPath, QStringLiteral("1-2"));
    QCOMPARE(d.sizeBytes, qint64(62500000) * 512);
    QCOMPARE(d.sectorSize, 512);
    // The vendor id is normalised, as for the USB list.
    QCOMPARE(m_disks[1].usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(m_disks[1].portPath, QStringLiteral("1-4"));
}

void TestDeviceDisks::sysfsPartitionsAreNumbered()
{
    TACETOS_NEEDS_SYMLINKS();
    const LinuxDisk &d = m_disks[0];
    QCOMPARE(d.partitions.size(), 2);
    QCOMPARE(d.partitions[0].name, QStringLiteral("sdb1"));
    QCOMPARE(d.partitions[0].number, 1);
    QCOMPARE(d.partitions[1].name, QStringLiteral("sdb2"));
    QCOMPARE(d.partitions[1].number, 2);
    QCOMPARE(d.partitions[1].major, 8);
    QCOMPARE(d.partitions[1].minor, 18);
}

void TestDeviceDisks::sysfsInternalDiskIsIgnored()
{
    TACETOS_NEEDS_SYMLINKS();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeDisk(dir.path(),
             dir.path() + QStringLiteral("/devices/pci0000:00/0000:00:1d.0/nvme/nvme0/nvme0n1"),
             QStringLiteral("nvme0n1"), QStringLiteral("259:0"), 1000,
             {{QStringLiteral("nvme0n1p1"), 1, QStringLiteral("259:1")}});
    const LinuxDiskList list = readLinuxDisks(dir.path() + QStringLiteral("/class/block"));
    QVERIFY(list.error.isEmpty());
    QVERIFY(list.disks.isEmpty());
}

void TestDeviceDisks::sysfsMissingRoot()
{
    const LinuxDiskList list = readLinuxDisks(m_sys + QStringLiteral("/nothing-here"));
    QVERIFY(list.disks.isEmpty());
    QVERIFY(!list.error.isEmpty());
}

void TestDeviceDisks::twoIpodsOfOneModelGetTheirOwnDisk()
{
    TACETOS_NEEDS_SYMLINKS();
    const LinuxLink a = linkLinuxDevice(device(QStringLiteral("05ac:1261"), QStringLiteral("1-2")),
                                        m_disks, m_mounts);
    const LinuxLink b = linkLinuxDevice(device(QStringLiteral("05ac:1261"), QStringLiteral("1-4")),
                                        m_disks, m_mounts);
    QVERIFY(a.disk && b.disk);
    QCOMPARE(a.disk->node, QStringLiteral("/dev/sdb"));
    QCOMPARE(b.disk->node, QStringLiteral("/dev/sdc"));
    QCOMPARE(a.disk->sizeBytes, qint64(62500000) * 512);
    QCOMPARE(b.disk->sizeBytes, qint64(31250000) * 512);
    QVERIFY(a.volume && b.volume);
    QCOMPARE(a.volume->mountPoint, QStringLiteral("/run/media/user/IPOD"));
    QCOMPARE(b.volume->filesystem, QStringLiteral("hfsplus"));
}

void TestDeviceDisks::matchNeedsPortAndId()
{
    TACETOS_NEEDS_SYMLINKS();
    // Same port, another id: the device changed under us, no disk.
    QVERIFY(!findDiskForUsb(m_disks, device(QStringLiteral("05ac:1209"), QStringLiteral("1-2"))));
    // Same id, no port path: never match on the id alone.
    QVERIFY(!findDiskForUsb(m_disks, device(QStringLiteral("05ac:1261"), QString())));
    QVERIFY(!findDiskForUsb(m_disks, device(QStringLiteral("05ac:1261"), QStringLiteral("1-9"))));
}

void TestDeviceDisks::noDiskYet()
{
    TACETOS_NEEDS_SYMLINKS();
    const LinuxLink link = linkLinuxDevice(
        device(QStringLiteral("05ac:1225"), QStringLiteral("1-5")), m_disks, m_mounts);
    QVERIFY(!link.disk);
    QVERIFY(!link.volume);
}

void TestDeviceDisks::mountinfoParsing()
{
    // 8 good lines out of 9 in the file.
    QCOMPARE(m_mounts.size(), 8);
    const MountEntry &m = m_mounts[2];
    QCOMPARE(m.major, 8);
    QCOMPARE(m.minor, 18);
    QCOMPARE(m.root, QStringLiteral("/Music"));
    QCOMPARE(m.mountPoint, QStringLiteral("/srv/bind"));
    QCOMPARE(m.filesystem, QStringLiteral("vfat"));
    QCOMPARE(m.source, QStringLiteral("/dev/sdb2"));
}

void TestDeviceDisks::mountinfoEscapes()
{
    // Space, backslash and tab, decoded.
    const QList<MountEntry> mounts = parseMountinfo(QStringLiteral(
        "130 29 8:34 / /run/media/user/My\\040iPod\\134x\\011y rw - hfsplus /dev/sdc2 rw\n"));
    QCOMPARE(mounts.size(), 1);
    QCOMPARE(mounts[0].mountPoint, QStringLiteral("/run/media/user/My iPod\\x\ty"));
    // A lone backslash that is no escape is kept.
    const QList<MountEntry> odd =
        parseMountinfo(QStringLiteral("1 2 8:2 / /a\\zz rw - vfat /dev/sdb2 rw\n"));
    QCOMPARE(odd[0].mountPoint, QStringLiteral("/a\\zz"));
}

void TestDeviceDisks::mountinfoSkipsBadLines()
{
    QVERIFY(parseMountinfo(QString()).isEmpty());
    QVERIFY(parseMountinfo(QStringLiteral("nonsense\n\n")).isEmpty());
    // No separator, bad device number, and a separator at the very end.
    QVERIFY(parseMountinfo(QStringLiteral("1 2 8:2 / /a rw vfat /dev/sdb2 rw\n")).isEmpty());
    QVERIFY(parseMountinfo(QStringLiteral("1 2 x:y / /a rw - vfat /dev/sdb2 rw\n")).isEmpty());
    QVERIFY(parseMountinfo(QStringLiteral("1 2 8:2 / /a rw -\n")).isEmpty());
    // No optional field before the separator is fine.
    QCOMPARE(parseMountinfo(QStringLiteral("1 2 8:2 / /a rw - vfat /dev/sdb2 rw\n")).size(), 1);
}

void TestDeviceDisks::volumeFat32()
{
    TACETOS_NEEDS_SYMLINKS();
    const LinuxLink link = linkLinuxDevice(
        device(QStringLiteral("05ac:1261"), QStringLiteral("1-2")), m_disks, m_mounts);
    QVERIFY(link.volume);
    QCOMPARE(link.volume->mountPoint, QStringLiteral("/run/media/user/IPOD"));
    QCOMPARE(classifyFilesystem(link.volume->filesystem), FilesystemKind::Fat32);
}

void TestDeviceDisks::volumeHfsPlus()
{
    TACETOS_NEEDS_SYMLINKS();
    const LinuxLink link = linkLinuxDevice(
        device(QStringLiteral("05ac:1261"), QStringLiteral("1-4")), m_disks, m_mounts);
    QVERIFY(link.volume);
    // The mount point with its escapes decoded.
    QCOMPARE(link.volume->mountPoint, QStringLiteral("/run/media/user/My iPod\\x\ty"));
    QCOMPARE(classifyFilesystem(link.volume->filesystem), FilesystemKind::HfsPlus);
}

void TestDeviceDisks::volumeBySourceLink()
{
    // The source is /dev/disk/by-label/CLASSIC: only the device number links it to sdd2.
    LinuxPartition p;
    p.name = QStringLiteral("sdd2");
    p.number = 2;
    p.major = 8;
    p.minor = 50;
    const std::optional<VolumeInfo> v = findMountedVolume(m_mounts, p);
    QVERIFY(v);
    QCOMPARE(v->mountPoint, QStringLiteral("/run/media/user/CLASSIC"));
}

void TestDeviceDisks::volumeBindMountLoses()
{
    TACETOS_NEEDS_SYMLINKS();
    // sdb2 is mounted on /srv/bind (a folder of it) before its real mount.
    const LinuxLink link = linkLinuxDevice(
        device(QStringLiteral("05ac:1261"), QStringLiteral("1-2")), m_disks, m_mounts);
    QVERIFY(link.volume);
    QCOMPARE(link.volume->mountPoint, QStringLiteral("/run/media/user/IPOD"));
}

void TestDeviceDisks::volumeUnsupportedIsKept()
{
    LinuxDisk disk;
    disk.name = QStringLiteral("sde");
    disk.node = QStringLiteral("/dev/sde");
    disk.usbId = QStringLiteral("05ac:1261");
    disk.portPath = QStringLiteral("1-7");
    disk.partitions = {{QStringLiteral("sde1"), 1, 8, 65}, {QStringLiteral("sde2"), 2, 8, 66}};
    const LinuxLink link = linkLinuxDevice(
        device(QStringLiteral("05ac:1261"), QStringLiteral("1-7")), {disk}, m_mounts);
    QVERIFY(link.volume);
    QCOMPARE(link.volume->filesystem, QStringLiteral("exfat"));
    QCOMPARE(classifyFilesystem(link.volume->filesystem), FilesystemKind::Unsupported);
}

void TestDeviceDisks::volumeNotMounted()
{
    LinuxPartition p;
    p.number = 2;
    p.major = 8;
    p.minor = 99;
    QVERIFY(!findMountedVolume(m_mounts, p));
    QVERIFY(!findMountedVolume({}, p));
}

void TestDeviceDisks::volumeSlotIsThePartitionNumber()
{
    // The names say nothing: the partition numbered 2 is the one that counts,
    // even when the first of the list is mounted and has a "2" in its name.
    LinuxDisk disk;
    disk.name = QStringLiteral("mmcblk0");
    disk.node = QStringLiteral("/dev/mmcblk0");
    disk.usbId = QStringLiteral("05ac:1261");
    disk.portPath = QStringLiteral("1-8");
    disk.partitions = {{QStringLiteral("mmcblk0p12"), 1, 8, 17},
                       {QStringLiteral("mmcblk0p1"), 2, 8, 18}};
    const LinuxLink link = linkLinuxDevice(
        device(QStringLiteral("05ac:1261"), QStringLiteral("1-8")), {disk}, m_mounts);
    QVERIFY(link.volume);
    QCOMPARE(link.volume->mountPoint, QStringLiteral("/run/media/user/IPOD"));
}

void TestDeviceDisks::notMountedDiskStillThere()
{
    // A disk with a partition 2 that nobody mounted: a disk, and no volume.
    LinuxDisk disk;
    disk.name = QStringLiteral("sdf");
    disk.node = QStringLiteral("/dev/sdf");
    disk.usbId = QStringLiteral("05ac:1261");
    disk.portPath = QStringLiteral("1-6");
    disk.partitions = {{QStringLiteral("sdf2"), 2, 8, 82}};
    const LinuxLink link = linkLinuxDevice(
        device(QStringLiteral("05ac:1261"), QStringLiteral("1-6")), {disk}, m_mounts);
    QVERIFY(link.disk);
    QVERIFY(!link.volume);
    // And a disk with no partition 2 at all.
    disk.partitions = {{QStringLiteral("sdf1"), 1, 8, 81}};
    QVERIFY(!linkLinuxDevice(device(QStringLiteral("05ac:1261"), QStringLiteral("1-6")), {disk},
                             m_mounts)
                 .volume);
}

void TestDeviceDisks::readMountinfoFiles()
{
    QVERIFY(readMountinfo(m_sys + QStringLiteral("/no-such-file")).isEmpty());
#ifdef Q_OS_LINUX
    QVERIFY(!readMountinfo().isEmpty());
#endif
}

void TestDeviceDisks::filesystem_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("kind");
    const int fat = int(FilesystemKind::Fat32);
    const int hfs = int(FilesystemKind::HfsPlus);
    const int no = int(FilesystemKind::Unsupported);
    QTest::newRow("vfat") << "vfat" << fat;
    QTest::newRow("msdos") << "msdos" << fat;
    QTest::newRow("FAT32") << "FAT32" << fat;
    QTest::newRow("hfsplus") << "hfsplus" << hfs;
    QTest::newRow("hfs") << "hfs" << hfs;
    QTest::newRow("HFS") << "HFS" << hfs;
    QTest::newRow("exfat") << "exfat" << no;
    QTest::newRow("ntfs") << "ntfs" << no;
    QTest::newRow("ext4") << "ext4" << no;
    QTest::newRow("empty") << "" << no;
}

void TestDeviceDisks::filesystem()
{
    QFETCH(QString, name);
    QFETCH(int, kind);
    QCOMPARE(int(classifyFilesystem(name)), kind);
}

void TestDeviceDisks::volumeSpace()
{
    VolumeInfo v;
    v.mountPoint = m_sys;
    readVolumeSpace(v);
    QVERIFY(v.totalBytes > 0);
    QVERIFY(v.freeBytes >= 0 && v.freeBytes <= v.totalBytes);

    VolumeInfo gone;
    gone.mountPoint = m_sys + QStringLiteral("/no-such-folder");
    readVolumeSpace(gone);
    QCOMPARE(gone.totalBytes, qint64(0));
}

QTEST_GUILESS_MAIN(TestDeviceDisks)
#include "tst_device_disks.moc"
