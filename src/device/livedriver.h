// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "live.h"

#include <QList>
#include <QObject>
#include <QTimer>
#include <optional>

// What makes the live part move (docs/specs/detection.md section 9.1): a
// source of events tells that something changed, and one timer wakes the
// tracker at the time it asks for. The tracker does the thinking; this only
// calls it.

namespace tacetos::device {

// A source of hints from the system (the udev monitor, a watch on the mounts).
// Its signals are only hints to read now, never data.
class EventSource : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

signals:
    void usbChanged();
    void disksChanged();
};

// Something besides the tracker that has delays of its own (the end of a
// `done` or a `released` of the state model). The driver wakes it at the time
// it asks for, with the same timer, so no timer runs for nothing.
class WakeSource : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // The time of the next tick() that would change something, from the
    // clock's own scale. Empty when nothing is waiting.
    virtual std::optional<qint64> nextWakeMs() const = 0;
    // Lets time pass. Called by the driver when the timer fires.
    virtual void tick() = 0;

signals:
    // nextWakeMs() is not what the driver last heard: it sets its timer again.
    void wakeChanged();
};

class LiveDriver : public QObject
{
    Q_OBJECT

public:
    // `tracker` and `clock` must outlive the driver. `clock` is the one given
    // to the tracker.
    LiveDriver(LiveTracker *tracker, const Clock *clock, QObject *parent = nullptr);

    // Adds a source of events (not owned). Once there is one, the tracker
    // stops polling and only the backup reading is left. With none, it polls
    // (section 9.1).
    void addEventSource(EventSource *source);

    // Adds something to wake together with the tracker (not owned).
    void addWakeSource(WakeSource *source);

    // True while no event source is there.
    bool polling() const { return sources_.isEmpty(); }
    // True while the backup reading runs: started, with an event source.
    bool backupRunning() const { return backup_.isActive(); }

    // Starts with one full reading, then waits for events and wake-ups.
    void start();
    // Stops the timer and ignores the events until start() is called again.
    void stop();
    bool isRunning() const { return running_; }

    // The delay of the timer that is waiting, in milliseconds. Empty when no
    // timer runs: the driver is stopped, or the tracker has nothing to wait for.
    std::optional<int> scheduledDelayMs() const;

    // What the timer calls: lets the tracker take its turn, then sets the
    // timer again. Public so that a test can call it without waiting.
    void wake();
    // What the backup timer calls, every kPollBackupMs: the same hints as an
    // event source would send. Public so that a test can call it.
    void backupTick();

private:
    void onUsbChanged();
    void onDisksChanged();
    void reschedule();
    void updateBackup();

    LiveTracker *tracker_;
    const Clock *clock_;
    QList<EventSource *> sources_;
    QList<WakeSource *> wakeSources_;
    QTimer timer_;
    QTimer backup_;
    bool running_ = false;
};

} // namespace tacetos::device
