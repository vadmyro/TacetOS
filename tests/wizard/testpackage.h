// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "constants.h"
#include "testzip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

// A package of the firmware made by the tests of the wizard
// (firmware/README.md): a zip of stored files in the firmware folder, a
// bootloader file, and the manifest. None of it is a real build.

namespace tacetos::wizard::testing {

struct TestPackage
{
    QString manifest;
    QString sha256;
};

inline QString sha256Of(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// `files` are paths inside the firmware folder with their bytes.
inline TestPackage makeTestPackage(const QString &folder, const QString &target,
                                   const QByteArray &bootloader,
                                   const QMap<QString, QByteArray> &files)
{
    const QString top = QString(device::kFirmwareFolder) + QLatin1Char('/');
    QList<QPair<QByteArray, QByteArray>> entries{{top.toUtf8(), QByteArray()}};
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        entries.append({(top + it.key()).toUtf8(), it.value()});
    const QByteArray zip = storedZip(entries);
    const QString zipName = QStringLiteral("tacetos-%1-0.2.0.zip").arg(target);
    const QString bootName = QStringLiteral("tacetos-bootloader-%1-0.2.0.bin").arg(target);
    QDir().mkpath(folder);
    const auto write = [&folder](const QString &name, const QByteArray &bytes) {
        QFile f(QDir(folder).filePath(name));
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    };
    write(zipName, zip);
    write(bootName, bootloader);
    const auto entry = [](const QString &name, const char *kind, const QByteArray &bytes) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), name);
        o.insert(QStringLiteral("kind"), QLatin1String(kind));
        o.insert(QStringLiteral("size"), double(bytes.size()));
        o.insert(QStringLiteral("sha256"), sha256Of(bytes));
        return o;
    };
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), 1);
    manifest.insert(QStringLiteral("target"), target);
    manifest.insert(QStringLiteral("version"), QStringLiteral("0.2.0+gtest"));
    manifest.insert(QStringLiteral("files"), QJsonArray{entry(zipName, "firmware", zip),
                                                        entry(bootName, "bootloader", bootloader)});
    const QByteArray text = QJsonDocument(manifest).toJson();
    const QString name = QStringLiteral("tacetos-%1-0.2.0.json").arg(target);
    write(name, text);
    return {QDir(folder).filePath(name), sha256Of(text)};
}

} // namespace tacetos::wizard::testing
