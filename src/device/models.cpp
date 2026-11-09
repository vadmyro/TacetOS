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

        const TomlTable usb = table.children.value(QStringLiteral("usb"));
        if (!readUsbList(usb, QStringLiteral("normal"), m.id, &m.usbNormal, &list.error) ||
            !readUsbList(usb, QStringLiteral("disk_mode"), m.id, &m.usbDiskMode, &list.error) ||
            !readUsbList(usb, QStringLiteral("dfu"), m.id, &m.usbDfu, &list.error))
            return list;
        list.models.append(m);
    }
    return list;
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
