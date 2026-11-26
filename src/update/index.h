// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// The index of a release (docs/specs/update-channel.md section 3): one small
// JSON file that gives the manifest of every model.

namespace tacetos::update {

struct IndexPackage
{
    // As `bootloader_target` in models/models.toml.
    QString target;
    // The file name of the manifest, with no folder.
    QString manifest;
    qint64 size = 0;
    // Lowercase hexadecimal.
    QString sha256;
};

struct Index
{
    QString version;
    // Logged only.
    QString built;
    QList<IndexPackage> packages;

    // The package of `target`, or null when the index does not have it.
    const IndexPackage *package(const QString &target) const;
};

struct IndexReading
{
    bool ok = false;
    Index index;
    // Why the index is refused, for the log. Empty when ok.
    QString error;
};

// Reads an index. It is refused when it is larger than kIndexMaxBytes, is not
// a JSON object, has a `format` other than 1, misses a key or has one of the
// wrong type, or gives a target twice. A model missing from the index is not a
// fault of the index: package() just finds nothing for it.
IndexReading parseIndex(const QByteArray &data);

} // namespace tacetos::update
