// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <QStringList>

namespace tacetos::wizard {

// The whole percentage of the steps that write to the iPod (docs/specs/boot-screen.md section
// 10.3): each writing step of the flow is an equal share, the bar is at
// `(steps done + part of the current step) / N`, and it never goes back until `reset`.
class WholeProgress
{
public:
    // The steps labelled Erase, Convert, Firmware, Bootloader, Remove bootloader, Remove files
    // and Check (install-screens.md section 13.1).
    static bool isWriting(const QString &step);
    // The Classic steps, whose share is the wait for the restart, not the bytes of the transfer
    // (the DFU guide shows those).
    static bool isRestart(const QString &step);

    // A new flow with these steps (install-wizard.md section 2.3): the bar is back at 0.
    void reset(const QStringList &steps);

    // The runner's signals. A step started at `index` counts the writing steps before it as done,
    // also when the flow resumes there.
    void stepStarted(int index, const QString &name);
    void stepProgress(const QString &name, qint64 done, qint64 total);
    void stepDone(const QString &name);
    // The time waited for the Classic to come back, over `RESTART_WINDOW`: at most 0.9.
    void restartWaited(qint64 waitedMs, qint64 windowMs);

    // N, the writing steps of the flow.
    int count() const { return count_; }
    // 0 to 1, never lower than before.
    double fraction() const { return shown_; }
    // 0 to 100, rounded down: 100 only when every writing step is done.
    int percent() const;

private:
    void update();

    QStringList steps_;
    QString current_;
    int count_ = 0;
    int before_ = 0;
    double part_ = 0;
    double shown_ = 0;
};

} // namespace tacetos::wizard
