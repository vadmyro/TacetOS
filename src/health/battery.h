// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "files.h"

#include <QList>
#include <optional>

// What the app makes of the battery lines (docs/specs/health.md section 6):
// the estimate of one charge, and the verdict over the estimates of the
// history. Pure.

namespace tacetos::health {

enum class BatteryVerdict { Good, Watch, Worn, Unknown };

// Section 6.2: the hours a full charge lasts, with one digit after the point,
// from a usable charge file and a later usable health file. None unless the
// charge started at kFullLevel or more, the level dropped by kMinDrop points
// or more, the runtime is kMinRuntimeS or more and, with both clocks set, the
// health file is the later one.
std::optional<double> estimateHours(const ChargeFile &charge, const HealthFile &health);

// The middle one of three values.
double median3(double a, double b, double c);

// Section 6.3: the median of the last 3 estimates, none with fewer than 3.
std::optional<double> currentEstimate(const QList<double> &estimates);

// Section 6.3: the largest median of 3 estimates in a row, none with fewer
// than 3.
std::optional<double> referenceEstimate(const QList<double> &estimates);

// Section 6.3. `chargingError` is the last health file saying `charging:
// error`; `estimates` are those of the history, in the order they were read.
BatteryVerdict batteryVerdict(bool chargingError, const QList<double> &estimates);

} // namespace tacetos::health
