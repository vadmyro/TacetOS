// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "reading.h"

#include "battery.h"
#include "constants.h"
#include "markers.h"

#include <QFile>
#include <QFileInfo>

namespace tacetos::health {

namespace {

bool volumeIsThere(const QString &root)
{
    return QFileInfo(root).isDir();
}

// Reads `name` in `folder` of the volume, at most kMarkerMaxBytes. Logs why a
// file that is there is not read.
std::optional<QByteArray> readFile(const QString &root, const QString &folder, const QString &name,
                                   HealthFiles *files)
{
    const QString path = device::findMarkerPath(root, folder + QLatin1Char('/') + name);
    if (path.isEmpty())
        return std::nullopt;
    const QFileInfo info(path);
    // Only a regular file: opening a pipe or a device could block.
    if (!info.isFile()) {
        files->log.append(QStringLiteral("%1: not a file").arg(name));
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        files->log.append(QStringLiteral("%1: %2").arg(name, file.errorString()));
        return std::nullopt;
    }
    // One byte more than the limit tells "too large" without reading it all.
    QByteArray data = file.read(device::kMarkerMaxBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        files->log.append(QStringLiteral("%1: %2").arg(name, file.errorString()));
        return std::nullopt;
    }
    if (data.size() > device::kMarkerMaxBytes) {
        files->log.append(
            QStringLiteral("%1: larger than %2 bytes").arg(name).arg(device::kMarkerMaxBytes));
        return std::nullopt;
    }
    return data;
}

} // namespace

HealthFiles readHealthFiles(const QString &mountPoint, const QString &modelFolder)
{
    HealthFiles files;
    if (!volumeIsThere(mountPoint)) {
        files.volumeGone = true;
        return files;
    }

    QStringList candidates;
    if (!modelFolder.isEmpty())
        candidates << modelFolder;
    if (!candidates.contains(QString(device::kFirmwareFolder), Qt::CaseInsensitive))
        candidates << QString(device::kFirmwareFolder);
    for (const QString &candidate : candidates) {
        const QString path = device::findMarkerPath(mountPoint, candidate);
        if (!path.isEmpty() && QFileInfo(path).isDir()) {
            files.folder = candidate;
            break;
        }
    }

    if (!files.folder.isEmpty()) {
        files.health = readFile(mountPoint, files.folder, QString(device::kHealthFile), &files);
        files.charge = readFile(mountPoint, files.folder, QString(device::kChargeFile), &files);
    }
    // The folder may have vanished between two of the reads above.
    if (!volumeIsThere(mountPoint)) {
        files = HealthFiles();
        files.volumeGone = true;
    }
    return files;
}

HealthReading makeReading(const std::optional<QByteArray> &health,
                          const std::optional<QByteArray> &charge, const QDateTime &readAt)
{
    HealthReading reading;
    reading.readAt = readAt;

    if (health) {
        reading.healthBytes = *health;
        const HealthFileReading parsed = parseHealthFile(*health);
        if (parsed.ok) {
            reading.ok = true;
            reading.health = parsed.file;
            reading.disk = readDisk(parsed.file, &reading.log);
        } else {
            reading.log.append(parsed.error);
        }
    }

    if (charge) {
        reading.chargeBytes = *charge;
        const ChargeFileReading parsed = parseChargeFile(*charge);
        if (parsed.ok) {
            reading.chargeOk = true;
            reading.charge = parsed.file;
        } else {
            reading.log.append(parsed.error);
        }
    }

    if (reading.ok && reading.chargeOk)
        reading.estimateH = estimateHours(reading.charge, reading.health);
    return reading;
}

} // namespace tacetos::health
