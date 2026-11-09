// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "udisks.h"

#include "volume.h"

#include <QCoreApplication>
#include <algorithm>

namespace tacetos::device {

namespace {

const QString kBlockInterface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kFilesystemInterface = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const QString kPartitionInterface = QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString kDriveInterface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString kErrorPrefix = QStringLiteral("org.freedesktop.UDisks2.Error.");

QString tr(const char *text)
{
    return QCoreApplication::translate("device", text);
}

// A string that the service sends as a string or as bytes ending with a zero.
QString textOf(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QByteArray) {
        QByteArray bytes = value.toByteArray();
        const int end = bytes.indexOf('\0');
        if (end >= 0)
            bytes.truncate(end);
        return QString::fromUtf8(bytes);
    }
    return value.toString();
}

QStringList textListOf(const QVariant &value)
{
    QStringList list;
    if (value.metaType().id() == QMetaType::QStringList) {
        return value.toStringList();
    } else if (value.metaType().id() == QMetaType::QByteArrayList) {
        const QByteArrayList items = value.value<QByteArrayList>();
        for (const QByteArray &item : items)
            list << textOf(item);
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        const QVariantList items = value.toList();
        for (const QVariant &item : items)
            list << textOf(item);
    }
    return list;
}

} // namespace

quint64 makeDeviceNumber(int major, int minor)
{
    const quint64 ma = static_cast<quint64>(major);
    const quint64 mi = static_cast<quint64>(minor);
    return ((ma & 0xfff) << 8) | ((ma & ~quint64(0xfff)) << 32) | (mi & 0xff) |
           ((mi & ~quint64(0xff)) << 12);
}

UdisksSnapshot parseUdisksObjects(const UdisksObjects &objects)
{
    UdisksSnapshot snapshot;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QMap<QString, QVariantMap> &interfaces = it.value();

        const auto block = interfaces.constFind(kBlockInterface);
        if (block != interfaces.cend()) {
            UdisksBlock out;
            out.path = it.key();
            out.node = textOf(block->value(QStringLiteral("Device")));
            out.deviceNumber = block->value(QStringLiteral("DeviceNumber")).toULongLong();
            out.drivePath = block->value(QStringLiteral("Drive")).toString();
            out.filesystem = block->value(QStringLiteral("IdType")).toString();
            out.label = block->value(QStringLiteral("IdLabel")).toString();
            out.sizeBytes = block->value(QStringLiteral("Size")).toULongLong();
            const auto filesystem = interfaces.constFind(kFilesystemInterface);
            if (filesystem != interfaces.cend()) {
                out.hasFilesystem = true;
                out.mountPoints = textListOf(filesystem->value(QStringLiteral("MountPoints")));
            }
            const auto partition = interfaces.constFind(kPartitionInterface);
            if (partition != interfaces.cend())
                out.partitionNumber = partition->value(QStringLiteral("Number")).toInt();
            snapshot.blocks << out;
        }

        const auto drive = interfaces.constFind(kDriveInterface);
        if (drive != interfaces.cend()) {
            UdisksDrive out;
            out.path = it.key();
            out.vendor = drive->value(QStringLiteral("Vendor")).toString();
            out.model = drive->value(QStringLiteral("Model")).toString();
            out.connectionBus = drive->value(QStringLiteral("ConnectionBus")).toString();
            out.ejectable = drive->value(QStringLiteral("Ejectable")).toBool();
            out.canPowerOff = drive->value(QStringLiteral("CanPowerOff")).toBool();
            snapshot.drives << out;
        }
    }
    return snapshot;
}

QString diskErrorMessage(DiskError error)
{
    switch (error) {
    case DiskError::None:
        return {};
    case DiskError::NotAnIpodDisk:
        return tr("This disk is not an iPod that the system lets us handle.");
    case DiskError::ServiceMissing:
        return tr("The disk service (udisks2) is not running. Install it, then try again.");
    case DiskError::NotAuthorized:
        return tr("The system did not allow it. Try again and accept the permission request.");
    case DiskError::Busy:
        return tr("Another program is using the iPod. Close it, then try again.");
    case DiskError::NoVolume:
        return tr("The iPod has no volume that can be mounted.");
    case DiskError::Failed:
        break;
    }
    return tr("The system could not do it.");
}

DiskError diskErrorFromName(const QString &errorName)
{
    if (errorName.isEmpty())
        return DiskError::None;
    if (errorName.startsWith(kErrorPrefix)) {
        const QString name = errorName.mid(kErrorPrefix.size());
        if (name.startsWith(QLatin1String("NotAuthorized")))
            return DiskError::NotAuthorized;
        if (name == QLatin1String("DeviceBusy"))
            return DiskError::Busy;
        return DiskError::Failed;
    }
    // The bus has no one with the name of the service, or cannot start it.
    if (errorName == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown") ||
        errorName == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner") ||
        errorName == QLatin1String("org.freedesktop.DBus.Error.NoServer") ||
        errorName == QLatin1String("org.freedesktop.DBus.Error.Disconnected"))
        return DiskError::ServiceMissing;
    return DiskError::Failed;
}

DiskControl::DiskControl(std::unique_ptr<UdisksBackend> backend) : backend_(std::move(backend)) {}

DiskControl::Target DiskControl::resolve(const LinuxDisk &disk)
{
    Target target;

    UdisksReply problem;
    const std::optional<UdisksObjects> objects = backend_->managedObjects(&problem);
    if (!objects) {
        target.error = diskErrorFromName(problem.errorName);
        if (target.error == DiskError::None)
            target.error = DiskError::Failed;
        target.detail = problem.errorMessage;
        return target;
    }
    const UdisksSnapshot snapshot = parseUdisksObjects(*objects);

    // The whole disk, by its node. Its drive is the one every block must share.
    QString drivePath;
    for (const UdisksBlock &block : snapshot.blocks) {
        if (block.node == disk.node) {
            drivePath = block.drivePath;
            break;
        }
    }
    if (drivePath.isEmpty()) {
        target.error = DiskError::NotAnIpodDisk;
        target.detail = QStringLiteral("no block %1 in the disk service").arg(disk.node);
        return target;
    }

    if (drivePath != QLatin1String("/")) {
        for (const UdisksDrive &drive : snapshot.drives) {
            if (drive.path == drivePath) {
                target.drive = drive;
                target.hasDrive = true;
            }
        }
        // A disk that the service puts on another bus is not what the USB
        // walk said it was: leave it alone.
        if (target.hasDrive && !target.drive.connectionBus.isEmpty() &&
            target.drive.connectionBus != QLatin1String("usb")) {
            target.error = DiskError::NotAnIpodDisk;
            target.detail = QStringLiteral("bus %1").arg(target.drive.connectionBus);
            return target;
        }
    }

    for (const UdisksBlock &block : snapshot.blocks) {
        if (block.drivePath != drivePath)
            continue;
        bool ours = block.node == disk.node;
        for (const LinuxPartition &partition : disk.partitions) {
            if (block.node == QLatin1String("/dev/") + partition.name ||
                block.deviceNumber == makeDeviceNumber(partition.major, partition.minor))
                ours = true;
        }
        if (ours)
            target.blocks << block;
    }
    return target;
}

DiskResult DiskControl::mount(const LinuxDisk &disk, bool readOnly)
{
    DiskResult result;
    const Target target = resolve(disk);
    if (target.error != DiskError::None) {
        result.error = target.error;
        result.detail = target.detail;
        return result;
    }

    // The best candidate, the way section 6.1 of the detection spec picks the
    // music volume.
    const UdisksBlock *best = nullptr;
    int bestRank = 0;
    for (const UdisksBlock &block : target.blocks) {
        if (!block.hasFilesystem)
            continue;
        const FilesystemKind kind = classifyFilesystem(block.filesystem);
        if (kind == FilesystemKind::Unsupported)
            continue;
        const int rank = musicVolumeRank(block.partitionNumber, kind);
        if (!best || rank < bestRank) {
            best = &block;
            bestRank = rank;
        }
    }
    if (!best) {
        result.error = DiskError::NoVolume;
        return result;
    }
    if (best->mounted()) {
        result.alreadyMounted = true;
        result.mountPoint = best->mountPoints.first();
        return result;
    }

    const UdisksReply reply = backend_->mount(best->path, readOnly);
    if (!reply.ok()) {
        result.error = diskErrorFromName(reply.errorName);
        result.detail = reply.errorMessage;
        // Someone mounted it between our reading and our call.
        if (reply.errorName == kErrorPrefix + QLatin1String("AlreadyMounted")) {
            result.error = DiskError::None;
            result.alreadyMounted = true;
            for (const UdisksBlock &block : resolve(disk).blocks) {
                if (block.path == best->path && block.mounted())
                    result.mountPoint = block.mountPoints.first();
            }
        }
        return result;
    }
    result.mountPoint = reply.value;
    return result;
}

DiskResult DiskControl::unmountBlocks(const Target &target)
{
    DiskResult result;
    for (const UdisksBlock &block : target.blocks) {
        if (!block.hasFilesystem || !block.mounted())
            continue;
        const UdisksReply reply = backend_->unmount(block.path);
        if (reply.ok() || reply.errorName == kErrorPrefix + QLatin1String("NotMounted")) {
            ++result.unmounted;
            continue;
        }
        // Stop at the first volume that stays: the next ones are left as they are.
        result.error = diskErrorFromName(reply.errorName);
        result.detail = reply.errorMessage;
        return result;
    }
    return result;
}

DiskResult DiskControl::unmount(const LinuxDisk &disk)
{
    const Target target = resolve(disk);
    if (target.error != DiskError::None) {
        DiskResult result;
        result.error = target.error;
        result.detail = target.detail;
        return result;
    }
    return unmountBlocks(target);
}

DiskResult DiskControl::eject(const LinuxDisk &disk)
{
    const Target target = resolve(disk);
    if (target.error != DiskError::None) {
        DiskResult result;
        result.error = target.error;
        result.detail = target.detail;
        return result;
    }

    DiskResult result = unmountBlocks(target);
    if (!result.ok() || !target.hasDrive)
        return result;

    UdisksReply reply;
    if (target.drive.canPowerOff)
        reply = backend_->powerOff(target.drive.path);
    else if (target.drive.ejectable)
        reply = backend_->eject(target.drive.path);
    else
        return result;

    if (!reply.ok()) {
        result.error = diskErrorFromName(reply.errorName);
        result.detail = reply.errorMessage;
        return result;
    }
    result.released = true;
    return result;
}

} // namespace tacetos::device
