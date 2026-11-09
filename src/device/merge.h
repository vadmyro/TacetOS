// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "markers.h"
#include "models.h"
#include "usblist.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// Merging the evidence (docs/specs/detection.md section 8): the readings of the
// three layers for one iPod go in, the detected iPod comes out. A pure
// function: no system access, no clock.

namespace tacetos::device {

// How the read of the disk (bootloader-legacy.md section 5) ended.
enum class DiskReadStatus {
    // Not made: not asked (the Classic has nothing to read) or not yet.
    NotRead,
    // It gave a generation, in `target`.
    Ok,
    // The system refused the read (section 5, last paragraph).
    PermissionDenied,
    // The disk looks like an iPod but its generation is not in the table.
    UnknownGeneration,
};

struct DiskReading
{
    DiskReadStatus status = DiskReadStatus::NotRead;
    // The target name of the generation (`ipodvideo`), when status is Ok.
    QString target;
    // The partition test saw a Mac iPod (bootloader-legacy.md section 3).
    bool macFormat = false;
};

// What was seen of an iPod in normal mode, kept by the live part for rule 5.
struct PreviousIpod
{
    DetectedIpod ipod;
    // How long ago it was seen.
    qint64 ageMs = 0;
};

// Everything known about one iPod at one moment.
struct IpodEvidence
{
    // None when the iPod is only known from a folder picked by hand
    // (section 6.4).
    std::optional<UsbDevice> usb;
    std::optional<DiskInfo> disk;
    std::optional<VolumeInfo> volume;
    DiskReading diskReading;
    // What the marker files said. Only used when there is a volume.
    MarkerReading markers;
    // Id of the model the user picked, empty when none.
    QString userModel;
    // The wait of section 9.2 is over.
    bool waitOver = false;
    // The last normal mode reading of the same port, if any.
    std::optional<PreviousIpod> previous;
};

struct MergeResult
{
    DetectedIpod ipod;
    // Why the decisions were taken, for the logs (section 2, last paragraph).
    QStringList log;
};

// Section 8. The order of trust is 8.1, the rules are 8.2, the state is 8.3.
MergeResult mergeEvidence(const IpodEvidence &evidence, const QList<Model> &models);

} // namespace tacetos::device
