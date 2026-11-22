// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "classicdfu.h"
#include "helpercalls.h"
#include "step.h"

#include <QString>

// The steps of the uninstall (docs/specs/install-wizard.md section 10):
// `remove-bootloader` of the older iPods, `classic-remove` of the iPod Classic,
// and `remove-firmware` of both. The settings are copied before by
// `backup-settings`, which is `BackupStep` with `BackupKind::Settings`.

namespace tacetos::wizard {

// What the steps are made from. The app gives the real ones, the tests their own.
class UninstallSources
{
public:
    virtual ~UninstallSources() = default;
    virtual HelperCommands &helper() const = 0;
    // The mount point of the music volume, asked when the step runs: an older
    // iPod gets a new one after Disk Mode.
    virtual QString volumeRoot() const = 0;
    // How long `remove-firmware` waits for the volume: the Classic comes back from
    // `classic-remove` before its volume is mounted.
    virtual qint64 volumeWaitMs() const = 0;
};

// Section 10, "Nothing to do" for an older iPod: `read-state` says `clean` and
// there is no firmware folder. The screen asks it before the flow starts.
bool legacyNothingToDo(const QString &state, const QString &volumeRoot);

// Section 10 step 2: `read-state`, `uninstall-bootloader`, `read-state` again,
// which must say `clean`. A `clean` iPod is done at once (a resume after a cut
// that came after the removal), unless it is not a resume and there is no
// firmware folder: then `nothing-to-do`. Facts: `record`, or `recordMessage`,
// and after a failed write the `next` of the helper.
class RemoveBootloaderStep : public Step
{
public:
    explicit RemoveBootloaderStep(const UninstallSources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    const UninstallSources &m_sources;
};

// Section 10 step 3: `classic-dfu` with `uninstall`, awaiting the return of
// the iPod with Apple's firmware. The same step as the install of section 9.
class ClassicRemoveStep : public ClassicDfuStep
{
public:
    explicit ClassicRemoveStep(const ClassicDfuSources &sources)
        : ClassicDfuStep(sources, ClassicOperation::Uninstall)
    {
    }
};

// Section 10 step 4: deletes the firmware folder, and the folders with the two
// suffixes of section 7.4, from the music volume. Only after the settings
// backup of the flow is verified. On a Classic it then writes the record of the
// uninstall in that backup (section 9.3).
class RemoveFirmwareStep : public Step
{
public:
    explicit RemoveFirmwareStep(const UninstallSources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    bool writeRecord(StepContext &context) const;

    const UninstallSources &m_sources;
};

} // namespace tacetos::wizard
