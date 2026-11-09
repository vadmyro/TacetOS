// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

// Qt has no TOML reader, so this is a small one that understands only what
// models/models.toml uses: comments, [table] and [[model]] headers, and
// single-line values (string, integer, float, boolean, date, array).

namespace tacetos::device {

struct TomlTable
{
    QMap<QString, QVariant> values;
    QMap<QString, TomlTable> children;
};

struct TomlDocument
{
    TomlTable root;
    // One table per [[model]] header, in file order. [model.*] sub-tables are
    // the children of the last one.
    QList<TomlTable> models;
    // Empty when the text was read without a problem.
    QString error;
};

TomlDocument parseTomlText(const QByteArray &text);
TomlDocument parseTomlFile(const QString &path);

} // namespace tacetos::device
