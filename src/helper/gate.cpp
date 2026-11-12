// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "gate.h"

#include "checks.h"

namespace tacetos::helper {

bool writeAllowed(const installer::SectorDevice &dev, bool realWritesBuild)
{
    return !dev.isRealDisk() || realWritesBuild;
}

bool writeAllowed(const installer::SectorDevice &dev)
{
    return writeAllowed(dev, installer::realWritesAllowed());
}

} // namespace tacetos::helper
