// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/healthwindow.h"

#include "constants.h"
#include "home/homeaction.h"
#include "style/appfont.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace tacetos {

using namespace health;

namespace {

constexpr int kMargin = 24;
constexpr int kIndent = 16;

QString htr(const char *text)
{
    return QCoreApplication::translate("health", text);
}

// "4 h 12 min", "12 min".
QString duration(qint64 seconds, const QLocale &locale)
{
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours == 0)
        return htr("%1 min").arg(locale.toString(minutes));
    return htr("%1 h %2 min").arg(locale.toString(hours), locale.toString(minutes));
}

QString dateAndTime(const QDateTime &when, const QLocale &locale)
{
    return htr("%1, %2").arg(locale.toString(when.date(), QStringLiteral("d MMM yyyy")),
                             locale.toString(when.time(), QLocale::ShortFormat));
}

const char *roleOf(HealthLine::Style style)
{
    switch (style) {
    case HealthLine::Style::Secondary:
        return "secondary";
    case HealthLine::Style::Danger:
        return "danger";
    case HealthLine::Style::Body:
        break;
    }
    return "";
}

QLabel *newLabel(QWidget *parent, const char *name, TextStyle style, const QString &text = {})
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(name));
    label->setFont(fontFor(style));
    label->setWordWrap(true);
    return label;
}

void showLine(QLabel *label, const HealthLine &line)
{
    label->setText(line.text);
    label->setFont(fontFor(line.style == HealthLine::Style::Secondary ? TextStyle::Secondary
                                                                      : TextStyle::Body));
    setRole(label, roleOf(line.style));
}

// Section 5.2: "+3 since 12 Nov", from the last earlier reading of the history whose value of
// `id` differs. Empty when there is none.
QString changeOf(const HealthState &state, int id, quint32 now, const QLocale &locale)
{
    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(state.reading.healthBytes, QCryptographicHash::Sha256).toHex());
    const QList<HistoryReading> &readings = state.history.readings;
    for (auto it = readings.crbegin(); it != readings.crend(); ++it) {
        if (it->healthSha256 == sha || !it->smart || !it->smart->contains(id))
            continue;
        const quint32 before = it->smart->value(id);
        if (before == now)
            continue;
        const qint64 change = qint64(now) - qint64(before);
        const QString signedChange = change > 0 ? QStringLiteral("+%1").arg(locale.toString(change))
                                                : locale.toString(change);
        return htr("%1 since %2")
            .arg(signedChange,
                 locale.toString(it->readAt.toLocalTime().date(), QStringLiteral("d MMM")));
    }
    return {};
}

} // namespace

BatteryVerdict HealthState::battery() const
{
    return batteryVerdict(reading.ok && reading.health.charging == Charging::Error,
                          history.estimates());
}

QString healthSourceLine(const HealthState &state, const QLocale &locale)
{
    const HealthFile &file = state.reading.health;
    if (file.event == Event::Usb) {
        if (!state.runningOurs)
            return htr("From the last time TacetOS ran on this iPod.");
        return htr("Read when the iPod was plugged in, %1.")
            .arg(dateAndTime(state.reading.readAt.toLocalTime(), locale));
    }
    const QDateTime clock =
        QDateTime::fromString(file.clock, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (file.clock.isEmpty() || !clock.isValid())
        return htr("Read when the iPod was last switched off.");
    return htr("Read when the iPod was last switched off, %1.").arg(dateAndTime(clock, locale));
}

HealthLine diskVerdictLine(const HealthState &state)
{
    switch (state.disk()) {
    case DiskVerdict::Good:
        return {htr("No sign of wear."), HealthLine::Style::Body};
    case DiskVerdict::Watch:
        return {htr("Some sectors were replaced or could not be read. Keep a copy of your music "
                    "on the computer."),
                HealthLine::Style::Body};
    case DiskVerdict::Failing:
        return {htr("The disk is failing. Copy your music to the computer soon."),
                HealthLine::Style::Danger};
    case DiskVerdict::Unknown:
        break;
    }
    if (state.reading.disk.kind == DiskKind::Flash)
        return {htr("Flash storage gives no wear data."), HealthLine::Style::Secondary};
    return {htr("This disk gives no health data."), HealthLine::Style::Secondary};
}

HealthLine batteryVerdictLine(const HealthState &state)
{
    switch (state.battery()) {
    case BatteryVerdict::Good:
        return {htr("The battery holds its charge."), HealthLine::Style::Body};
    case BatteryVerdict::Watch:
        return {htr("The charger reported an error at the last reading. Unplug the iPod and plug "
                    "it in again."),
                HealthLine::Style::Body};
    case BatteryVerdict::Worn:
        return {htr("The battery lasts about half as long as it used to."),
                HealthLine::Style::Danger};
    case BatteryVerdict::Unknown:
        break;
    }
    return {htr("Not enough full charges measured yet (%1 of %2).")
                .arg(QLocale().toString(state.history.estimates().size()),
                     QLocale().toString(device::kMinEstimates)),
            HealthLine::Style::Secondary};
}

HealthLine cardHealthLine(const HealthState &state)
{
    if (!state.usable())
        return {};
    const DiskVerdict disk = state.disk();
    const BatteryVerdict battery = state.battery();
    if (disk == DiskVerdict::Failing)
        return {htr("Disk failing: copy your music soon."), HealthLine::Style::Danger};
    if (battery == BatteryVerdict::Worn)
        return {htr("Battery worn."), HealthLine::Style::Danger};
    if (disk == DiskVerdict::Watch)
        return {htr("Disk: some sectors replaced."), HealthLine::Style::Secondary};
    if (battery == BatteryVerdict::Watch)
        return {htr("Charger error at the last reading."), HealthLine::Style::Secondary};
    return {};
}

HealthWindow::HealthWindow(const QString &name, const HealthState &state, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("healthWindow"));
    setWindowTitle(htr("Health"));
    setMinimumWidth(480);
    const QLocale locale;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    layout->setSpacing(6);
    layout->addWidget(newLabel(this, "healthTitle", TextStyle::Title, htr("Health")));
    layout->addWidget(newLabel(this, "healthName", TextStyle::Body, name));

    if (!state.usable()) {
        empty_ = newLabel(this, "healthEmpty", TextStyle::Body,
                          htr("TacetOS has not written a health reading on this iPod yet. Unplug "
                              "it, then plug it in again while it is on."));
        layout->addSpacing(12);
        layout->addWidget(empty_);
    } else {
        source_ = newLabel(this, "healthSource", TextStyle::Secondary, healthSourceLine(state));
        setRole(source_, "secondary");
        layout->addWidget(source_);

        // Disk.
        const DiskReading &disk = state.reading.disk;
        layout->addSpacing(16);
        layout->addWidget(newLabel(this, "diskTitle", TextStyle::Button, htr("Disk")));
        auto *diskRows = new QVBoxLayout;
        diskRows->setContentsMargins(kIndent, 0, 0, 0);
        diskRows->setSpacing(4);
        layout->addLayout(diskRows);
        const QString kind = disk.kind == DiskKind::Flash ? htr("flash storage") : htr("hard disk");
        const QString model = disk.identify.ok ? disk.identify.model : QString();
        QString what;
        if (model.isEmpty())
            what = disk.kind == DiskKind::Flash ? htr("Flash storage") : htr("Hard disk");
        else
            what = htr("%1, %2").arg(model, kind);
        diskRows->addWidget(newLabel(this, "diskModel", TextStyle::Body, what));
        diskVerdict_ = newLabel(this, "diskVerdict", TextStyle::Body);
        showLine(diskVerdict_, diskVerdictLine(state));
        diskRows->addWidget(diskVerdict_);

        const auto count = [&](int id, const char *title, bool withChange) {
            const std::optional<quint32> value = disk.value(id);
            if (!value)
                return;
            QString text = locale.toString(*value);
            const QString change = withChange ? changeOf(state, id, *value, locale) : QString();
            if (!change.isEmpty())
                text = htr("%1 (%2)").arg(text, change);
            addRow(diskRows, htr(title), text);
        };
        count(kReallocatedId, QT_TRANSLATE_NOOP("health", "Reallocated sectors"), true);
        count(kPendingId, QT_TRANSLATE_NOOP("health", "Pending sectors"), true);
        count(kUncorrectableId, QT_TRANSLATE_NOOP("health", "Uncorrectable sectors"), true);
        count(kSpinRetriesId, QT_TRANSLATE_NOOP("health", "Spin-up retries"), false);
        if (const std::optional<int> celsius = disk.temperature())
            addRow(diskRows, htr("Temperature"), htr("%1 °C").arg(locale.toString(*celsius)));
        if (const std::optional<quint32> hours = disk.value(kPowerOnHoursId))
            addRow(diskRows, htr("Power-on time"), htr("%1 h").arg(locale.toString(*hours)));
        count(kPowerCyclesId, QT_TRANSLATE_NOOP("health", "Power cycles"), false);
        count(kHeadLoadsId, QT_TRANSLATE_NOOP("health", "Head load cycles"), false);
        count(kDropEventsId, QT_TRANSLATE_NOOP("health", "Drop sensor events"), false);

        // Battery.
        const HealthFile &file = state.reading.health;
        layout->addSpacing(16);
        layout->addWidget(newLabel(this, "batteryTitle", TextStyle::Button, htr("Battery")));
        auto *batteryRows = new QVBoxLayout;
        batteryRows->setContentsMargins(kIndent, 0, 0, 0);
        batteryRows->setSpacing(4);
        layout->addLayout(batteryRows);
        batteryVerdict_ = newLabel(this, "batteryVerdict", TextStyle::Body);
        showLine(batteryVerdict_, batteryVerdictLine(state));
        batteryRows->addWidget(batteryVerdict_);
        const QString volts = htr("%1 V").arg(locale.toString(file.batteryMv / 1000.0, 'f', 2));
        addRow(batteryRows, htr("Level"),
               file.batteryLevel ? htr("%1 % (%2)").arg(locale.toString(*file.batteryLevel), volts)
                                 : volts);
        addRow(batteryRows, htr("On battery since the last charge"),
               duration(file.runtimeS, locale));
        addRow(batteryRows, htr("Longest on one charge"), duration(file.topRuntimeS, locale));
        const QList<double> estimates = state.history.estimates();
        if (const std::optional<double> current = currentEstimate(estimates)) {
            addRow(batteryRows, htr("Per full charge"),
                   htr("about %1 h, median of the last 3 charges")
                       .arg(locale.toString(*current, 'f', 1)));
        } else if (!estimates.isEmpty()) {
            addRow(batteryRows, htr("Per full charge"),
                   htr("about %1 h").arg(locale.toString(estimates.last(), 'f', 1)));
        }
    }

    layout->addSpacing(16);
    layout->addStretch(1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    close_ = new QPushButton(htr("Close"), this);
    close_->setObjectName(QStringLiteral("closeButton"));
    close_->setProperty("role", QStringLiteral("secondary"));
    close_->setFont(fontFor(TextStyle::Button));
    close_->setDefault(true);
    buttons->addWidget(close_);
    layout->addLayout(buttons);
    connect(close_, &QPushButton::clicked, this, &QDialog::accept);
}

QString HealthWindow::value(const QString &title) const
{
    for (const auto &[name, label] : rows_)
        if (name == title)
            return label->text();
    return {};
}

void HealthWindow::addRow(QVBoxLayout *layout, const QString &title, const QString &value)
{
    auto *row = new QHBoxLayout;
    row->setSpacing(16);
    auto *name = new QLabel(title, this);
    name->setFont(fontFor(TextStyle::Body));
    auto *shown = new QLabel(value, this);
    shown->setFont(fontFor(TextStyle::Body));
    shown->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(name);
    row->addStretch(1);
    row->addWidget(shown);
    layout->addLayout(row);
    rows_.append({title, shown});
}

} // namespace tacetos
