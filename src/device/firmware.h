// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"

#include <QMap>
#include <QString>
#include <QStringList>

// The firmware line (docs/specs/detection.md section 7.6). Pure functions over
// what the marker files said: nothing here reads a system.

namespace tacetos::device {

// The version of Apple's firmware from the fields of the Apple model file: the
// part in brackets of the build key (`0x01168000 (1.1.6)` gives `1.1.6`).
// Empty when the key is missing or its value has no such part.
QString appleFirmwareVersion(const QMap<QString, QString> &appleFields);

// Section 7.6: the kind and the version to show. The files of our firmware or
// of another one come first (a valid info file that is for this model), then
// the Apple model file, else unknown. `log` receives the reason, when given.
FirmwareLine identifyFirmware(const FirmwareInfo &files, const QMap<QString, QString> &appleFields,
                              QStringList *log = nullptr);

} // namespace tacetos::device
