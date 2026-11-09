// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"

#include <QString>

// The music volume (docs/specs/detection.md sections 6.2 and 6.3). Nothing
// here depends on the system that found the volume.

namespace tacetos::device {

// The partition number of the music volume, as the system numbers it (section 6.1).
inline constexpr int kMusicVolumeSlot = 2;

enum class FilesystemKind {
    // A Windows iPod.
    Fat32,
    // A Mac iPod. TacetOS still needs FAT32 (section 6.2).
    HfsPlus,
    // Kept in the result, but nothing is read from it.
    Unsupported,
};

// Section 6.2: what the name of a filesystem, as one OS writes it (`vfat`,
// `msdos`, `hfsplus`, `hfs`, `FAT32`, `HFS`), means. Case does not matter.
FilesystemKind classifyFilesystem(const QString &osName);

// Section 6.3: fills the total and free bytes and the label (display only) of
// a volume from its mount point. Leaves the fields alone when the system
// cannot tell.
void readVolumeSpace(VolumeInfo &volume);

} // namespace tacetos::device
