// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "constants.h"
#include "detected.h"
#include "models.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Layer 3: marker files on the music volume (docs/specs/detection.md section
// 7). Everything here only reads, and nothing depends on the system that
// found the volume: it takes the mount point as a plain folder.

namespace tacetos::device {

// Section 7.1: how one read ended.
enum class ReadStatus {
    Ok,
    // No such entry.
    Missing,
    // More than the limit, so not one of our files (rule 3).
    TooLarge,
    // A zero byte in it (rule 4).
    NotText,
    // Present but not readable (a folder, a link, no right to read it).
    Unreadable,
    // The volume itself is gone: the user unplugged the iPod (rule 6).
    VolumeGone,
};

struct MarkerRead
{
    ReadStatus status = ReadStatus::Missing;
    // Set only when status is Ok. Line ends are left as they are.
    QString text;
};

// Section 7.1 rules 1, 2 and 5: finds `relativePath` (parts separated by `/`)
// under `root`, comparing names without regard to case, listing hidden and
// system entries, and never going through a link. Returns the path on disk of
// the entry, or an empty string when it is not there.
QString findMarkerPath(const QString &root, const QString &relativePath);

// Section 7.1 rules 3 to 7: reads a marker file as text. Reads at most
// `maxBytes` and writes nothing.
MarkerRead readMarkerFile(const QString &root, const QString &relativePath,
                          qint64 maxBytes = kMarkerMaxBytes);

// Section 7.3. Only the keys detection reads; the others are dropped.
struct InfoFile
{
    QString target;
    QString targetId;
    QString version;
    QString binary;
    // The value of the distribution line, when the file has one.
    QString distribution;
};

// Section 7.3: splits the text into `Key: value` lines (a key starts with an
// upper case letter, then letters and spaces; other lines are skipped, the
// last one of a key wins). `distributionKey` is the key of the TacetOS line,
// and nothing is read for it when it is empty.
InfoFile parseInfoFile(const QString &text, const QString &distributionKey);

// Ids of the models whose `bootloaderTarget` is this target (case does not
// matter). Empty when the target is unknown.
QStringList modelsForTarget(const QList<Model> &models, const QString &target);

// What the marker files say, for the merging step (section 8).
struct MarkerReading
{
    // Section 7.2.
    bool appleFolder = false;
    bool appleModelFile = false;
    // The `Key: value` lines of the Apple model file, not interpreted yet
    // (its content is still to be checked, section 13).
    QMap<QString, QString> appleModelFields;
    bool appleDatabase = false;

    // Sections 7.3 and 7.4.
    FirmwareInfo firmware;
    InfoFile info;
    // The models that the target of a valid info file stands for.
    QStringList modelsFromInfo;

    // The volume vanished during the read (section 7.1 rule 6). Nothing else
    // here can be trusted, and it is not an error to show.
    bool volumeGone = false;
};

// Section 7.4 with the distribution key of the constants file passed in so
// that a test can name one.
MarkerReading readMarkers(const QString &mountPoint, const QList<Model> &models,
                          const QString &distributionKey);
// Same, with the key of the constants file.
MarkerReading readMarkers(const QString &mountPoint, const QList<Model> &models);

} // namespace tacetos::device
