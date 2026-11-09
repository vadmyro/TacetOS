// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "livedriver.h"

#include <algorithm>
#include <limits>

namespace tacetos::device {

LiveDriver::LiveDriver(LiveTracker *tracker, const Clock *clock, QObject *parent)
    : QObject(parent), tracker_(tracker), clock_(clock)
{
    // One shot, set again after every turn: a timer that nobody needs never runs.
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &LiveDriver::wake);
    backup_.setInterval(static_cast<int>(kPollBackupMs));
    connect(&backup_, &QTimer::timeout, this, &LiveDriver::backupTick);
    tracker_->setPolling(true);
}

void LiveDriver::addEventSource(EventSource *source)
{
    if (!source || sources_.contains(source))
        return;
    sources_ << source;
    connect(source, &EventSource::usbChanged, this, &LiveDriver::onUsbChanged);
    connect(source, &EventSource::disksChanged, this, &LiveDriver::onDisksChanged);
    connect(source, &QObject::destroyed, this, [this, source] {
        sources_.removeAll(source);
        tracker_->setPolling(polling());
        updateBackup();
        reschedule();
    });
    tracker_->setPolling(false);
    updateBackup();
    reschedule();
}

void LiveDriver::start()
{
    running_ = true;
    tracker_->refresh();
    updateBackup();
    reschedule();
}

void LiveDriver::stop()
{
    running_ = false;
    timer_.stop();
    backup_.stop();
}

std::optional<int> LiveDriver::scheduledDelayMs() const
{
    if (!timer_.isActive())
        return std::nullopt;
    return timer_.interval();
}

void LiveDriver::wake()
{
    if (!running_)
        return;
    tracker_->tick();
    reschedule();
}

void LiveDriver::backupTick()
{
    if (!running_)
        return;
    tracker_->usbEvent();
    tracker_->diskEvent();
    reschedule();
}

void LiveDriver::updateBackup()
{
    if (running_ && !polling())
        backup_.start();
    else
        backup_.stop();
}

void LiveDriver::onUsbChanged()
{
    if (!running_)
        return;
    tracker_->usbEvent();
    reschedule();
}

void LiveDriver::onDisksChanged()
{
    if (!running_)
        return;
    tracker_->diskEvent();
    reschedule();
}

void LiveDriver::reschedule()
{
    if (!running_)
        return;
    const std::optional<qint64> next = tracker_->nextWakeMs();
    if (!next) {
        timer_.stop();
        return;
    }
    const qint64 delay =
        std::clamp<qint64>(*next - clock_->nowMs(), 0, std::numeric_limits<int>::max());
    timer_.start(static_cast<int>(delay));
}

} // namespace tacetos::device
