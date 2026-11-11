// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "firmware.h"

#include "constants.h"

#include <QRegularExpression>

namespace tacetos::device {

namespace {

// The value of the distribution line without the name in front of it.
QString withoutDistributionName(const QString &value)
{
    QString text = value.trimmed();
    const QString name = QString(kDistributionName);
    if (text.startsWith(name, Qt::CaseInsensitive))
        text = text.mid(name.size()).trimmed();
    return text;
}

} // namespace

QString appleFirmwareVersion(const QMap<QString, QString> &appleFields)
{
    static const QRegularExpression bracketed(QStringLiteral("\\(([0-9][0-9A-Za-z.]*)\\)"));
    const QString value = appleFields.value(QString(kSysInfoFirmwareKey));
    const QRegularExpressionMatch match = bracketed.match(value);
    return match.hasMatch() ? match.captured(1) : QString();
}

FirmwareLine identifyFirmware(const FirmwareInfo &files, const QMap<QString, QString> &appleFields,
                              QStringList *log)
{
    FirmwareLine line;
    // Files meant for another model are not this iPod's firmware (section 8.2
    // rule 4): they are not used for the line.
    const bool filesCount = !files.otherModel;

    if (filesCount && files.files == FirmwareFiles::Tacetos) {
        line.kind = FirmwareKind::Tacetos;
        line.version = withoutDistributionName(files.version);
    } else if (filesCount && files.files == FirmwareFiles::Present) {
        line.kind = FirmwareKind::Custom;
        line.version = files.infoVersion.trimmed();
    } else if (!appleFields.isEmpty()) {
        // Written by Apple's firmware only; the file is empty on the others.
        line.kind = FirmwareKind::Apple;
        line.version = appleFirmwareVersion(appleFields);
    }

    if (log) {
        static const char *const names[] = {"unknown", "apple", "custom", "tacetos"};
        QString text =
            QStringLiteral("firmware: %1").arg(QLatin1String(names[static_cast<int>(line.kind)]));
        if (!line.version.isEmpty())
            text += QLatin1Char(' ') + line.version;
        *log << text;
        if (files.otherModel && files.files != FirmwareFiles::Absent)
            *log << QStringLiteral("firmware: the files on the volume are for another model");
    }
    return line;
}

} // namespace tacetos::device
