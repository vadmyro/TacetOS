// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "linuxsource.h"

#include "linuxdisks.h"
#include "markers.h"
#include "usblist.h"
#include "volume.h"

namespace tacetos::device {

LinuxDeviceSource::LinuxDeviceSource(QList<Model> models, LinuxSourcePaths paths)
    : models_(std::move(models)), paths_(std::move(paths))
{
}

std::optional<QList<UsbDevice>> LinuxDeviceSource::readUsb()
{
    const UsbList list = readLinuxUsbList(paths_.usbDevices);
    // A list that could not be read says nothing: the screen keeps what it has.
    if (!list.error.isEmpty())
        return std::nullopt;
    const UsbClassification classified = classifyUsb(list.entries, models_);
    usbLog_ = classified.log;
    return classified.devices;
}

PortReading LinuxDeviceSource::readPort(const UsbDevice &device)
{
    PortReading reading;

    const LinuxDiskList disks = readLinuxDisks(paths_.classBlock);
    if (!disks.error.isEmpty())
        return reading;
    const QList<MountEntry> mounts = readMountinfo(paths_.mountinfo);

    const LinuxLink link = linkLinuxDevice(device, disks.disks, mounts);
    reading.disk = link.disk;
    reading.volume = link.volume;
    if (!reading.volume)
        return reading;

    // Section 6.2: nothing is read from a filesystem that is not supported.
    if (classifyFilesystem(reading.volume->filesystem) == FilesystemKind::Unsupported)
        return reading;

    if (readVolumeSpace_)
        tacetos::device::readVolumeSpace(*reading.volume);
    reading.markers = readMarkers(reading.volume->mountPoint, models_);
    return reading;
}

} // namespace tacetos::device
