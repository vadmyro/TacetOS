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

QString modelName(const QString &id, const QList<Model> &models)
{
    for (const Model &model : models) {
        if (model.id == id)
            return model.name;
    }
    return id;
}

QString sizeText(qint64 bytes)
{
    return tr("%1 GB").arg(std::llround(static_cast<double>(bytes) / 1e9));
}

bool hasVariantName(const DetectedIpod &ipod)
{
    return ipod.variant.source != VariantSource::None && !ipod.variant.name.isEmpty();
}

// Section 8.4, "What is shown": the variant with its release period, or the
// one name left by the capacity, else the model and the real size.
QString readyMessage(const DetectedIpod &ipod, const QList<Model> &models)
{
    const qint64 bytes =
        ipod.disk ? ipod.disk->sizeBytes : (ipod.volume ? ipod.volume->totalBytes : 0);
    const VariantMatch &variant = ipod.variant;

    QStringList parts;
    if (hasVariantName(ipod)) {
        parts << ipodName(ipod, models);
        // The name carries the capacity of the box: show the real size only if
        // the storage is not the original one.
        if (variant.storageChanged && bytes > 0)
            parts << sizeText(bytes);
    } else {
        parts << modelName(ipod.model, models);
        if (bytes > 0)
            parts << sizeText(bytes);
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

// Section 8.4 rule 3: the question, with the hint of the disk size when one
// candidate fits it.
QString ambiguousMessage(const DetectedIpod &ipod, const QList<Model> &models)
{
    QString text = tr("Several iPod models share this connection. Which one is yours?");
    if (!ipod.hintModel.isEmpty() && ipod.disk && ipod.disk->sizeBytes > 0) {
        text += QLatin1Char(' ') +
                tr("A disk of %1 is usually the %2.")
                    .arg(sizeText(ipod.disk->sizeBytes), modelName(ipod.hintModel, models));
    }
    return text;
}

} // namespace

QString ipodName(const DetectedIpod &ipod, const QList<Model> &models)
{
    if (hasVariantName(ipod)) {
        const VariantMatch &variant = ipod.variant;
        return variant.released.isEmpty() ? variant.name
                                          : tr("%1 (%2)").arg(variant.name, variant.released);
    }
    for (const Model &model : models) {
        if (model.id == ipod.model && !model.name.isEmpty())
            return model.name;
    }
    return QString();
}

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
        return ambiguousMessage(ipod, models);
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

QString screenMessage(const ScreenState &state, const std::optional<DetectedIpod> &ipod,
                      const QList<Model> &models, HostSystem system)
{
    // The detected iPod of the entry, or a bare one that only carries the state.
    auto detected = [&](IpodState detection) {
        DetectedIpod copy = ipod.value_or(DetectedIpod());
        copy.state = detection;
        return copy;
    };

    switch (state.kind) {
    case ScreenKind::Unplugged:
        return tr("Connect your iPod.");
    case ScreenKind::Rebooting:
        return tr("The iPod is restarting. Wait...");
    case ScreenKind::Released:
        return tr("You can unplug the iPod now.");
    case ScreenKind::Busy:
        switch (state.operation) {
        case OperationKind::Mount:
            return tr("Opening the iPod...");
        case OperationKind::MountRo:
            return tr("Opening the iPod, for reading only...");
        case OperationKind::Unmount:
            return tr("Closing the iPod...");
        case OperationKind::Eject:
            break;
        }
        return tr("Getting the iPod ready to unplug...");
    case ScreenKind::Done:
        switch (state.operation) {
        case OperationKind::Mount:
        case OperationKind::MountRo:
            return tr("The iPod is open.");
        case OperationKind::Unmount:
            return tr("The iPod is closed.");
        case OperationKind::Eject:
            break;
        }
        // A good eject leaves `released`, never `done`.
        return tr("You can unplug the iPod now.");
    case ScreenKind::Failed:
        switch (state.error) {
        case DiskError::NotAuthorized:
            return tr("The system did not allow it. Try again and accept the permission request.");
        case DiskError::Busy:
            return tr("Another program is using the iPod. Close it, then try again.");
        case DiskError::NoVolume:
            return tr("The iPod has no volume that can be mounted.");
        case DiskError::ServiceMissing:
            return tr("The disk service is not running. Install it, then try again.");
        case DiskError::NotAnIpodDisk:
            return tr("This disk is not an iPod that the system lets us handle.");
        case DiskError::None:
        case DiskError::Failed:
            break;
        }
        return tr("The system could not do it.");
    case ScreenKind::NotMounted:
        if (system == HostSystem::Linux) {
            return tr("The iPod storage is there but is not open on this computer. TacetOS can "
                      "open it.");
        }
        return stateMessage(detected(IpodState::NoVolume), models, system);
    case ScreenKind::Attention:
        return stateMessage(detected(state.reason), models, system);
    case ScreenKind::Reading:
        return stateMessage(detected(IpodState::Waiting), models, system);
    case ScreenKind::Dfu:
        return stateMessage(detected(IpodState::Dfu), models, system);
    case ScreenKind::Ready:
    case ScreenKind::DiskMode:
        break;
    }
    return ipod ? stateMessage(*ipod, models, system) : QString();
}

QString firmwareMessage(const DetectedIpod &ipod)
{
    const FirmwareLine &line = ipod.firmwareLine;
    switch (line.kind) {
    case FirmwareKind::Apple:
        return line.version.isEmpty() ? tr("Apple firmware")
                                      : tr("Apple firmware %1").arg(line.version);
    case FirmwareKind::Custom:
        return line.version.isEmpty() ? tr("Custom firmware")
                                      : tr("Custom firmware %1").arg(line.version);
    case FirmwareKind::Tacetos:
        return line.version.isEmpty() ? tr("TacetOS") : tr("TacetOS %1").arg(line.version);
    case FirmwareKind::Unknown:
        break;
    }
    // Section 7.6: nothing to say, the card hides the line.
    return QString();
}

} // namespace tacetos::device
