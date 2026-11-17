// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QtGlobal>
#include <deque>

// The time left of a long copy (docs/specs/install-wizard.md section 5.5).

namespace tacetos::wizard {

// The speed of a copy over its last `kSpeedWindowMs`, and the time left at that
// speed. The times are given by the caller, in milliseconds of a clock that
// never goes back, so that the tests choose them. The screen feeds it with the
// progress of a step.
class TimeLeft
{
public:
    // `done` bytes at `msecs`. A `done` smaller than the last one is a new copy:
    // what was noted before is forgotten.
    void add(qint64 msecs, qint64 done);

    // Bytes per second over the window, or -1 while it is not known yet.
    double speed() const;

    // Whole seconds left until `total`, or -1 while the speed is not known.
    qint64 secondsLeft(qint64 total) const;

    void clear() { m_points.clear(); }

private:
    struct Point
    {
        qint64 msecs;
        qint64 done;
    };
    std::deque<Point> m_points;
};

} // namespace tacetos::wizard
