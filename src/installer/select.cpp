// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "select.h"

namespace tacetos::installer {

Selection selectIpod(const QList<DiskCandidate> &candidates)
{
    Selection s;
    for (int i = 0; i < candidates.size(); ++i) {
        const DiskCandidate &c = candidates.at(i);
        if (c.permissionDenied) {
            ++s.permissionRefused;
            continue;
        }
        // A disk that cannot be opened for another reason is skipped like any
        // disk that is not an iPod.
        if (!c.device)
            continue;
        FoundIpod f;
        f.index = i;
        f.name = c.name;
        f.description = c.description;
        f.capacityBytes = c.capacityBytes;
        f.recognition = recognise(*c.device);
        if (f.recognition.verdict == Verdict::Recognised)
            s.ipods.append(f);
        else if (f.recognition.verdict == Verdict::UnknownGeneration)
            s.unsupported.append(f);
    }

    if (s.ipods.size() == 1) {
        s.outcome = SelectOutcome::One;
    } else if (s.ipods.size() > 1) {
        s.outcome = SelectOutcome::Several;
    } else if (!candidates.isEmpty() && s.permissionRefused == candidates.size()) {
        s.outcome = SelectOutcome::PermissionDenied;
        s.message = QStringLiteral("Permission denied");
    } else if (!s.unsupported.isEmpty()) {
        s.outcome = SelectOutcome::None;
        s.message = QStringLiteral("An iPod TacetOS does not support");
    } else {
        s.outcome = SelectOutcome::None;
        s.message = QStringLiteral("No iPod found. Is it in Disk Mode?");
    }
    return s;
}

} // namespace tacetos::installer
