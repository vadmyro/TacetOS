// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "entry.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// The anonymised report (docs/specs/install-wizard.md section 12). It builds
// two texts and sends nothing. Everything about the machine is given by the
// caller, never looked up, so that the tests do not depend on the computer.

namespace tacetos::journal {

// One failure of the last flow: a code of wizard section 13 and its detail.
struct ReportFailure
{
    QString code;
    QString detail;
};

struct ReportInput
{
    // Section 12.1: the versions, from the caller.
    QString appVersion;
    QString system;
    QString helperVersion;

    // The iPod and the last flow.
    QString model;
    QString family;
    QString firmwareLine;
    QString serial;
    QString flow;
    QString path;
    QString step;
    QList<ReportFailure> failures;

    // The ring of the journal, the oldest first. The entries from
    // `flowStart` on are the last flow, and the kReportContext entries before
    // are its context. Without a `flowStart`, every entry is the flow.
    QList<Entry> entries;
    QDateTime flowStart;

    // Section 12.2: what is taken out. `serials` are the other serials that
    // detection saw or that a progress file or a backup names.
    QStringList serials;
    QStringList volumeLabels;
    QString backupFolder;
    QString hostName;
    QString home;

    // The data folder of the app, where `report-key` is kept.
    QString dataFolder;
};

struct Report
{
    // At most kReportSummaryMax characters, to copy.
    QString summary;
    // Every line, to save as a `.txt` file.
    QString full;
};

// The key of the serial names: 16 bytes in `<dataFolder>/report-key`, made the
// first time. When the file cannot be written the key lives for this call
// only, and the names are stable inside one report but not between two.
QByteArray loadReportKey(const QString &dataFolder);

// Section 12.2: `ipod-` and the first 8 hexadecimal digits of the HMAC-SHA256
// of the serial with the key.
QString serialName(const QByteArray &key, const QString &serial);

// The serial made safe for a file name (wizard section 5.1): everything but
// letters, digits, `-` and `_` becomes `_`.
QString safeSerial(const QString &serial);

// The pure function of section 12.2: one pass over a text, so that what is
// put in is never taken out again by another rule.
class ReportScrubber
{
public:
    struct Rules
    {
        QByteArray key;
        QStringList serials;
        QStringList volumeLabels;
        QString backupFolder;
        QString hostName;
        QString home;
    };

    explicit ReportScrubber(const Rules &rules);

    QString apply(const QString &text) const;

private:
    QRegularExpression m_re;
    QStringList m_replacements;
    QString m_home;
};

// Builds both forms. Section 12.3: the summary is cut at the end of a whole
// line and ends with `... (N more lines in the full report)`, its log lines
// are in a code block counted in the limit.
Report buildReport(const ReportInput &input);

} // namespace tacetos::journal
