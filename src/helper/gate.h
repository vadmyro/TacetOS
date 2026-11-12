// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "sectordevice.h"

// The gate of development builds (docs/specs/helper.md section 9): a write to
// a real disk is refused unless the build was made with TACETOS_ALLOW_REAL_WRITES.
// An image file is always allowed. Reading is never gated.

namespace tacetos::helper {

// Whether this build may write to `dev`. Every write command asks it right
// before the first write, after the checks (section 7.1 step 9), and answers
// `dev-build` when it is false.
bool writeAllowed(const installer::SectorDevice &dev, bool realWritesBuild);

// The same, with the build option of this program.
bool writeAllowed(const installer::SectorDevice &dev);

} // namespace tacetos::helper
