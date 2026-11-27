// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "battery.h"

#include "constants.h"

#include <algorithm>
#include <cmath>

namespace tacetos::health {

std::optional<double> estimateHours(const ChargeFile &charge, const HealthFile &health)
{
    if (!charge.batteryLevel || *charge.batteryLevel < device::kFullLevel)
        return std::nullopt;
    if (!health.batteryLevel)
        return std::nullopt;
    // The charge counts as 100 % from kFullLevel up.
    const int drop = 100 - *health.batteryLevel;
    if (drop < device::kMinDrop)
        return std::nullopt;
    if (health.runtimeS < device::kMinRuntimeS)
        return std::nullopt;
    // Both clocks are `YYYY-MM-DD HH:MM:SS`, so they compare as text.
    if (!charge.clock.isEmpty() && !health.clock.isEmpty() && health.clock <= charge.clock)
        return std::nullopt;
    const double hours = double(health.runtimeS) * 100.0 / drop / 3600.0;
    return std::round(hours * 10.0) / 10.0;
}

double median3(double a, double b, double c)
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

std::optional<double> currentEstimate(const QList<double> &estimates)
{
    const qsizetype n = estimates.size();
    if (n < 3)
        return std::nullopt;
    return median3(estimates[n - 3], estimates[n - 2], estimates[n - 1]);
}

std::optional<double> referenceEstimate(const QList<double> &estimates)
{
    std::optional<double> best;
    for (qsizetype i = 0; i + 2 < estimates.size(); ++i) {
        const double m = median3(estimates[i], estimates[i + 1], estimates[i + 2]);
        if (!best || m > *best)
            best = m;
    }
    return best;
}

BatteryVerdict batteryVerdict(bool chargingError, const QList<double> &estimates)
{
    if (chargingError)
        return BatteryVerdict::Watch;
    static_assert(device::kMinEstimates >= 3, "the medians need 3 estimates");
    if (estimates.size() < device::kMinEstimates)
        return BatteryVerdict::Unknown;
    const double current = *currentEstimate(estimates);
    const double reference = *referenceEstimate(estimates);
    if (current <= device::kWornRatio * reference)
        return BatteryVerdict::Worn;
    return BatteryVerdict::Good;
}

} // namespace tacetos::health
