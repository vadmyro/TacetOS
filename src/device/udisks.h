// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "linuxdisks.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <memory>
#include <optional>

// Mount, unmount and eject of an iPod on Linux, through the disk service of
// the desktop (udisks2). The service asks the user's permission itself when it
// needs to, so TacetOS never runs as root and never calls `sudo`. Only a disk
// found by readLinuxDisks (a USB ancestor of vendor 05ac) can be given to
// these functions, and they check again that the service sees it on a USB bus.
//
// The service is reached through UdisksBackend. The real one speaks D-Bus
// (udisksbackend.h); the tests use one that answers from memory.

namespace tacetos::device {

// What the service knows, as it sends it: object path > interface > properties.
// Byte strings are QByteArray (with or without their final zero byte).
using UdisksObjects = QMap<QString, QMap<QString, QVariantMap>>;

// A block device (disk or partition) of the service.
struct UdisksBlock
{
    QString path;
    // "/dev/sdb2".
    QString node;
    quint64 deviceNumber = 0;
    // Object path of its drive, "/" when it has none.
    QString drivePath;
    // What the service read in it ("vfat", "hfsplus", ...). Empty when unknown.
    QString filesystem;
    QString label;
    quint64 sizeBytes = 0;
    // The block has a filesystem the service can mount.
    bool hasFilesystem = false;
    // 0 when the block is not a partition.
    int partitionNumber = 0;
    QStringList mountPoints;

    bool mounted() const { return !mountPoints.isEmpty(); }
};

// A drive of the service (the physical device).
struct UdisksDrive
{
    QString path;
    QString vendor;
    QString model;
    // "usb" for a USB device. Empty when the service does not say.
    QString connectionBus;
    bool ejectable = false;
    bool canPowerOff = false;
};

struct UdisksSnapshot
{
    QList<UdisksBlock> blocks;
    QList<UdisksDrive> drives;
};

// Turns the answer of the service into plain structs. Objects without the
// block or drive interface are left out.
UdisksSnapshot parseUdisksObjects(const UdisksObjects &objects);

// The device number of Linux (`dev_t`) from its major and minor.
quint64 makeDeviceNumber(int major, int minor);

// The answer to one call of the service.
struct UdisksReply
{
    // The D-Bus error name. Empty when the call worked.
    QString errorName;
    QString errorMessage;
    // The returned string, when there is one (the mount point of a mount).
    QString value;

    bool ok() const { return errorName.isEmpty(); }
};

class UdisksBackend
{
public:
    virtual ~UdisksBackend() = default;

    // Every object of the service. None, with the error name and text in
    // `error`, when the service cannot be reached.
    virtual std::optional<UdisksObjects> managedObjects(UdisksReply *error) = 0;
    virtual UdisksReply mount(const QString &blockPath, bool readOnly) = 0;
    virtual UdisksReply unmount(const QString &blockPath) = 0;
    // Eject the medium of a drive.
    virtual UdisksReply eject(const QString &drivePath) = 0;
    // Cut the power of a USB drive, so it can be unplugged.
    virtual UdisksReply powerOff(const QString &drivePath) = 0;
};

enum class DiskError {
    None,
    // The service does not know the disk, or does not see it on a USB bus.
    NotAnIpodDisk,
    // No disk service on this system (udisks2 is required).
    ServiceMissing,
    // The system refused, or the user did not give the permission.
    NotAuthorized,
    // Something still uses the volume.
    Busy,
    // No partition that can be mounted.
    NoVolume,
    Failed,
};

struct DiskResult
{
    DiskError error = DiskError::None;
    // The text of the service, for the journal (not for the user).
    QString detail;
    // Mount: where the volume is.
    QString mountPoint;
    // Mount: it was mounted already, nothing was done.
    bool alreadyMounted = false;
    // Unmount and eject: how many volumes were unmounted.
    int unmounted = 0;
    // Eject: the drive was ejected or powered off. False when the volumes are
    // unmounted and the service has nothing more to do (still safe to unplug).
    bool released = false;

    bool ok() const { return error == DiskError::None; }
};

// Short text for the user, translatable (context "device").
QString diskErrorMessage(DiskError error);

// The error that the name of a D-Bus error stands for.
DiskError diskErrorFromName(const QString &errorName);

class DiskControl
{
public:
    explicit DiskControl(std::unique_ptr<UdisksBackend> backend);

    // Mounts the music volume of the disk: the best partition by
    // musicVolumeRank among those with a filesystem TacetOS knows.
    DiskResult mount(const LinuxDisk &disk, bool readOnly = false);
    // Unmounts every mounted partition of the disk. Succeeds when nothing was
    // mounted.
    DiskResult unmount(const LinuxDisk &disk);
    // Unmounts, then asks the service to power the drive off (or eject it).
    // The volumes are never left half done: when one cannot be unmounted, the
    // drive is not touched.
    DiskResult eject(const LinuxDisk &disk);

private:
    struct Target
    {
        DiskError error = DiskError::None;
        QString detail;
        UdisksDrive drive;
        bool hasDrive = false;
        // The blocks of the disk that can hold a filesystem, any order.
        QList<UdisksBlock> blocks;
    };

    Target resolve(const LinuxDisk &disk);
    DiskResult unmountBlocks(const Target &target);

    std::unique_ptr<UdisksBackend> backend_;
};

} // namespace tacetos::device
