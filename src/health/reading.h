// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "disk.h"
#include "files.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <optional>

// One reading of the health of an iPod (docs/specs/health.md sections 3 and
// 4): the two files read from its firmware folder, then turned into what the
// app shows.

namespace tacetos::health {

// The bytes of the two files, as found on the music volume.
struct HealthFiles
{
    // None when the file is not there or is not read (too large, unreadable).
    std::optional<QByteArray> health;
    std::optional<QByteArray> charge;
    // The firmware folder the files were looked for in, empty when the volume
    // has none.
    QString folder;
    // One line for each file that is there but not read.
    QStringList log;
    // The volume went away during the read: nothing here can be trusted.
    bool volumeGone = false;
};

// Section 3: looks in `modelFolder` (`firmware_folder` of the model in
// models.toml), then in the default folder of the constants file, as detection
// does, without regard to case and never through a link. Each file is read only
// when it is at most kMarkerMaxBytes. Only reads.
HealthFiles readHealthFiles(const QString &mountPoint, const QString &modelFolder);

// Section 4, without the history (section 7).
struct HealthReading
{
    // The health file is usable: everything below but the charge file needs it.
    bool ok = false;
    HealthFile health;
    DiskReading disk;
    bool chargeOk = false;
    ChargeFile charge;
    // Section 6.2, from this pair of files alone. The history drops it when
    // the charge file already gave one (section 7).
    std::optional<double> estimateH;
    // The computer's time of the read, given by the caller.
    QDateTime readAt;
    // The files as read, for the history.
    QByteArray healthBytes;
    QByteArray chargeBytes;
    // The log lines of the files and of the blocks, in order.
    QStringList log;
};

// Pure: parses and decodes the files given, with no file system or clock.
HealthReading makeReading(const std::optional<QByteArray> &health,
                          const std::optional<QByteArray> &charge, const QDateTime &readAt);

} // namespace tacetos::health
