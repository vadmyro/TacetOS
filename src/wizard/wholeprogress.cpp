// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wholeprogress.h"

#include "flow.h"

#include <QtMath>

namespace tacetos::wizard {

namespace {

// Section 10.3: the wait for the restart stops short of the end until the iPod is back.
constexpr double kRestartCeiling = 0.9;

} // namespace

bool WholeProgress::isWriting(const QString &name)
{
    return name == step::format || name == step::convert || name == step::firmware ||
           name == step::bootloader || name == step::classicBootloader ||
           name == step::removeBootloader || name == step::classicRemove ||
           name == step::removeFirmware || name == step::verify;
}

bool WholeProgress::isRestart(const QString &name)
{
    return name == step::classicBootloader || name == step::classicRemove;
}

void WholeProgress::reset(const QStringList &steps)
{
    steps_ = steps;
    count_ = 0;
    for (const QString &name : steps)
        if (isWriting(name))
            ++count_;
    current_.clear();
    before_ = 0;
    part_ = 0;
    shown_ = 0;
}

void WholeProgress::stepStarted(int index, const QString &name)
{
    before_ = 0;
    for (int i = 0; i < index && i < steps_.size(); ++i)
        if (isWriting(steps_.at(i)))
            ++before_;
    current_ = isWriting(name) ? name : QString();
    part_ = 0;
    update();
}

void WholeProgress::stepProgress(const QString &name, qint64 done, qint64 total)
{
    // A total of 0 says nothing about the part: it stays where it was.
    if (name != current_ || isRestart(name) || total <= 0)
        return;
    part_ = qBound(0.0, double(done) / double(total), 1.0);
    update();
}

void WholeProgress::stepDone(const QString &name)
{
    if (name != current_)
        return;
    part_ = 1;
    update();
}

void WholeProgress::restartWaited(qint64 waitedMs, qint64 windowMs)
{
    if (!isRestart(current_) || windowMs <= 0)
        return;
    part_ = qBound(0.0, double(waitedMs) / double(windowMs), kRestartCeiling);
    update();
}

int WholeProgress::percent() const
{
    // The small margin keeps 0.29 * 100 at 29, not 28.
    return qBound(0, int(qFloor(shown_ * 100 + 1e-9)), 100);
}

void WholeProgress::update()
{
    if (count_ == 0)
        return;
    const double now = qMin(1.0, (before_ + part_) / count_);
    shown_ = qMax(shown_, now);
}

} // namespace tacetos::wizard
