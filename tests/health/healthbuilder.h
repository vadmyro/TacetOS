// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "blockbuilder.h"

#include <QByteArray>

// Builds the text of the health file and of the charge file of
// docs/specs/health.md sections 2.1 and 2.2, for the tests of the battery and
// the history.

namespace testfiles {

struct Health
{
    QByteArray clock = "2026-11-27 14:03:00";
    QByteArray level = "50";
    qint64 runtimeS = 36000;
    QByteArray charging = "no";
    // The identify block, none when empty.
    QByteArray identify;
    // The SMART block: `smart: ok` with it, `smart: unsupported` without.
    QByteArray smart;
};

inline QByteArray healthText(const Health &h)
{
    QByteArray text = "format: 1\nevent: usb\nclock: " + h.clock + "\nbattery_level: " + h.level +
                      "\nbattery_mv: 3800\ncharging: " + h.charging +
                      "\nruntime_s: " + QByteArray::number(h.runtimeS) +
                      "\ntop_runtime_s: " + QByteArray::number(h.runtimeS) + "\ndisk_kind: hdd\n";
    if (!h.identify.isEmpty())
        text += "identify_data: " + h.identify.toHex() + "\n";
    if (h.smart.isEmpty())
        text += "smart: unsupported\n";
    else
        text += "smart: ok\nsmart_data: " + h.smart.toHex() + "\n";
    return text + "end: ok\n";
}

inline QByteArray chargeText(const QByteArray &level = "100",
                             const QByteArray &clock = "2026-11-26 09:00:00")
{
    return "format: 1\nclock: " + clock + "\nbattery_level: " + level +
           "\nbattery_mv: 4150\nend: ok\n";
}

} // namespace testfiles
