// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "home/healthwindow.h"
#include "models.h"
#include "screenstate.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <optional>

namespace tacetos::journal {
class Journal;
}

namespace tacetos {

// When the home screen reads the health files (docs/specs/health.md section 3): each time the
// card of an iPod reaches `ready` with the firmware line `tacetos` and its music volume mounted,
// and again on Refresh. Each reading is added to the history of the iPod in `dataFolder`; with
// an empty folder the history is not kept. Only reads on the iPod.
class HealthWatcher : public QObject
{
    Q_OBJECT

public:
    // `journal` may be null and outlives the watcher.
    HealthWatcher(const QString &dataFolder, journal::Journal *journal, QObject *parent = nullptr);

    // The entries the home screen shows now.
    void show(const QList<device::ScreenEntry> &entries, const QList<device::Model> &models);
    // Refresh: reads the files of every iPod shown again.
    void refresh();

    // What is known for the iPod of this port, none when its files were not read.
    std::optional<HealthState> stateFor(const QString &port) const;

    // How many times the files were read, for the tests.
    int reads() const { return reads_; }

signals:
    // The state of this port changed (stateFor).
    void healthChanged(const QString &port);

private:
    struct Shown
    {
        QString mountPoint;
        QString folder;
        QString usbSerial;
        bool runningOurs = false;
    };

    void read(const QString &port, const Shown &shown);

    QString dataFolder_;
    journal::Journal *journal_;
    int reads_ = 0;
    // The iPods whose files are read, by port.
    QHash<QString, Shown> shown_;
    QHash<QString, HealthState> states_;
};

} // namespace tacetos
