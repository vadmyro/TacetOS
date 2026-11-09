// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "markers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace tacetos::device {

namespace {

// The entry of `dir` called `name` without regard to case, hidden and system
// entries included. Empty when there is none.
QFileInfo findEntry(const QString &dir, const QString &name)
{
    const QDir d(dir);
    const QFileInfoList entries =
        d.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().compare(name, Qt::CaseInsensitive) == 0)
            return entry;
    }
    return QFileInfo();
}

// Splits `Key: value` lines. `keyPattern` says what a key looks like.
QList<QPair<QString, QString>> keyValueLines(const QString &text,
                                             const QRegularExpression &keyPattern)
{
    QList<QPair<QString, QString>> lines;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon);
        if (!keyPattern.match(key).hasMatch())
            continue;
        // A colon, then white space, then the value.
        const QString rest = line.mid(colon + 1);
        if (rest.isEmpty() || !rest.at(0).isSpace())
            continue;
        lines.append({key, rest.trimmed()});
    }
    return lines;
}

bool volumeIsThere(const QString &root)
{
    return QFileInfo(root).isDir();
}

} // namespace

QString findMarkerPath(const QString &root, const QString &relativePath)
{
    QString current = root;
    const QStringList parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QString();
    for (const QString &part : parts) {
        const QFileInfo entry = findEntry(current, part);
        // A link is refused whatever it points to: this is stricter than
        // "not outside the volume", and FAT and HFS+ volumes have none.
        if (entry.fileName().isEmpty() || entry.isSymLink())
            return QString();
        current = entry.absoluteFilePath();
    }
    return current;
}

MarkerRead readMarkerFile(const QString &root, const QString &relativePath, qint64 maxBytes)
{
    MarkerRead result;
    if (!volumeIsThere(root)) {
        result.status = ReadStatus::VolumeGone;
        return result;
    }

    const QString path = findMarkerPath(root, relativePath);
    if (path.isEmpty()) {
        result.status = volumeIsThere(root) ? ReadStatus::Missing : ReadStatus::VolumeGone;
        return result;
    }

    // Only a regular file: opening a pipe or a device could block.
    const QFileInfo info(path);
    if (!info.isFile()) {
        result.status = volumeIsThere(root) ? ReadStatus::Unreadable : ReadStatus::VolumeGone;
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.status = volumeIsThere(root) ? ReadStatus::Unreadable : ReadStatus::VolumeGone;
        return result;
    }
    // One byte more than the limit tells "too large" without reading it all.
    const QByteArray data = file.read(maxBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        result.status = volumeIsThere(root) ? ReadStatus::Unreadable : ReadStatus::VolumeGone;
        return result;
    }
    if (data.size() > maxBytes) {
        result.status = ReadStatus::TooLarge;
        return result;
    }
    if (data.contains('\0')) {
        result.status = ReadStatus::NotText;
        return result;
    }
    result.status = ReadStatus::Ok;
    result.text = QString::fromUtf8(data);
    return result;
}

InfoFile parseInfoFile(const QString &text, const QString &distributionKey)
{
    static const QRegularExpression keyPattern(QStringLiteral("^[A-Z][A-Za-z ]*$"));
    InfoFile info;
    for (const auto &[key, value] : keyValueLines(text, keyPattern)) {
        if (key == QLatin1String("Target"))
            info.target = value;
        else if (key == QLatin1String("Target id"))
            info.targetId = value;
        else if (key == QLatin1String("Version"))
            info.version = value;
        else if (key == QLatin1String("Binary"))
            info.binary = value;
        else if (!distributionKey.isEmpty() && key == distributionKey)
            info.distribution = value;
    }
    return info;
}

QStringList modelsForTarget(const QList<Model> &models, const QString &target)
{
    QStringList ids;
    const QString wanted = target.trimmed();
    if (wanted.isEmpty())
        return ids;
    for (const Model &m : models) {
        if (!m.bootloaderTarget.isEmpty() &&
            m.bootloaderTarget.compare(wanted, Qt::CaseInsensitive) == 0)
            ids.append(m.id);
    }
    return ids;
}

MarkerReading readMarkers(const QString &mountPoint, const QList<Model> &models,
                          const QString &distributionKey)
{
    MarkerReading reading;
    if (!volumeIsThere(mountPoint)) {
        reading.volumeGone = true;
        return reading;
    }

    // Section 7.2. Only the presence of the root folder and of the database
    // is used; the model file is read as text.
    reading.appleFolder = !findMarkerPath(mountPoint, kAppleRootFolder).isEmpty();
    const QString database = findMarkerPath(mountPoint, kAppleDatabase);
    reading.appleDatabase = !database.isEmpty() && QFileInfo(database).isFile();

    const MarkerRead sysInfo = readMarkerFile(mountPoint, kAppleModelFile);
    if (sysInfo.status == ReadStatus::VolumeGone)
        reading.volumeGone = true;
    if (sysInfo.status == ReadStatus::Ok) {
        reading.appleModelFile = true;
        static const QRegularExpression anyKey(QStringLiteral("^[^\\s:][^:]*$"));
        for (const auto &[key, value] : keyValueLines(sysInfo.text, anyKey))
            reading.appleModelFields.insert(key, value);
    }

    // Sections 7.3 and 7.4.
    const QString folder = QString(kFirmwareFolder);
    if (findMarkerPath(mountPoint, folder).isEmpty()) {
        reading.firmware.files = FirmwareFiles::Absent;
    } else {
        reading.firmware.files = FirmwareFiles::Unknown;
        const MarkerRead infoRead =
            readMarkerFile(mountPoint, folder + QLatin1Char('/') + QString(kFirmwareInfoFile));
        if (infoRead.status == ReadStatus::VolumeGone)
            reading.volumeGone = true;
        if (infoRead.status == ReadStatus::Ok) {
            reading.info = parseInfoFile(infoRead.text, distributionKey);
            reading.modelsFromInfo = modelsForTarget(models, reading.info.target);
            if (!reading.modelsFromInfo.isEmpty()) {
                if (!reading.info.distribution.isEmpty()) {
                    reading.firmware.files = FirmwareFiles::Tacetos;
                    reading.firmware.version = reading.info.distribution;
                } else {
                    reading.firmware.files = FirmwareFiles::Present;
                }
            }
        }
    }

    // The folder may have vanished between two of the reads above.
    if (!volumeIsThere(mountPoint))
        reading.volumeGone = true;
    return reading;
}

MarkerReading readMarkers(const QString &mountPoint, const QList<Model> &models)
{
    return readMarkers(mountPoint, models, QString(kDistributionKey));
}

} // namespace tacetos::device
