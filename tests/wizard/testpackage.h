// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "constants.h"
#include "testzip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

// `files` are paths inside the firmware folder with their bytes. `firmwareFolder`
// is the top folder of the zip. `version` names the files and is the version of
// the manifest; empty: the package of 0.2.0 that every older test uses.
inline TestPackage makeTestPackage(const QString &folder, const QString &target,
                                   const QByteArray &bootloader,
                                   const QMap<QString, QByteArray> &files,
                                   const QString &firmwareFolder = QString(device::kFirmwareFolder),
                                   const QString &version = QString())
{
    const QString named = version.isEmpty() ? QStringLiteral("0.2.0") : version;
    const QString top = firmwareFolder + QLatin1Char('/');
    QList<QPair<QByteArray, QByteArray>> entries{{top.toUtf8(), QByteArray()}};
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        entries.append({(top + it.key()).toUtf8(), it.value()});
    const QByteArray zip = storedZip(entries);
    const QString zipName = QStringLiteral("tacetos-%1-%2.zip").arg(target, named);
    const QString bootName = QStringLiteral("tacetos-bootloader-%1-%2.bin").arg(target, named);
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
    manifest.insert(QStringLiteral("version"),
                    version.isEmpty() ? QStringLiteral("0.2.0+gtest") : version);
    manifest.insert(QStringLiteral("files"), QJsonArray{entry(zipName, "firmware", zip),
                                                        entry(bootName, "bootloader", bootloader)});
    const QByteArray text = QJsonDocument(manifest).toJson();
    const QString name = QStringLiteral("tacetos-%1-%2.json").arg(target, named);
    write(name, text);
    return {QDir(folder).filePath(name), sha256Of(text)};
}

// The index of a channel (update-channel.md section 3) in `folder`, which then
// is a local channel: one entry per package, each with the size and sha256 of
// its manifest as the file is now.
inline QString writeTestIndex(const QString &folder, const QString &version,
                              const QMap<QString, TestPackage> &byTarget)
{
    QJsonArray packages;
    for (auto it = byTarget.cbegin(); it != byTarget.cend(); ++it) {
        QFile manifest(it.value().manifest);
        if (!manifest.open(QIODevice::ReadOnly))
            return QString();
        const QByteArray bytes = manifest.readAll();
        QJsonObject entry;
        entry.insert(QStringLiteral("target"), it.key());
        entry.insert(QStringLiteral("manifest"), QFileInfo(it.value().manifest).fileName());
        entry.insert(QStringLiteral("size"), double(bytes.size()));
        entry.insert(QStringLiteral("sha256"), sha256Of(bytes));
        packages.append(entry);
    }
    QJsonObject index;
    index.insert(QStringLiteral("format"), 1);
    index.insert(QStringLiteral("version"), version);
    index.insert(QStringLiteral("built"), QStringLiteral("2026-11-26T12:00:00Z"));
    index.insert(QStringLiteral("packages"), packages);
    const QString path = QDir(folder).filePath(QStringLiteral("tacetos-index.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(index).toJson()) <= 0)
        return QString();
    return path;
}

} // namespace tacetos::wizard::testing
