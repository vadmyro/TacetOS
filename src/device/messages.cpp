// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "messages.h"

#include "volume.h"

#include <QCoreApplication>
#include <cmath>

namespace tacetos::device {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("device", text);
}

QString readyMessage(const DetectedIpod &ipod, const QList<Model> &models)
{
    QString name = ipod.model;
    for (const Model &model : models) {
        if (model.id == ipod.model)
            name = model.name;
    }

    QStringList parts{name};
    const qint64 bytes =
        ipod.disk ? ipod.disk->sizeBytes : (ipod.volume ? ipod.volume->totalBytes : 0);
    if (bytes > 0) {
        const qint64 gigabytes = std::llround(static_cast<double>(bytes) / 1e9);
        parts << tr("%1 GB").arg(gigabytes);
    }
    if (ipod.volume) {
        switch (classifyFilesystem(ipod.volume->filesystem)) {
        case FilesystemKind::Fat32:
            parts << tr("Windows format");
            break;
        case FilesystemKind::HfsPlus:
            parts << tr("Mac format");
            break;
        case FilesystemKind::Unsupported:
            break;
        }
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

HostSystem currentHostSystem()
{
#if defined(Q_OS_WIN)
    return HostSystem::Windows;
#elif defined(Q_OS_MACOS)
    return HostSystem::MacOs;
#else
    return HostSystem::Linux;
#endif
}

QString permissionHint(HostSystem system)
{
    switch (system) {
    case HostSystem::Windows:
        return tr("Start TacetOS again and accept the administrator prompt.");
    case HostSystem::MacOs:
        return tr("Allow TacetOS in the disk access setting of the system.");
    case HostSystem::Linux:
        break;
    }
    return tr("Add your user to the group that may read disks, then sign in again.");
}

QString stateMessage(const DetectedIpod &ipod, const QList<Model> &models, HostSystem system)
{
    switch (ipod.state) {
    case IpodState::Waiting:
        return tr("Reading the iPod...");
    case IpodState::NoDisk:
        return tr("The iPod is connected but does not show its storage. Turn on disk use in the "
                  "software of "
                  "the iPod, or put the iPod in Disk Mode.");
    case IpodState::NoVolume:
        return tr("The iPod storage is there but is not open on this computer. Open it from the "
                  "file manager, "
                  "then press Refresh.");
    case IpodState::MacFormat:
        return tr(
            "This iPod was formatted for a Mac. TacetOS needs the Windows format: the iPod can be "
            "converted (this erases the music on it). If the system offers to format the disk, "
            "answer No.");
    case IpodState::UnsupportedFilesystem:
        return tr("The iPod uses a filesystem TacetOS cannot read.");
    case IpodState::Ambiguous:
        return tr("Several iPod models share this connection. Which one is yours?");
    case IpodState::Conflict:
        return tr(
            "The iPod and its files do not agree on the model. TacetOS will not change anything.");
    case IpodState::Permission:
        return tr("TacetOS may not read the disk of the iPod.") + QLatin1Char(' ') +
               permissionHint(system);
    case IpodState::UnsupportedModel:
        return tr("This iPod is not supported.");
    case IpodState::Dfu:
        return tr("The iPod is in DFU mode.");
    case IpodState::Ready:
        return readyMessage(ipod, models);
    }
    return QString();
}

} // namespace tacetos::device
