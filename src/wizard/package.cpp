// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "package.h"

#include "backup.h"
#include "constants.h"
#include "wizardconstants.h"
#include "wizarderrors.h"
#include "zipreader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tacetos::wizard {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("tacetos::wizard::Package", text);
}

PackageCheck refused(QLatin1String code, const QString &message)
{
    PackageCheck check;
    check.code = QString(code);
    check.message = message;
    return check;
}

PackageCheck wrongFormat()
{
    return refused(error::packageFormat, tr("This package is for another version of TacetOS"));
}

PackageCheck incomplete()
{
    return refused(error::packageIncomplete, tr("This package is incomplete"));
}

PackageCheck damaged()
{
    return refused(error::packageDamaged, tr("This package is damaged. Download it again"));
}

// Check 3: a file name with no folder part.
bool plainName(const QString &name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) &&
           !name.contains(QLatin1Char('\\')) && name != QLatin1String(".") &&
           name != QLatin1String("..");
}

struct Listed
{
    QString name;
    QString path;
    qint64 size = -1;
    QString sha256;
};

} // namespace

PackageCheck checkPackage(const QString &manifestPath, const QString &target, PackageFiles *files)
{
    // 1. A JSON object of at most `kPackageManifestMax`, of this format.
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
        return incomplete();
    const QByteArray bytes = file.read(kPackageManifestMax + 1);
    if (bytes.size() > kPackageManifestMax)
        return wrongFormat();
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    const QJsonObject manifest = doc.object();
    const QJsonValue format = manifest.value(QStringLiteral("format"));
    if (!doc.isObject() || !format.isDouble() || format.toDouble() != kPackageFormat)
        return wrongFormat();

    // 2. The model of the iPod.
    if (target.isEmpty() || manifest.value(QStringLiteral("target")).toString() != target)
        return refused(error::packageWrongModel, tr("This package is for another iPod model"));

    // 3. Exactly one firmware and one bootloader, next to the manifest.
    const QJsonArray list = manifest.value(QStringLiteral("files")).toArray();
    if (list.size() != 2)
        return incomplete();
    Listed firmware;
    Listed bootloader;
    const QDir folder = QFileInfo(manifestPath).absoluteDir();
    for (const QJsonValue &value : list) {
        const QJsonObject entry = value.toObject();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        Listed *listed = kind == QLatin1String("firmware")     ? &firmware
                         : kind == QLatin1String("bootloader") ? &bootloader
                                                               : nullptr;
        if (listed == nullptr || !listed->name.isEmpty())
            return incomplete();
        listed->name = entry.value(QStringLiteral("name")).toString();
        if (!plainName(listed->name))
            return incomplete();
        listed->path = folder.filePath(listed->name);
        const QJsonValue size = entry.value(QStringLiteral("size"));
        listed->size = size.isDouble() ? qint64(size.toDouble()) : -1;
        listed->sha256 = entry.value(QStringLiteral("sha256")).toString().toLower();
        if (!QFileInfo(listed->path).isFile())
            return incomplete();
    }

    // 4. Each file as the manifest says.
    for (const Listed *listed : {&firmware, &bootloader}) {
        if (QFileInfo(listed->path).size() != listed->size ||
            fileSha256(listed->path) != listed->sha256)
            return damaged();
    }

    // 5. A zip our reader takes, all in one top folder: the firmware folder.
    archive::ZipReader zip(firmware.path);
    if (!zip.open({kZipEntryMax, kZipUnpackMax}))
        return damaged();
    for (const archive::ZipEntry &entry : zip.entries()) {
        const bool inside = entry.name.contains(QLatin1Char('/')) &&
                            entry.name.section(QLatin1Char('/'), 0, 0)
                                    .compare(device::kFirmwareFolder, Qt::CaseInsensitive) == 0;
        if (!inside)
            return damaged();
    }

    PackageCheck check;
    check.unpackedBytes = zip.unpackedSize();
    check.manifestSha256 =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (files) {
        files->manifest = manifestPath;
        files->firmware = firmware.path;
        files->bootloader = bootloader.path;
        files->version = manifest.value(QStringLiteral("version")).toString();
    }
    return check;
}

PackageCheck sameAsFlow(PackageCheck check, const QString &flowSha256)
{
    if (check.code.isEmpty() && !flowSha256.isEmpty() && check.manifestSha256 != flowSha256) {
        check.code = QString(error::packageChanged);
        check.message = tr("This package changed on this computer since the install started. "
                           "Start again");
    }
    return check;
}

} // namespace tacetos::wizard
