// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "models.h"

#include <QString>
#include <QStringList>
#include <optional>

// What detection produces (docs/specs/detection.md section 2). Plain data: the
// readers and the merging step fill it in, nothing here reads a system.

namespace tacetos::device {

// Which evidence decided the model. Only the logs and the support see it.
enum class ModelSource { None, Usb, Disk, InfoFile, User };

// Section 8.3, in the order the merging step tests them.
enum class IpodState {
    Conflict,
    UnsupportedModel,
    Permission,
    Ambiguous,
    Dfu,
    Waiting,
    NoDisk,
    MacFormat,
    NoVolume,
    UnsupportedFilesystem,
    Ready,
};

// Section 7.4: the state of the firmware files on the music volume.
enum class FirmwareFiles { Absent, Present, Tacetos, Unknown };

struct DiskInfo
{
    QString node;
    qint64 sizeBytes = 0;
    int sectorSize = 0;
};

struct VolumeInfo
{
    // A folder, or a drive letter on Windows.
    QString mountPoint;
    QString filesystem;
    QString label;
    qint64 totalBytes = 0;
    qint64 freeBytes = 0;
};

struct FirmwareInfo
{
    FirmwareFiles files = FirmwareFiles::Absent;
    // Only set when files is Tacetos.
    QString version;
    // Section 8.2 rule 4: the info file names another model than the one
    // decided. The files on the volume are for another iPod.
    bool otherModel = false;
};

struct DetectedIpod
{
    // Empty when the iPod is only known from a volume picked by hand.
    QString usbId;
    UsbMode mode = UsbMode::None;
    QString portPath;
    QString serial;
    // Ids of the models the USB id can be, in the order of models.toml.
    QStringList candidates;
    // Empty until the merging step has decided.
    QString model;
    ModelSource modelSource = ModelSource::None;
    std::optional<DiskInfo> disk;
    std::optional<VolumeInfo> volume;
    IpodState state = IpodState::Waiting;
    FirmwareInfo firmware;
};

} // namespace tacetos::device
