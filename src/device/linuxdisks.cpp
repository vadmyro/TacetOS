// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "linuxdisks.h"

#include "volume.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace tacetos::device {

namespace {

// How far up the device tree to look for the USB device. Real trees are about
// ten levels deep.
constexpr int kMaxLevels = 32;

// First line of a small sysfs file, or an empty string.
QString readAttribute(const QDir &dir, const QString &name)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.read(256)).section(QLatin1Char('\n'), 0, 0).trimmed();
}

bool isHexId(const QString &s)
{
    if (s.size() != 4)
        return false;
    return std::all_of(s.cbegin(), s.cend(), [](QChar c) {
        return c.isDigit() || (c.toLower() >= QLatin1Char('a') && c.toLower() <= QLatin1Char('f'));
    });
}

// "8:17" to its two numbers.
bool parseDeviceNumber(const QString &text, int &major, int &minor)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 2)
        return false;
    bool okMajor = false;
    bool okMinor = false;
    major = parts[0].toInt(&okMajor);
    minor = parts[1].toInt(&okMinor);
    return okMajor && okMinor;
}

// Going up from `start`, the first folder that holds idVendor and idProduct.
std::optional<QDir> usbAncestor(const QString &start)
{
    QDir dir(start);
    for (int level = 0; level < kMaxLevels; ++level) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("idVendor"))) &&
            QFileInfo::exists(dir.filePath(QStringLiteral("idProduct"))))
            return dir;
        if (!dir.cdUp())
            break;
    }
    return std::nullopt;
}

QList<LinuxPartition> readPartitions(const QDir &diskDir)
{
    QList<LinuxPartition> partitions;
    const QStringList names = diskDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QDir dir(diskDir.filePath(name));
        bool ok = false;
        const int number = readAttribute(dir, QStringLiteral("partition")).toInt(&ok);
        if (!ok)
            continue;
        LinuxPartition partition;
        partition.name = name;
        partition.number = number;
        if (!parseDeviceNumber(readAttribute(dir, QStringLiteral("dev")), partition.major,
                               partition.minor))
            continue;
        partitions.append(partition);
    }
    std::sort(partitions.begin(), partitions.end(),
              [](const LinuxPartition &a, const LinuxPartition &b) { return a.number < b.number; });
    return partitions;
}

// Mount points and sources write space, tab, newline and backslash as a
// backslash and three octal digits.
QString decodeMountEscapes(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('\\') && i + 3 < text.size()) {
            bool ok = false;
            const int value = text.mid(i + 1, 3).toInt(&ok, 8);
            if (ok && value < 256) {
                out.append(QChar(value));
                i += 3;
                continue;
            }
        }
        out.append(text[i]);
    }
    return out;
}

} // namespace

LinuxDiskList readLinuxDisks(const QString &classBlock)
{
    LinuxDiskList list;
    const QDir root(classBlock);
    if (!root.exists()) {
        list.error = QStringLiteral("cannot read %1").arg(classBlock);
        return list;
    }
    const QStringList names = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        // The entry is a link to the real folder of the node.
        const QString real = QFileInfo(root.filePath(name)).canonicalFilePath();
        if (real.isEmpty())
            continue;
        const QDir diskDir(real);
        // Partitions are listed under their disk, not on their own.
        if (QFileInfo::exists(diskDir.filePath(QStringLiteral("partition"))))
            continue;
        const std::optional<QDir> usb = usbAncestor(real);
        if (!usb)
            continue;
        const QString vendor = readAttribute(*usb, QStringLiteral("idVendor")).toLower();
        const QString product = readAttribute(*usb, QStringLiteral("idProduct")).toLower();
        if (!isHexId(vendor) || !isHexId(product) || vendor != QString::fromLatin1(kUsbVendorApple))
            continue;

        LinuxDisk disk;
        disk.name = name;
        disk.node = QStringLiteral("/dev/") + name;
        disk.usbId = vendor + QLatin1Char(':') + product;
        disk.portPath = usb->dirName();
        // The size file counts units of 512 bytes, whatever the sector size.
        disk.sizeBytes = readAttribute(diskDir, QStringLiteral("size")).toLongLong() * 512;
        disk.sectorSize =
            readAttribute(diskDir, QStringLiteral("queue/logical_block_size")).toInt();
        disk.partitions = readPartitions(diskDir);
        list.disks.append(disk);
    }
    return list;
}

QList<MountEntry> parseMountinfo(const QString &text)
{
    QList<MountEntry> entries;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // Fields 1 to 6, then optional ones up to a lone "-", then the type, the source and the
        // options.
        int separator = -1;
        for (int i = 6; i < fields.size(); ++i) {
            if (fields[i] == QLatin1String("-")) {
                separator = i;
                break;
            }
        }
        if (separator < 0 || separator + 2 >= fields.size())
            continue;
        MountEntry entry;
        if (!parseDeviceNumber(fields[2], entry.major, entry.minor))
            continue;
        entry.root = decodeMountEscapes(fields[3]);
        entry.mountPoint = decodeMountEscapes(fields[4]);
        entry.filesystem = fields[separator + 1];
        entry.source = decodeMountEscapes(fields[separator + 2]);
        entries.append(entry);
    }
    return entries;
}

QList<MountEntry> readMountinfo(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parseMountinfo(QString::fromUtf8(file.readAll()));
}

std::optional<VolumeInfo> findMountedVolume(const QList<MountEntry> &mounts,
                                            const LinuxPartition &partition)
{
    const MountEntry *best = nullptr;
    for (const MountEntry &mount : mounts) {
        if (mount.major != partition.major || mount.minor != partition.minor)
            continue;
        if (!best || (best->root != QLatin1String("/") && mount.root == QLatin1String("/")))
            best = &mount;
    }
    if (!best)
        return std::nullopt;
    VolumeInfo volume;
    volume.mountPoint = best->mountPoint;
    volume.filesystem = best->filesystem;
    return volume;
}

std::optional<LinuxDisk> findDiskForUsb(const QList<LinuxDisk> &disks, const UsbDevice &device)
{
    if (device.portPath.isEmpty())
        return std::nullopt;
    for (const LinuxDisk &disk : disks) {
        if (disk.portPath == device.portPath && disk.usbId == device.usbId)
            return disk;
    }
    return std::nullopt;
}

LinuxLink linkLinuxDevice(const UsbDevice &device, const QList<LinuxDisk> &disks,
                          const QList<MountEntry> &mounts)
{
    LinuxLink link;
    const std::optional<LinuxDisk> disk = findDiskForUsb(disks, device);
    if (!disk)
        return link;
    DiskInfo info;
    info.node = disk->node;
    info.sizeBytes = disk->sizeBytes;
    info.sectorSize = disk->sectorSize;
    link.disk = info;
    for (const LinuxPartition &partition : disk->partitions) {
        if (partition.number == kMusicVolumeSlot) {
            link.volume = findMountedVolume(mounts, partition);
            break;
        }
    }
    return link;
}

} // namespace tacetos::device
