// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"

#include <QString>

// The music volume (docs/specs/detection.md sections 6.2 and 6.3). Nothing
// here depends on the system that found the volume.

namespace tacetos::device {

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

// Section 6.1: the partition number that wins when a disk has several mounted
// volumes. The Classic has its only one at slot 1, the older models at slot 2
// (slot 1 is their firmware, never mounted).
inline constexpr int kMusicVolumeSlot = 2;

// Section 6.1: how good a mounted partition is as the music volume, lower is
// better. A known filesystem first, then slot 2, then the lowest number.
int musicVolumeRank(int partitionNumber, FilesystemKind kind);

// Section 6.3: fills the total and free bytes and the label (display only) of
// a volume from its mount point. Leaves the fields alone when the system
// cannot tell.
void readVolumeSpace(VolumeInfo &volume);

} // namespace tacetos::device
