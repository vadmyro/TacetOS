// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "format.h"
#include "helpercalls.h"
#include "step.h"
#include "wizardconstants.h"

#include <QString>

// The steps `disk-mode` and `bootloader` of the older iPods
// (docs/specs/install-wizard.md section 8).

namespace tacetos::wizard {

// What the steps are made from. The app gives the real ones, the tests their own.
class BootloaderSources
{
public:
    virtual ~BootloaderSources() = default;
    virtual HelperCommands &helper() const = 0;
    virtual VolumeWatch &volumes() const = 0;
    // The `bootloader_target` of the model of the iPod in `models.toml`.
    virtual QString target() const = 0;
    // Section 8.1: how long the iPod has to be ready again after the button:
    // the restart, then its volume.
    virtual qint64 returnMs() const { return kRestartWindowMs + kVolumeReturnMs; }
};

// Section 8.1: waits for the button "The iPod is in Disk Mode"
// (`WaitKind::DiskMode`, with the `model`), believes it (D27), then waits for
// the iPod to be ready.
class DiskModeStep : public Step
{
public:
    explicit DiskModeStep(const BootloaderSources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    const BootloaderSources &m_sources;
};

// Section 8.2: installs the bootloader of the package on a clean iPod, or
// updates it on an installed one. Facts: `record`, the path of the install
// record, or `recordMessage`, why it could not be written. After a failed
// write, `next` is what the helper said to do (`offer-retry`, `restored`,
// `restore-failed`...). After a cut (`isResume`): `installed` is done, `clean`
// runs again, `inconsistent` offers the backup (`WaitKind::Offer`, with `offer`
// = `restore-backup` and the `backup`), written back only after the yes.
class BootloaderStep : public Step
{
public:
    explicit BootloaderStep(const BootloaderSources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    StepOutcome offerRestore(StepContext &context, const QString &folder, const QString &failure);
    StepOutcome failedWrite(StepContext &context, const QString &folder,
                            const HelperAnswer &result);

    const BootloaderSources &m_sources;
};

} // namespace tacetos::wizard
