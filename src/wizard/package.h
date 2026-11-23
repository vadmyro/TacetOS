// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "constants.h"
#include "diagnose.h"

#include <QString>

// The package of the firmware that a flow installs
// (docs/specs/install-wizard.md section 7.1, the format in firmware/README.md).

namespace tacetos::wizard {

// Where the files of a checked package are.
struct PackageFiles
{
    QString manifest;
    // The zip of the firmware.
    QString firmware;
    QString bootloader;
    QString version;
};

// Section 7.1: the five checks, in their order, before anything is written to
// the iPod. `target` is the `bootloader_target` of the model of the iPod in
// `models.toml`, `firmwareFolder` its firmware folder (`firmwareFolder`). `files` is
// filled when the package can be used.
PackageCheck checkPackage(const QString &manifestPath, const QString &target,
                          const QString &firmwareFolder, PackageFiles *files = nullptr);

// Section 7.1: a flow installs the package it started with. A check that
// passed becomes `package-changed` when the sha256 of the manifest is not
// `flowSha256`, the one of the progress file (empty: nothing to compare).
PackageCheck sameAsFlow(PackageCheck check, const QString &flowSha256);

} // namespace tacetos::wizard
