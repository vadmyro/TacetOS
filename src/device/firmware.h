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

// Section 7.6: which firmware runs, from the USB name strings. Only Apple's
// two strings are ever compared: both given and equal to them is `Apple`, any
// other string at all is `Other`, none is `Unknown`.
RunningFirmware runningFirmware(const QString &manufacturer, const QString &product);

// Section 7.6: the kind and the version to show. What runs comes first when
// the name strings say Apple's; then the files of our firmware or of another
// one (a valid info file that is for this model); then `Custom` when something
// else runs; then, with no name strings, the Apple model file; else unknown.
// `log` receives the reason, when given.
FirmwareLine identifyFirmware(const FirmwareInfo &files, const QMap<QString, QString> &appleFields,
                              RunningFirmware running, QStringList *log = nullptr);

} // namespace tacetos::device
