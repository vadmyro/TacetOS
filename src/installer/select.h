// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "recognise.h"
#include "sectordevice.h"

#include <QList>
#include <QString>

// Finding the iPod among the disks (docs/specs/bootloader-legacy.md, section
// 6). The list of disks comes from the caller, who got it from the system
// (removable, USB, opened read-only): nothing here asks the system for
// anything.

namespace tacetos::installer {

struct DiskCandidate
{
    // What the user is shown: the name of the disk (`/dev/sdb`) and a label.
    QString name;
    QString description;
    qint64 capacityBytes = 0;
    // Opened read-only, not owned. Null when the disk could not be opened.
    SectorDevice *device = nullptr;
    // The disk was refused for permission (counted apart from "not an iPod").
    bool permissionDenied = false;
};

struct FoundIpod
{
    // Position in the list given to `selectIpod`.
    int index = -1;
    QString name;
    QString description;
    qint64 capacityBytes = 0;
    Recognition recognition;
};

enum class SelectOutcome {
    None,             // "No iPod found. Is it in Disk Mode?"
    PermissionDenied, // every candidate was refused for permission, none recognised
    One,              // exactly one disk passes: it is the iPod
    Several,          // the user must pick one by name and capacity
};

struct Selection
{
    SelectOutcome outcome = SelectOutcome::None;
    // The disks that passed, in the order of the list. Never picked for the user.
    QList<FoundIpod> ipods;
    // iPods of a generation we do not handle: shown, never offered for install.
    QList<FoundIpod> unsupported;
    int permissionRefused = 0;
    // The message of section 23 for `None` and `PermissionDenied`; for `None`
    // with an unsupported iPod, the one for an unknown generation.
    QString message;
};

Selection selectIpod(const QList<DiskCandidate> &candidates);

} // namespace tacetos::installer
