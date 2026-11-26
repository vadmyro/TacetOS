// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "address.h"
#include "constants.h"
#include "step.h"

#include <QString>

// The step `download` of an update (docs/specs/update-channel.md section 6): the
// package of the model of the iPod, from the channel to the data folder, each
// file checked from the index down.

namespace tacetos::wizard {

class DownloadSources
{
public:
    virtual ~DownloadSources() = default;
    // The `bootloader_target` of the model of the iPod in `models.toml`.
    virtual QString target() const = 0;
    // The version of TacetOS on the iPod now, as the line of its info file gives
    // it with no commit: `0.2.0`, or `0.3.0-dev` for a development build.
    virtual QString ipodVersion() const = 0;
    virtual update::ChannelAddress channel() const = 0;
    // `updates/` in the data folder of the app.
    virtual QString updatesFolder() const = 0;
    // The free bytes of a folder of the computer, -1 when not known.
    virtual qint64 freeBytes(const QString &folder) const = 0;
    // How long a read may get no byte.
    virtual qint64 channelTimeoutMs() const { return device::kChannelTimeoutMs; }
};

// Reads the index again, checks the space, then downloads the manifest and its
// two files into `<updates>/<version>/<target>/`, each one as `.part` first. A
// file already there with its size and sha256 is kept. The manifest becomes the
// package of the flow (`StepContext::setPackage`). Fails with `update-gone`,
// `download-space`, `download-damaged` or `download-failed`.
class DownloadStep : public Step
{
public:
    explicit DownloadStep(const DownloadSources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

private:
    const DownloadSources &m_sources;
};

// The folder of the package of one model and one version.
QString downloadFolder(const QString &updatesFolder, const QString &version, const QString &target);

// After an update: deletes the folder of every version in `updatesFolder` but
// `keep`.
void removeOlderUpdates(const QString &updatesFolder, const QString &keep);

} // namespace tacetos::wizard
