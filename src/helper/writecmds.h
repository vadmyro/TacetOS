// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "diskcmds.h"
#include "readstate.h"
#include "target.h"

#include <QJsonObject>
#include <QString>

// The commands of the older iPods (docs/specs/helper.md sections 6.4 to 6.8):
// backup, install, update, uninstall, restore, format and conversion. Each one
// opens the disk for reading, checks the scan token and the state that was shown
// to the user, and only then opens it for writing and gives the work to
// `tacet_installer`.

namespace tacetos::helper {

// What a running command needs from the loop of the helper.
class CommandHooks
{
public:
    virtual ~CommandHooks() = default;
    virtual void log(const QString &level, const QString &text) = 0;
    // A change of stage (section 10.1).
    virtual void stage(const QString &name) = 0;
    // Bytes done in the stage, 0 for `total` when not known. Already throttled.
    virtual void progress(qint64 done, qint64 total) = 0;
    // A stopping point (section 10.2): true when a cancel was asked and the
    // command may still stop.
    virtual bool cancelRequested() = 0;
    // Right before the first write (section 10.3). False when a cancel was asked
    // after all: nothing is written. True: the command can no longer be cancelled.
    virtual bool beforeFirstWrite() = 0;
};

// Whether `name` is one of the commands of this file.
bool isLegacyCommand(const QString &name);

// Section 6.7 and 6.8: the typed name against the one of the dialog, before
// anything else. Empty when it matches, the reason when not.
QString checkConfirmation(const QString &cmd, const QJsonObject &args);

// Section 5.4: absolute, a folder or made now. Empty when it is fine.
QString checkDataDir(const QString &dir);

// Runs the command on an image target. `args` has passed `checkArguments`.
CommandOutcome runLegacyCommand(const QString &cmd, const QJsonObject &args,
                                const ResolvedTarget &target, CommandHooks &hooks);

// The same on a real disk that passed step 4 of section 7.1 (`checkRealDisk`). The
// backup is made; for the other commands the checks run on a disk opened for
// reading only, and then a development build answers `dev-build`. No command
// opens a real disk for writing yet.
CommandOutcome runLegacyCommandOnDisk(const QString &cmd, const QJsonObject &args,
                                      SystemLayer &system, const RealDisk &disk,
                                      CommandHooks &hooks);

} // namespace tacetos::helper
