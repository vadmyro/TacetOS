// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "models.h"
#include "screenstate.h"

#include <QList>
#include <QString>
#include <optional>

// The text of every state (docs/specs/detection.md section 10). Short, in the
// words of the app, translatable (context "device").

namespace tacetos::device {

enum class HostSystem { Linux, Windows, MacOs };

// The system this program was built for.
HostSystem currentHostSystem();

// The line that follows the `permission` message: what to do on this system.
QString permissionHint(HostSystem system);

// The name of an iPod (section 8.4, "What is shown"): the variant with its
// release period ("iPod Classic 120 GB (late 2008)"), else the name of the
// model, else empty. The `ready` message and the card of the home screen start
// with it.
QString ipodName(const DetectedIpod &ipod, const QList<Model> &models);

// The message of the state of `ipod`. `models` gives the name shown for the
// `ready` state. For `permission` the line of `permissionHint` is added.
QString stateMessage(const DetectedIpod &ipod, const QList<Model> &models,
                     HostSystem system = currentHostSystem());

// docs/specs/device-state.md section 1.4: the message of a displayed state.
// `ipod` is the detected iPod of the entry, when there is one. Busy and done
// follow the operation, failed follows the error of the service, `released`,
// `rebooting`, `unplugged` and `not-mounted` (where the app can mount) have
// their own text; every other state gives the message of detection
// (`stateMessage`).
QString screenMessage(const ScreenState &state, const std::optional<DetectedIpod> &ipod,
                      const QList<Model> &models, HostSystem system = currentHostSystem());

// Section 7.6 and 10: the firmware line ("Apple firmware 1.1.6", "Custom
// firmware 4.0", "TacetOS 0.3"). Empty when the kind is unknown: the card hides
// the line. It never says that a bootloader is installed.
QString firmwareMessage(const DetectedIpod &ipod);

} // namespace tacetos::device
