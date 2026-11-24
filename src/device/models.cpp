// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "models.h"

#include <QRegularExpression>
#include <QSet>

namespace tacetos::device {

namespace {

// Reads usb.<key>. A missing key or an empty list gives an empty result.
bool readUsbList(const TomlTable &usb, const QString &key, const QString &modelId, QStringList *out,
                 QString *error)
{
    static const QRegularExpression usbRe(QStringLiteral("^[0-9a-f]{4}:[0-9a-f]{4}$"));
    for (const QVariant &v : usb.values.value(key).toList()) {
        const QString id = v.toString();
        if (!usbRe.match(id).hasMatch()) {
            *error = QStringLiteral("%1: bad usb id '%2' in usb.%3").arg(modelId, id, key);
            return false;
        }
        out->append(id);
    }
    return true;
}

} // namespace

ModelList modelsFromDocument(const TomlDocument &doc)
{
    ModelList list;
    if (!doc.error.isEmpty()) {
        list.error = doc.error;
        return list;
    }

    QSet<QString> seen;
    for (const TomlTable &table : doc.models) {
        Model m;
        m.id = table.values.value(QStringLiteral("id")).toString();
        if (m.id.isEmpty()) {
            list.error = QStringLiteral("a [[model]] has no id");
            return list;
        }
        if (seen.contains(m.id)) {
            list.error = QStringLiteral("%1: id used twice").arg(m.id);
            return list;
        }
        seen.insert(m.id);
        m.family = table.values.value(QStringLiteral("family")).toString();
        m.name = table.values.value(QStringLiteral("name")).toString();
        m.bootloaderTarget = table.values.value(QStringLiteral("bootloader_target")).toString();
        const QVariant folder = table.values.value(QStringLiteral("firmware_folder"));
        if (folder.isValid()) {
            static const QRegularExpression folderRe(QStringLiteral("^\\.[A-Za-z0-9_-]+$"));
            if (!folderRe.match(folder.toString()).hasMatch()) {
                list.error =
                    QStringLiteral("%1: bad firmware_folder '%2'").arg(m.id, folder.toString());
                return list;
            }
            m.firmwareFolder = folder.toString();
        }

        const TomlTable usb = table.children.value(QStringLiteral("usb"));
        if (!readUsbList(usb, QStringLiteral("normal"), m.id, &m.usbNormal, &list.error) ||
            !readUsbList(usb, QStringLiteral("disk_mode"), m.id, &m.usbDiskMode, &list.error) ||
            !readUsbList(usb, QStringLiteral("dfu"), m.id, &m.usbDfu, &list.error))
            return list;

        const TomlTable screen = table.children.value(QStringLiteral("screen"));
        const int width = screen.values.value(QStringLiteral("width")).toInt();
        const int height = screen.values.value(QStringLiteral("height")).toInt();
        if (width > 0 && height > 0)
            m.screen =
                ModelScreen{width, height, screen.values.value(QStringLiteral("colour")).toBool()};

        const TomlTable variants = table.children.value(QStringLiteral("variant"));
        for (auto it = variants.children.cbegin(); it != variants.children.cend(); ++it) {
            Variant v;
            v.key = it.key();
            v.name = it.value().values.value(QStringLiteral("name")).toString();
            v.capacityGb = it.value().values.value(QStringLiteral("capacity_gb")).toInt();
            v.released = it.value().values.value(QStringLiteral("released")).toString();
            for (const QVariant &number :
                 it.value().values.value(QStringLiteral("apple_models")).toList())
                v.appleModels.append(number.toString().trimmed().toUpper());
            m.variants.append(v);
        }
        list.models.append(m);
    }
    return list;
}

QStringList firmwareFolders(const QList<Model> &models)
{
    QStringList folders;
    for (const Model &m : models) {
        if (m.firmwareFolder.compare(kFirmwareFolder, Qt::CaseInsensitive) != 0 &&
            !folders.contains(m.firmwareFolder, Qt::CaseInsensitive))
            folders << m.firmwareFolder;
    }
    folders << QString(kFirmwareFolder);
    return folders;
}

ModelList loadModels(const QString &path)
{
    return modelsFromDocument(parseTomlFile(path));
}

UsbMatch matchUsbId(const QList<Model> &models, const QString &usbId)
{
    const QString id = usbId.trimmed().toLower();
    bool inNormal = false;
    bool inDfu = false;
    UsbMatch match;

    for (const Model &m : models) {
        const bool normal = m.usbNormal.contains(id) || m.usbDiskMode.contains(id);
        const bool dfu = m.usbDfu.contains(id);
        if (!normal && !dfu)
            continue;
        inNormal = inNormal || normal;
        inDfu = inDfu || dfu;
        match.candidates.append(m.id);
    }

    if (inNormal && inDfu)
        match.mode = UsbMode::Other;
    else if (inNormal)
        match.mode = UsbMode::Normal;
    else if (inDfu)
        match.mode = UsbMode::Dfu;
    return match;
}

} // namespace tacetos::device
