// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "constants.h"
#include "live.h"
#include "operations.h"
#include "screenstate.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

// Mounting on arrival (docs/specs/device-state.md section 2.8): an iPod that
// comes in with its music volume not mounted is mounted by the app, with the
// `mount` operation and without opening the file manager after it. It follows
// the displayed states, so it never starts anything for an iPod that an
// operation holds, for a volume formatted for Mac, or twice for the same
// arrival. Linux only in the app: the other systems mount a FAT32 volume by
// themselves.
//
// The model, the tracker and the runner must outlive it.

namespace tacetos::device {

class AutoMounter : public QObject
{
    Q_OBJECT

public:
    AutoMounter(DeviceStateModel *model, LiveTracker *tracker, OperationRunner *runner,
                bool enabled = kMountOnArrival, QObject *parent = nullptr);

    // The setting "Mount iPods when they are plugged in" (rule 4).
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

signals:
    // Lines for the journal, like OperationRunner::logged: the warning of a
    // refusal of the disk service (rule 3).
    void logged(const QString &portPath, const QStringList &lines);

private:
    void onStateChanged(const QString &portPath, const ScreenState &state);
    void onOperation(const Operation &operation);
    bool ours(const Operation &operation) const;

    DeviceStateModel *model_;
    LiveTracker *tracker_;
    OperationRunner *runner_;
    bool enabled_;
    // The last displayed state of each port, to see which one `not-mounted`
    // comes from.
    QHash<QString, ScreenState> last_;
    // The ports whose arrival was already dealt with (rule 2).
    QSet<QString> spent_;
    // The mount that the app started, by port: the id once the runner has
    // answered.
    QHash<QString, quint64> pending_;
    // The port of the request being made (a runner that works at once ends the
    // operation before it answers).
    QString starting_;
};

} // namespace tacetos::device
