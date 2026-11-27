// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

// The two files the firmware writes in its folder (docs/specs/health.md
// sections 2 and 3): the health file and the charge file. Only parsing here,
// with no file system.

namespace tacetos::health {

enum class Event { Usb, Shutdown };
enum class Charging { No, Yes, Error };
enum class DiskKind { Hdd, Flash };
enum class SmartState { Ok, Unsupported, Error };

// Section 2.1.
struct HealthFile
{
    Event event = Event::Usb;
    // `YYYY-MM-DD HH:MM:SS` as the iPod wrote it, empty for `unset`. Two clocks
    // in this form compare as text.
    QString clock;
    // None for `unknown`.
    std::optional<int> batteryLevel;
    int batteryMv = 0;
    Charging charging = Charging::No;
    qint64 runtimeS = 0;
    qint64 topRuntimeS = 0;
    DiskKind diskKind = DiskKind::Hdd;
    SmartState smart = SmartState::Unsupported;
    // The code of `smart: error <code>`.
    int smartError = 0;
    // The blocks as written, not checked yet (section 5.1). Empty when the
    // file has no such line.
    QString identifyData;
    QString smartData;
};

// Section 2.2.
struct ChargeFile
{
    QString clock;
    std::optional<int> batteryLevel;
    int batteryMv = 0;
};

struct HealthFileReading
{
    bool ok = false;
    HealthFile file;
    // Why the file is ignored, for the log line ("health.txt: no end line").
    // Empty when ok.
    QString error;
};

struct ChargeFileReading
{
    bool ok = false;
    ChargeFile file;
    QString error;
};

// Section 3: a file is usable when it is at most kMarkerMaxBytes, its first
// line is `format: 1`, its last line `end: ok`, every line is a `key: value`
// line and every key marked "Always" has a value of the right form. A carriage
// return at the end of a line is dropped; an unknown key is ignored; the last
// line of a key wins.
HealthFileReading parseHealthFile(const QByteArray &data);
ChargeFileReading parseChargeFile(const QByteArray &data);

} // namespace tacetos::health
