// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>

// What the app says and offers after a write command ended with `helper-crashed`
// (docs/specs/helper.md section 3.7 and section 13): the same as for "a directory
// write" in `bootloader-legacy.md` section 19. The app has read the state of the
// iPod again, and gives the name of the state here. Nothing is started by this:
// the user chooses the next operation.

namespace tacetos::helper {

struct CrashRecovery
{
    // The text for the screen, already translated.
    QString message;
    // `report-state` (say what the iPod is, offer nothing else), `offer-restore`
    // (offer to write the backup back) or `none`. The words are those of the
    // `next` field of a failed write.
    QString next;
};

// `state` is `clean`, `installed`, `inconsistent`, or empty when the state could
// not be read. `backupKept` is whether the data folder holds a backup.
CrashRecovery recoveryAfterCrash(const QString &state, bool backupKept);

} // namespace tacetos::helper
