// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "livedriver.h"

#include <QFile>
#include <QSocketNotifier>
#include <QString>

// A watch on the mount table of Linux (docs/specs/detection.md section 9.1).
// The kernel reports a mount or an unmount on the open mountinfo file as an
// exceptional condition (POLLPRI), which Qt gives as a socket notifier of type
// Exception. Each one is a hint to read the disks again, never data.

namespace tacetos::device {

class MountWatcher : public EventSource
{
    Q_OBJECT

public:
    // `path` is the file to watch. Only a test gives another one than the real
    // mountinfo of the process.
    explicit MountWatcher(QString path = QStringLiteral("/proc/self/mountinfo"),
                          QObject *parent = nullptr);
    ~MountWatcher() override;

    // Opens the file and starts listening. False when it cannot be opened:
    // nothing is left half open, and the caller polls instead.
    bool open();
    bool isOpen() const { return file_.isOpen(); }
    const QString &path() const { return path_; }

    // Opens a watch and gives it to the driver as an event source. Returns
    // null when it does not open, and the driver is left as it was. The watch
    // belongs to `driver`.
    static MountWatcher *attach(LiveDriver *driver,
                                const QString &path = QStringLiteral("/proc/self/mountinfo"));

    // What the notifier calls when the kernel reports a change: sends one
    // disksChanged. Public so that a test can stand for the kernel, which only
    // raises it on the real mountinfo.
    void reportChange() { emit disksChanged(); }

private:
    QString path_;
    QFile file_;
    QSocketNotifier *notifier_ = nullptr;
};

} // namespace tacetos::device
