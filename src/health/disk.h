// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "blocks.h"
#include "files.h"

#include <QStringList>
#include <optional>

// What the app makes of the disk lines of a health file (docs/specs/health.md
// sections 5.2 and 5.3).

namespace tacetos::health {

enum class DiskVerdict { Good, Watch, Failing, Unknown };

// Section 5.3. `smart` is the decoded block, or one with ok false when the
// file has none (`unsupported`, `error`).
DiskVerdict diskVerdict(DiskKind kind, const SmartBlock &smart);

struct DiskReading
{
    DiskKind kind = DiskKind::Hdd;
    SmartState smartState = SmartState::Unsupported;
    int smartError = 0;
    // Not ok when the file has no identify line or the block is unusable.
    IdentifyBlock identify;
    // Not ok without a usable block; the attributes are there even on flash.
    SmartBlock smart;
    DiskVerdict verdict = DiskVerdict::Unknown;

    // Section 5.2: the values shown, each only when the disk reports it.
    std::optional<quint32> value(int id) const { return smart.ok ? smart.raw(id) : std::nullopt; }
    std::optional<int> temperature() const { return smart.ok ? smart.temperature() : std::nullopt; }
};

// Decodes both blocks of a usable health file and gives the verdict. Each
// unusable block, and a wrong SMART checksum, adds one line to `log`.
DiskReading readDisk(const HealthFile &file, QStringList *log);

} // namespace tacetos::health
