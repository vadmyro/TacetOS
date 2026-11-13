// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "system.h"

#include <QString>
#include <QStringList>

// The command `install-udev-rule` (docs/specs/helper.md sections 6.11 and 11.5).

namespace tacetos::helper {

// The rule built into the helper: the same text as `70-tacetos-dfu.rules`.
QString udevRuleText();

// The fixed script that `pkexec` runs with `/bin/sh`. It takes the path of the
// copy of the rule as its only argument and has nothing the app could change.
QString udevInstallScript();

// The arguments given to `pkexec`: the shell, the script, a name for the shell,
// and the one path.
QStringList pkexecArguments(const QString &ruleCopyPath);

struct UdevOutcome
{
    bool ok = false;
    // `unsupported-system`, `cancelled`, `permission-denied` or `udev-failed`.
    QString code;
    QString detail;
    bool alreadyInstalled = false;
};

// Section 11.5 steps 1 to 4. The exit codes of `pkexec`: 126 is a closed password
// window, 127 a refused authorisation, anything else but 0 is a failure.
UdevOutcome installUdevRule(const UdevEnv &env);

// A runner that starts the program with `QProcess` and waits for it.
std::unique_ptr<ProcessRunner> makeProcessRunner();

} // namespace tacetos::helper
