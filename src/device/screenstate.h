// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "live.h"
#include "livedriver.h"
#include "operations.h"
#include "udisks.h"

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

// What the app shows for each iPod (docs/specs/device-state.md section 1): the
// detected iPod, the operation of the app and the released record, combined
// by one rule. screenState() is the rule, a pure function; DeviceStateModel
// feeds it from the tracker and the runner and tells the screens what changed.

namespace tacetos::device {

// Section 1.1. These are the names of the code, not the text the user reads.
enum class ScreenKind {
    // No entry: the state of the whole screen when the list is empty, and what
    // stateChanged() says for a port whose entry went away.
    Unplugged,
    Reading,
    Ready,
    NotMounted,
    Dfu,
    Rebooting,
    Busy,
    Done,
    Failed,
    Released,
    Attention,
    // Reserved (section 1.5): never produced yet.
    DiskMode,
};

// The words of the spec ("unplugged", "not-mounted", ...), for the tests and
// the journal.
QString screenKindName(ScreenKind kind);

// A displayed state is a small record (section 1.1).
struct ScreenState
{
    ScreenKind kind = ScreenKind::Unplugged;
    // Attention only: the detection state that explains it.
    IpodState reason = IpodState::Ready;
    // Busy, done and failed: the kind of the operation.
    OperationKind operation = OperationKind::Mount;
    // Failed: the error of the service (the text for the journal stays in the
    // operation record).
    DiskError error = DiskError::None;

    bool operator==(const ScreenState &other) const;
    bool operator!=(const ScreenState &other) const { return !(*this == other); }
};

// Section 2.7: a good eject leaves this behind for the port.
struct ReleasedRecord
{
    // When the eject ended, in ms of the clock of the app.
    qint64 sinceMs = 0;
};

// Section 1.2: the order is operation, rebooting, released, detection. None
// when there is no entry. `missing` is true when the iPod is held (or in DFU)
// and its USB device is gone (LiveTracker::isMissing). A finished operation
// needs an iPod to be shown on: with none, it is dropped (rows 12 and 25).
std::optional<ScreenState> screenState(const std::optional<DetectedIpod> &ipod, bool missing,
                                       const std::optional<Operation> &operation,
                                       const std::optional<ReleasedRecord> &released, qint64 nowMs);

// One entry of the list the screen shows.
struct ScreenEntry
{
    QString portPath;
    ScreenState state;
    // None when the entry is kept by a record alone (busy after an unplug,
    // released after an unplug).
    std::optional<DetectedIpod> ipod;
};

// Combines LiveTracker and OperationRunner. It keeps the released records,
// ends `done` and `released` on time, and sends only what changed.
class DeviceStateModel : public WakeSource
{
    Q_OBJECT

public:
    // All three must outlive the model. `clock` is the one given to the tracker.
    DeviceStateModel(LiveTracker *tracker, OperationRunner *runner, const Clock *clock,
                     QObject *parent = nullptr);

    // The entries, in the order the iPods appeared.
    QList<ScreenEntry> entries() const;
    std::optional<ScreenEntry> entry(const QString &portPath) const;
    // The state of the whole screen: `unplugged` when the list is empty, else
    // the state of the first entry.
    ScreenState screen() const;

    // The user starts an operation (rows 8, 14 and 15): ends the released
    // record of the port, then asks the runner.
    OperationRequest startOperation(OperationKind kind, const QString &portPath);
    // The user presses Refresh (row 16): ends `failed` and `released`, then
    // reads everything again.
    void refresh();

    // WakeSource: the next end of a `done` or a `released`.
    std::optional<qint64> nextWakeMs() const override;
    void tick() override;

signals:
    // A displayed state changed. `Unplugged` means the port has no entry now.
    void stateChanged(const QString &portPath, const tacetos::device::ScreenState &state);
    // Once after the stateChanged signals of one change.
    void listChanged();

private:
    void onAdded(const DetectedIpod &ipod);
    void onRemoved(const QString &portPath);
    void onOperation(const Operation &operation);
    // Turns a finished operation into what it leaves: a released record, or
    // nothing when the iPod left.
    void settle(const QString &portPath);
    // Computes every entry again and sends what is different.
    void update();
    std::optional<ScreenEntry> compute(const QString &portPath) const;

    LiveTracker *tracker_;
    OperationRunner *runner_;
    const Clock *clock_;
    QHash<QString, ReleasedRecord> released_;
    // What was last shown, and in which order.
    QStringList order_;
    QHash<QString, ScreenState> shown_;
    std::optional<qint64> lastWake_;
};

} // namespace tacetos::device

Q_DECLARE_METATYPE(tacetos::device::ScreenState)
