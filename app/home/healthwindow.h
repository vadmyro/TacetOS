// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "battery.h"
#include "disk.h"
#include "history.h"
#include "reading.h"

#include <QDialog>
#include <QLocale>
#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace tacetos {

// What the app knows of the health of one iPod (docs/specs/health.md section 4): the last
// reading and the history it was added to.
struct HealthState
{
    // Not ok without a usable health file.
    health::HealthReading reading;
    // The readings kept for this iPod, the last one included. The reading alone when there is
    // no history (no serial).
    health::History history;
    // Section 3, "How fresh": the iPod runs a firmware that is not Apple's now.
    bool runningOurs = false;

    bool usable() const { return reading.ok; }
    health::DiskVerdict disk() const { return reading.disk.verdict; }
    // Section 6.3, over the estimates of the history.
    health::BatteryVerdict battery() const;
};

// A line of the window or of the card, with the style of the tables of sections 8 and 9.
struct HealthLine
{
    enum class Style { Body, Secondary, Danger };
    // Empty: no line.
    QString text;
    Style style = Style::Body;
};

// Section 8, "The source line".
QString healthSourceLine(const HealthState &state, const QLocale &locale = QLocale());
// Section 8, "Verdict lines".
HealthLine diskVerdictLine(const HealthState &state);
HealthLine batteryVerdictLine(const HealthState &state);
// Section 9: the line of the card for the most serious verdict, empty for `good` and `unknown`.
HealthLine cardHealthLine(const HealthState &state);

// The Health window (section 8): the name of the card, where the reading comes from, the disk
// and the battery. It only shows what it is given; Close closes it.
class HealthWindow : public QDialog
{
    Q_OBJECT

public:
    HealthWindow(const QString &name, const HealthState &state, QWidget *parent = nullptr);

    QLabel *sourceLabel() const { return source_; }
    QLabel *diskVerdictLabel() const { return diskVerdict_; }
    QLabel *batteryVerdictLabel() const { return batteryVerdict_; }
    QLabel *emptyLabel() const { return empty_; }
    QPushButton *closeButton() const { return close_; }
    // The value of the row named `title` ("Pending sectors"), empty when the row is not shown.
    QString value(const QString &title) const;

private:
    void addRow(QVBoxLayout *layout, const QString &title, const QString &value);

    QLabel *source_ = nullptr;
    QLabel *diskVerdict_ = nullptr;
    QLabel *batteryVerdict_ = nullptr;
    QLabel *empty_ = nullptr;
    QPushButton *close_ = nullptr;
    QList<std::pair<QString, QLabel *>> rows_;
};

} // namespace tacetos
