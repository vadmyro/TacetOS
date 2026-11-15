// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>

// The error codes of the wizard (docs/specs/install-wizard.md section 13). The
// codes of the helper are passed on as they are.

namespace tacetos::wizard::error {

inline constexpr QLatin1String noSerial("no-serial");
inline constexpr QLatin1String flowRunning("flow-running");
inline constexpr QLatin1String progressRefused("progress-refused");
inline constexpr QLatin1String progressMismatch("progress-mismatch");
inline constexpr QLatin1String noProgress("no-progress");
inline constexpr QLatin1String badFlow("bad-flow");
inline constexpr QLatin1String cancelled("cancelled");
inline constexpr QLatin1String ipodGone("ipod-gone");
inline constexpr QLatin1String checkBlocking("check-blocking");

} // namespace tacetos::wizard::error
