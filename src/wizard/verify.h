// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "constants.h"
#include "detected.h"
#include "helpercalls.h"
#include "step.h"

#include <QString>

// The end of a flow (docs/specs/install-wizard.md section 11): `verify` reads
// the result again from the iPod, `finish` keeps what the last screen shows.

namespace tacetos::wizard {

// What the two steps are made from. The app gives the real ones, the tests
// their own.
class VerifySources
{
public:
    virtual ~VerifySources() = default;
    virtual HelperCommands &helper() const = 0;
    // The mount point of the music volume.
    virtual QString volumeRoot() const = 0;
    // The `bootloader_target` of the model of the iPod in `models.toml`.
    virtual QString target() const = 0;
    // The hidden folder of the firmware of this model (`firmwareFolder` of its
    // model in `models.toml`).
    virtual QString firmwareFolder() const { return QString(device::kFirmwareFolder); }
    // The iPod of the flow as detection shows it now; empty `serial` when it
    // shows none.
    virtual device::DetectedIpod detected() const = 0;
    // How long the step waits for the iPod to be `ready`: after a restart it is
    // seen before its volume is mounted.
    virtual qint64 readyWaitMs() const = 0;
    // Update section 6: the folder of the downloads, `updates/` in the data folder.
    // Empty: nothing to clean up.
    virtual QString updatesFolder() const { return QString(); }
};

// Section 11.1. Reads everything again, from the iPod and not from the facts of
// the other steps, and writes nothing. A difference fails with `verify-failed`
// and what differs.
class VerifyStep : public Step
{
public:
    explicit VerifyStep(const VerifySources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    const VerifySources &m_sources;
};

// Section 11.2. Facts: `backup` (the backup folder of the flow), `version` (the
// package of an install or an update), `settings` (the folder where an uninstall
// saved the settings). The runner gives them to the last screen with
// `flowFinished`. After an update, the downloads of the older versions are
// deleted.
class FinishStep : public Step
{
public:
    explicit FinishStep(const VerifySources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    const VerifySources &m_sources;
};

} // namespace tacetos::wizard
