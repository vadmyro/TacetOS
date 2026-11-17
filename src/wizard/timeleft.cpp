// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "timeleft.h"

#include "wizardconstants.h"

#include <cmath>

namespace tacetos::wizard {

namespace {

// Under one second of copy, a speed says more about the caches than the disks.
constexpr qint64 kShortestSpan = 1000;

} // namespace

void TimeLeft::add(qint64 msecs, qint64 done)
{
    if (!m_points.empty() && (done < m_points.back().done || msecs < m_points.back().msecs))
        m_points.clear();
    m_points.push_back({msecs, done});
    // Keep one point at or before the start of the window, so that the window
    // spans its whole length once the copy has run that long.
    while (m_points.size() > 2 && m_points[1].msecs <= msecs - kSpeedWindowMs)
        m_points.pop_front();
}

double TimeLeft::speed() const
{
    if (m_points.size() < 2)
        return -1;
    const qint64 span = m_points.back().msecs - m_points.front().msecs;
    const qint64 bytes = m_points.back().done - m_points.front().done;
    if (span < kShortestSpan || bytes <= 0)
        return -1;
    return double(bytes) * 1000.0 / double(span);
}

qint64 TimeLeft::secondsLeft(qint64 total) const
{
    if (!m_points.empty() && m_points.back().done >= total)
        return 0;
    const double perSecond = speed();
    if (perSecond <= 0)
        return -1;
    return qint64(std::ceil(double(total - m_points.back().done) / perSecond));
}

} // namespace tacetos::wizard
