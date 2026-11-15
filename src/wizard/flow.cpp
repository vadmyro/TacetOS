// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "flow.h"

namespace tacetos::wizard {

QStringList stepsFor(const FlowShape &shape)
{
    const bool older = shape.family == Family::Partition;
    QStringList steps{QString(step::diagnose)};

    if (shape.kind == FlowKind::Uninstall) {
        if (shape.path != InstallPath::None)
            return {};
        steps << QString(step::backupSettings);
        if (older)
            steps << QString(step::diskMode) << QString(step::removeBootloader);
        else
            steps << QString(step::classicRemove);
        steps << QString(step::removeFirmware) << QString(step::verify) << QString(step::finish);
        return steps;
    }

    switch (shape.path) {
    case InstallPath::None:
        return {};
    case InstallPath::Keep:
        // Rule 2 of section 2.2: the conversion of a Mac iPod erases it.
        if (shape.macFormat)
            return {};
        steps << QString(step::backupKeep);
        if (older)
            steps << QString(step::diskMode);
        break;
    case InstallPath::Clean:
        steps << QString(step::backupFull);
        // Disk Mode is asked once, before the first write of sectors.
        if (older)
            steps << QString(step::diskMode);
        steps << QString(shape.macFormat ? step::convert : step::format);
        break;
    }
    steps << QString(step::firmware) << QString(older ? step::bootloader : step::classicBootloader)
          << QString(step::verify) << QString(step::finish);
    return steps;
}

QString familyName(Family family)
{
    return family == Family::Partition ? QStringLiteral("partition") : QStringLiteral("dfu");
}

QString kindName(FlowKind kind)
{
    return kind == FlowKind::Install ? QStringLiteral("install") : QStringLiteral("uninstall");
}

QString pathName(InstallPath path)
{
    switch (path) {
    case InstallPath::Clean:
        return QStringLiteral("clean");
    case InstallPath::Keep:
        return QStringLiteral("keep");
    case InstallPath::None:
        break;
    }
    return QString();
}

std::optional<Family> familyFromName(const QString &name)
{
    if (name == QLatin1String("partition"))
        return Family::Partition;
    if (name == QLatin1String("dfu"))
        return Family::Dfu;
    return std::nullopt;
}

std::optional<FlowKind> kindFromName(const QString &name)
{
    if (name == QLatin1String("install"))
        return FlowKind::Install;
    if (name == QLatin1String("uninstall"))
        return FlowKind::Uninstall;
    return std::nullopt;
}

std::optional<InstallPath> pathFromName(const QString &name)
{
    if (name == QLatin1String("clean"))
        return InstallPath::Clean;
    if (name == QLatin1String("keep"))
        return InstallPath::Keep;
    return std::nullopt;
}

} // namespace tacetos::wizard
