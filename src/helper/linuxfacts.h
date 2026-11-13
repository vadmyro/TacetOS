// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "udisks.h"

#include <QList>
#include <QString>
#include <QStringList>

// What the helper learns about a disk on Linux, and the verdict of section 7.1
// step 4 (docs/specs/helper.md sections 7.2 and 11.2). Everything here is a pure
// function of recorded data: the answers of udisks2, a root folder standing for
// /sys, the text of mountinfo and of /proc/swaps. Nothing talks to the system.

namespace tacetos::helper {

// The block device object of udisks2 whose `Device` is the node asked for, and
// its drive. `found` is false when no object has that name.
struct BlockFacts
{
    bool found = false;
    QString node;
    QString blockPath;
    bool isPartition = false;
    bool isLoop = false;
    qint64 sizeBytes = 0;
    // The device number of the block (`dev_t`), split.
    int major = 0;
    int minor = 0;
    bool hasDrive = false;
    bool removable = false;
    QString connectionBus;
    QString serial;
};

// From the answer of `GetManagedObjects` (section 11.2 steps 1 and 2).
BlockFacts blockFactsFromObjects(const tacetos::device::UdisksObjects &objects,
                                 const QString &node);

// The first parent of the block device, in the device tree, that is a USB device.
struct UsbFacts
{
    bool found = false;
    QString vendor;
    QString product;
    int busNumber = -1;
    int deviceNumber = -1;
    // The name of the folder, for example `1-2`.
    QString port;
    QString serial;
    // The kernel names of the partitions of the disk, and their device numbers.
    QStringList partitionNames;
    QList<quint64> deviceNumbers;
};

// `sysRoot` is `/sys` (a copy of it in the tests). Section 11.2 step 3. Walks up
// from `<sysRoot>/dev/block/<major>:<minor>` and never leaves `sysRoot`.
UsbFacts readUsbFacts(const QString &sysRoot, int major, int minor);

// Section 7.1 step 4, third point: Apple's vendor id and the product id of an iPod
// in normal mode or in Disk Mode (`models/models.toml`, kept in step by a test).
bool isIpodUsbId(const QString &vendor, const QString &product);

// What the request says about the disk (section 5.1).
struct DiskRequest
{
    QString node;
    qint64 sizeBytes = 0;
    QString serial;
    QString port;
};

// Everything read for one disk.
struct DiskInputs
{
    BlockFacts block;
    UsbFacts usb;
    QList<tacetos::device::MountEntry> mounts;
    // The text of /proc/swaps.
    QString swaps;
    // The data folder of the command, when it has one.
    QString dataDir;
};

struct DiskVerdict
{
    bool ok = false;
    // One code of section 13, and the reason in English.
    QString code;
    QString detail;
};

// Section 5.2 and section 7.1 step 4, in the order of the spec: the shape of the
// node, found again, size, serial and port, removable and on USB, Apple's ids,
// then the system check.
DiskVerdict judgeDisk(const DiskRequest &request, const DiskInputs &inputs);

// A node of the system that is not a plain disk: a partition, a loop device, a
// device-mapper or RAID volume, a network block device, an optical drive.
bool isPlainDiskNode(const QString &node);

// The names of the swap areas of /proc/swaps.
QStringList parseSwapNames(const QString &text);

// Section 11.1: udisks2 2.7.3 or newer.
bool udisksVersionSupportsOpenDevice(const QString &version);

} // namespace tacetos::helper
