// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "models.h"

#include <QList>
#include <QString>

// The text of every state (docs/specs/detection.md section 10). Short, in the
// words of the app, translatable (context "device").

namespace tacetos::device {

enum class HostSystem { Linux, Windows, MacOs };

// The system this program was built for.
HostSystem currentHostSystem();

// The line that follows the `permission` message: what to do on this system.
QString permissionHint(HostSystem system);

// The message of the state of `ipod`. `models` gives the name shown for the
// `ready` state. For `permission` the line of `permissionHint` is added.
QString stateMessage(const DetectedIpod &ipod, const QList<Model> &models,
                     HostSystem system = currentHostSystem());

// Section 7.6 and 10: the firmware line ("Apple firmware 1.1.6", "Custom
// firmware 4.0", "TacetOS 0.3", or "Firmware unknown"). It only tells what the
// files on the volume say, never that a bootloader is installed.
QString firmwareMessage(const DetectedIpod &ipod);

} // namespace tacetos::device
