// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "usblist.h"

#include <QList>
#include <QString>
#include <optional>

// Layer 2 on Linux (docs/specs/detection.md sections 5 and 6.1 to 6.5): from
// the USB device to its disk through sysfs, and from the disk to the mounted
// music volume through mountinfo. The readers take the folder or the text to
// read, so the tests feed them a fake tree.

namespace tacetos::device {

struct LinuxPartition
{
    // Kernel name, for example "sdb2".
    QString name;
    // The number the system gives (the `partition` file), not a name suffix.
    int number = 0;
    int major = 0;
    int minor = 0;
};

// A whole disk that sits under an Apple USB device.
struct LinuxDisk
{
    // Kernel name, for example "sdb".
    QString name;
    // "/dev/sdb".
    QString node;
    // What the USB ancestor says: its id and its folder name.
    QString usbId;
    QString portPath;
    qint64 sizeBytes = 0;
    int sectorSize = 0;
    // Sorted by number.
    QList<LinuxPartition> partitions;
};

struct LinuxDiskList
{
    QList<LinuxDisk> disks;
    // Empty when the list was read without a problem.
    QString error;
};

// Section 5. Reads `classBlock`, which is /sys/class/block on a real system.
// Each whole disk is resolved, then its parent folders are walked until one
// holds idVendor and idProduct. Disks with no such ancestor, or whose ancestor
// is not of vendor 05ac, are left out and never opened. Sorted by kernel name.
LinuxDiskList readLinuxDisks(const QString &classBlock = QStringLiteral("/sys/class/block"));

// One line of mountinfo (section 6.1).
struct MountEntry
{
    int major = 0;
    int minor = 0;
    // The path inside the filesystem that is mounted. "/" for a whole one.
    QString root;
    // Escapes decoded.
    QString mountPoint;
    QString filesystem;
    QString source;
};

// Section 6.1. Lines that do not parse are skipped.
QList<MountEntry> parseMountinfo(const QString &text);

// Reads /proc/self/mountinfo, or another file for the tests. Empty when it
// cannot be read.
QList<MountEntry> readMountinfo(const QString &path = QStringLiteral("/proc/self/mountinfo"));

// Section 6.1: the volume of the partition `partition`, by device number. When
// a device is mounted more than once, the mount of its whole filesystem wins,
// then the first. None when it is not mounted.
std::optional<VolumeInfo> findMountedVolume(const QList<MountEntry> &mounts,
                                            const LinuxPartition &partition);

// Section 5: the disk under a USB device. The match is made on the port path
// (and the id must agree), never on the id alone, so two iPods of one model
// get their own disk. None when the walk found no disk.
std::optional<LinuxDisk> findDiskForUsb(const QList<LinuxDisk> &disks, const UsbDevice &device);

// What sections 5 and 6 give for one iPod.
struct LinuxLink
{
    std::optional<DiskInfo> disk;
    std::optional<VolumeInfo> volume;
};

// The disk, then the volume of its partition 2. The space and the label of the
// volume are not read here (see readVolumeSpace).
LinuxLink linkLinuxDevice(const UsbDevice &device, const QList<LinuxDisk> &disks,
                          const QList<MountEntry> &mounts);

} // namespace tacetos::device
