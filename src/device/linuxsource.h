// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "live.h"
#include "models.h"

#include <QList>
#include <QString>

// The real source of readings on Linux (docs/specs/detection.md sections 3, 5,
// 6 and 7): it puts together the readers of layers 1 to 3 behind the
// DeviceSource of the live part. The folders it reads are given, so the tests
// point it at a fake tree and never at the real /sys.

namespace tacetos::device {

struct LinuxSourcePaths
{
    // The USB devices (section 3.1).
    QString usbDevices = QStringLiteral("/sys/bus/usb/devices");
    // The block devices (section 5).
    QString classBlock = QStringLiteral("/sys/class/block");
    // The mounts (section 6.1).
    QString mountinfo = QStringLiteral("/proc/self/mountinfo");
};

class LinuxDeviceSource : public DeviceSource
{
public:
    explicit LinuxDeviceSource(QList<Model> models, LinuxSourcePaths paths = LinuxSourcePaths());

    // Section 6.3: by default the total and free bytes and the label of the
    // volume are read from its mount point. A test turns it off to keep the
    // readings the same on every machine.
    void setReadVolumeSpace(bool on) { readVolumeSpace_ = on; }

    std::optional<QList<UsbDevice>> readUsb() override;
    // The disk reading of section 5 (the table, the banner) is not made here:
    // `diskReading` stays at NotRead. It needs the helper and is not part of
    // this source.
    PortReading readPort(const UsbDevice &device) override;

private:
    QList<Model> models_;
    LinuxSourcePaths paths_;
    bool readVolumeSpace_ = true;
};

} // namespace tacetos::device
