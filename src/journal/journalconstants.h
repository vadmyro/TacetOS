// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>
#include <QtGlobal>

// The constants of the journal (docs/specs/device-state.md section 4). The
// sizes are starting values.

namespace tacetos::journal {

// Section 3.3: entries kept in memory, the oldest dropped first.
inline constexpr int kJournalMaxEntries = 1000;

// Section 3.2: an entry text longer than this is cut and ends with "...".
inline constexpr int kJournalLineMax = 1000;

// Section 3.4: the file, from the data folder of the app that the caller gives.
inline constexpr QLatin1String kJournalFile("logs/tacetos.log");

// Section 3.4: the file is rotated before an entry would take it over this size.
inline constexpr qint64 kJournalFileMaxBytes = 1024 * 1024;

// Section 3.4: the current file and the old ones.
inline constexpr int kJournalFiles = 3;

// Section 3.1: the time of an entry, in UTC.
inline constexpr QLatin1String kJournalTimeFormat("yyyy-MM-ddTHH:mm:ss.zzzZ");

} // namespace tacetos::journal
