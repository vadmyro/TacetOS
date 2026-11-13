// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "linuxfacts.h"

#include "helperconstants.h"
#include "linuxdisks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace tacetos::helper {

namespace {

const QString kBlockInterface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kDriveInterface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString kPartitionInterface = QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString kLoopInterface = QStringLiteral("org.freedesktop.UDisks2.Loop");

// A byte string of D-Bus ends with a zero byte.
QString bytesText(const QVariant &value)
{
    QByteArray bytes = value.toByteArray();
    const int zero = bytes.indexOf('\0');
    if (zero >= 0)
        bytes.truncate(zero);
    return QString::fromUtf8(bytes);
}

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.read(256)).trimmed();
}

// The device number of Linux: the major and minor are spread over a 64-bit number.
void splitDeviceNumber(quint64 dev, int *major, int *minor)
{
    *major = int(((dev >> 8) & 0xfff) | ((dev >> 32) & ~quint64(0xfff)));
    *minor = int((dev & 0xff) | ((dev >> 12) & ~quint64(0xff)));
}

// The system folders of section 7.1 step 4 (fourth point).
const char *const kSystemMountPoints[] = {"/",     "/usr", "/boot", "/boot/efi",
                                          "/home", "/var", "/etc"};

bool pathIsUnder(const QString &path, const QString &folder)
{
    if (folder == QLatin1String("/"))
        return true;
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

} // namespace

BlockFacts blockFactsFromObjects(const tacetos::device::UdisksObjects &objects, const QString &node)
{
    BlockFacts facts;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto block = it.value().constFind(kBlockInterface);
        if (block == it.value().cend() || bytesText(block->value(QStringLiteral("Device"))) != node)
            continue;
        facts.found = true;
        facts.node = node;
        facts.blockPath = it.key();
        facts.isPartition = it.value().contains(kPartitionInterface);
        facts.isLoop = it.value().contains(kLoopInterface);
        facts.sizeBytes = qint64(block->value(QStringLiteral("Size")).toULongLong());
        splitDeviceNumber(block->value(QStringLiteral("DeviceNumber")).toULongLong(), &facts.major,
                          &facts.minor);
        const QString drivePath = block->value(QStringLiteral("Drive")).toString();
        const auto drive = objects.constFind(drivePath);
        if (drive != objects.cend() && drive->contains(kDriveInterface)) {
            const QVariantMap &props = drive->value(kDriveInterface);
            facts.hasDrive = true;
            facts.removable = props.value(QStringLiteral("Removable")).toBool() ||
                              props.value(QStringLiteral("MediaRemovable")).toBool();
            facts.connectionBus = props.value(QStringLiteral("ConnectionBus")).toString();
            facts.serial = props.value(QStringLiteral("Serial")).toString();
        }
        break;
    }
    return facts;
}

UsbFacts readUsbFacts(const QString &sysRoot, int major, int minor)
{
    UsbFacts usb;
    const QString rootCanonical = QFileInfo(sysRoot).canonicalFilePath();
    const QString start =
        QFileInfo(QStringLiteral("%1/dev/block/%2:%3").arg(sysRoot).arg(major).arg(minor))
            .canonicalFilePath();
    if (rootCanonical.isEmpty() || start.isEmpty())
        return usb;

    // The partitions of the disk, for the check of the system.
    const QDir disk(start);
    for (const QString &name : disk.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!QFile::exists(disk.filePath(name + QStringLiteral("/partition"))))
            continue;
        usb.partitionNames.append(name);
        const QStringList numbers =
            readText(disk.filePath(name + QStringLiteral("/dev"))).split(QLatin1Char(':'));
        if (numbers.size() == 2)
            usb.deviceNumbers.append((quint64(numbers.at(0).toUInt()) << 8) |
                                     quint64(numbers.at(1).toUInt()));
    }

    QDir dir(start);
    while (dir.path().startsWith(rootCanonical + QLatin1Char('/'))) {
        if (dir.exists(QStringLiteral("idVendor")) && dir.exists(QStringLiteral("idProduct"))) {
            usb.found = true;
            usb.vendor = readText(dir.filePath(QStringLiteral("idVendor"))).toLower();
            usb.product = readText(dir.filePath(QStringLiteral("idProduct"))).toLower();
            bool ok = false;
            const int bus = readText(dir.filePath(QStringLiteral("busnum"))).toInt(&ok);
            usb.busNumber = ok ? bus : -1;
            const int dev = readText(dir.filePath(QStringLiteral("devnum"))).toInt(&ok);
            usb.deviceNumber = ok ? dev : -1;
            usb.port = dir.dirName();
            usb.serial = readText(dir.filePath(QStringLiteral("serial")));
            return usb;
        }
        if (!dir.cdUp())
            break;
    }
    return usb;
}

bool isIpodUsbId(const QString &vendor, const QString &product)
{
    // Kept in step with `normal` and `disk_mode` of models/models.toml (a test
    // compares them). The DFU ids are no disks.
    static const char *const kProducts[] = {"1201", "1203", "1204", "1205",
                                            "1209", "120a", "1260", "1261"};
    if (vendor.toLower() != QLatin1String("05ac"))
        return false;
    for (const char *p : kProducts) {
        if (product.toLower() == QLatin1String(p))
            return true;
    }
    return false;
}

bool isPlainDiskNode(const QString &node)
{
    static const QRegularExpression plain(
        QStringLiteral("^/dev/((sd|hd|vd|xvd)[a-z]+|nvme[0-9]+n[0-9]+|mmcblk[0-9]+)$"));
    return plain.match(node).hasMatch();
}

QStringList parseSwapNames(const QString &text)
{
    QStringList names;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 1; i < lines.size(); ++i) {
        const QString first =
            lines.at(i)
                .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
                .value(0);
        if (first.startsWith(QLatin1Char('/')))
            names.append(first);
    }
    return names;
}

bool udisksVersionSupportsOpenDevice(const QString &version)
{
    const QStringList parts = version.trimmed().split(QLatin1Char('.'));
    int numbers[3] = {0, 0, 0};
    for (int i = 0; i < 3 && i < parts.size(); ++i) {
        bool ok = false;
        numbers[i] = parts.at(i).toInt(&ok);
        if (!ok)
            return false;
    }
    if (numbers[0] != 2)
        return numbers[0] > 2;
    if (numbers[1] != 7)
        return numbers[1] > 7;
    return numbers[2] >= 3;
}

DiskVerdict judgeDisk(const DiskRequest &request, const DiskInputs &inputs)
{
    auto refuse = [](const QLatin1String &code, const QString &detail) {
        DiskVerdict v;
        v.code = QString(code);
        v.detail = detail;
        return v;
    };
    const BlockFacts &block = inputs.block;
    const UsbFacts &usb = inputs.usb;

    // Section 5.2: a whole disk, never a partition or a volume.
    if (!isPlainDiskNode(request.node))
        return refuse(err::badTarget,
                      QStringLiteral("%1 is not the name of a whole disk").arg(request.node));
    if (!block.found)
        return refuse(err::diskGone, QStringLiteral("udisks2 does not know %1").arg(request.node));
    if (block.isPartition || block.isLoop)
        return refuse(err::badTarget,
                      QStringLiteral("%1 is a partition or a loop device").arg(request.node));

    // Found again: the same size, serial and port as the request.
    if (block.sizeBytes != request.sizeBytes)
        return refuse(err::diskChanged,
                      QStringLiteral("the size of the disk is %1, the request says %2")
                          .arg(block.sizeBytes)
                          .arg(request.sizeBytes));
    const QString actualSerial = usb.found ? usb.serial : block.serial;
    if (actualSerial != request.serial ||
        (!block.serial.isEmpty() && block.serial != request.serial))
        return refuse(err::diskChanged,
                      QStringLiteral("the serial of the disk is not the one of the request"));
    const QString actualPort = usb.found ? usb.port : QString();
    if (actualPort != request.port)
        return refuse(err::diskChanged,
                      QStringLiteral("the USB port of the disk is \"%1\", the request says \"%2\"")
                          .arg(actualPort, request.port));

    // Removable and on USB, as the system says (section 7.2).
    if (!block.hasDrive || !block.removable || block.connectionBus != QLatin1String("usb"))
        return refuse(
            err::notRemovableUsb,
            QStringLiteral("the disk is not removable and on USB (bus \"%1\", removable %2)")
                .arg(block.connectionBus,
                     block.removable ? QStringLiteral("yes") : QStringLiteral("no")));

    // Apple's vendor id and the product id of an iPod.
    if (!usb.found)
        return refuse(err::notAnIpod, QStringLiteral("no USB device was found above the disk"));
    if (!isIpodUsbId(usb.vendor, usb.product))
        return refuse(err::notAnIpod, QStringLiteral("the USB id %1:%2 is not the one of an iPod")
                                          .arg(usb.vendor, usb.product));

    // The last net: nothing of the system may be on this disk.
    QList<quint64> numbers = usb.deviceNumbers;
    numbers.append((quint64(block.major) << 8) | quint64(block.minor));
    for (const tacetos::device::MountEntry &mount : inputs.mounts) {
        if (!numbers.contains((quint64(mount.major) << 8) | quint64(mount.minor)))
            continue;
        for (const char *point : kSystemMountPoints) {
            if (mount.mountPoint == QLatin1String(point))
                return refuse(err::systemDisk,
                              QStringLiteral("%1 holds %2").arg(request.node, mount.mountPoint));
        }
        if (!inputs.dataDir.isEmpty() && pathIsUnder(inputs.dataDir, mount.mountPoint))
            return refuse(err::systemDisk,
                          QStringLiteral("the data folder is on %1").arg(request.node));
    }
    for (const QString &swap : parseSwapNames(inputs.swaps)) {
        bool onDisk = swap == request.node;
        for (const QString &name : usb.partitionNames)
            onDisk = onDisk || swap == QStringLiteral("/dev/") + name;
        if (onDisk)
            return refuse(err::systemDisk, QStringLiteral("%1 is used as swap").arg(swap));
    }

    DiskVerdict ok;
    ok.ok = true;
    return ok;
}

} // namespace tacetos::helper
