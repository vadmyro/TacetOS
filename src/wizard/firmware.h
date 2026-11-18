// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "filefault.h"
#include "step.h"

#include <QString>

// The step `firmware`: the firmware of the package put in the firmware folder
// of the music volume, keeping the user's files (docs/specs/install-wizard.md
// sections 7.2 to 7.5).

namespace tacetos::wizard {

// What the step is made from. The app gives the real ones, the tests their own.
class FirmwareSources
{
public:
    virtual ~FirmwareSources() = default;
    // The mount point of the music volume.
    virtual QString volumeRoot() const = 0;
    // The `bootloader_target` of the model of the iPod in `models.toml`.
    virtual QString target() const = 0;
    // The free bytes of the music volume, -1 when not known.
    virtual qint64 freeBytes() const = 0;
    // Section 7.3: the newest `settings` backup of this serial, offered when
    // the iPod has no firmware folder. Empty when there is none.
    virtual QString settingsBackup() const { return QString(); }
    // For the tests: a write to the volume that fails as a full disk.
    virtual installer::FileFault fault() const { return {}; }
};

// Section 7.4. Facts: `swapped` once the new folder took the place of the old
// one (section 7.5, last row).
class FirmwareStep : public Step
{
public:
    explicit FirmwareStep(const FirmwareSources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    const FirmwareSources &m_sources;
};

// Section 11.1: the part of the check of section 7.4 step 6 that needs no
// memory of the install. Every file of the firmware zip is in the firmware
// folder of `volumeRoot` with its size and CRC-32, and the info file names
// `target`. Empty when it all matches, else what differs.
QString firmwareFolderDifference(const QString &volumeRoot, const QString &firmwareZip,
                                 const QString &target);

} // namespace tacetos::wizard
