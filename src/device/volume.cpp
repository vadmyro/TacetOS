// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "volume.h"

#include <QStorageInfo>

namespace tacetos::device {

FilesystemKind classifyFilesystem(const QString &osName)
{
    const QString name = osName.trimmed().toLower();
    if (name == QLatin1String("vfat") || name == QLatin1String("msdos") ||
        name == QLatin1String("fat32"))
        return FilesystemKind::Fat32;
    if (name == QLatin1String("hfsplus") || name == QLatin1String("hfs"))
        return FilesystemKind::HfsPlus;
    return FilesystemKind::Unsupported;
}

int musicVolumeRank(int partitionNumber, FilesystemKind kind)
{
    // Slot numbers are small, so the filesystem always weighs more than the slot.
    const int known = kind == FilesystemKind::Unsupported ? 1 : 0;
    const int slot = partitionNumber == kMusicVolumeSlot ? 0 : partitionNumber;
    return known * 1000 + slot;
}

void readVolumeSpace(VolumeInfo &volume)
{
    const QStorageInfo info(volume.mountPoint);
    if (!info.isValid() || !info.isReady())
        return;
    volume.totalBytes = info.bytesTotal();
    volume.freeBytes = info.bytesAvailable();
    volume.label = info.name();
}

} // namespace tacetos::device
