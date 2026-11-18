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

// Wizard section 12.3: the summary of the report fits in one Discord message.
inline constexpr int kReportSummaryMax = 2000;

// Wizard section 12.1: entries of the journal kept before the last flow.
inline constexpr int kReportContext = 200;

// Wizard section 12.2: the key of the serial names, in the data folder of the
// app, and its size in bytes.
inline constexpr QLatin1String kReportKeyFile("report-key");
inline constexpr int kReportKeyBytes = 16;

} // namespace tacetos::journal
