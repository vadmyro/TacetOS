// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "system.h"

#include <QString>
#include <QStringList>

// The command `install-udev-rule` (docs/specs/helper.md sections 6.11 and 11.5).

namespace tacetos::helper {

// The rule built into the helper: the same text as `70-tacetos-dfu.rules`. Its last
// line names the file of the polkit rule put in place with it.
QString udevRuleText();

// The same without its last line: what a system with no folder of polkit rules gets.
QString udevRuleTextWithoutPolkitLine();

// The polkit rule built into the helper (section 11.5): one action, a local and
// active session, a removable USB drive with the vendor and model of an iPod.
QString polkitRuleText();

// The fixed script that `pkexec` runs with `/bin/sh`. It takes the paths of the
// copies of the two rules as its only arguments and has nothing the app could
// change. Exit 3: a copy in place differs. Exit 4: no folder of polkit rules, so
// only the udev rule (without its last line) is in place.
QString udevInstallScript();

// The same script for other target files and another owner option (`-o root -g
// root` for the real one), so that a test can run it with `sh` on temporary folders.
QString udevInstallScriptFor(const QString &udevFile, const QString &polkitFile,
                             const QString &ownerOptions);

// The arguments given to `pkexec`: the shell, the script, a name for the shell,
// and the two paths.
QStringList pkexecArguments(const QString &udevCopyPath, const QString &polkitCopyPath);

struct UdevOutcome
{
    bool ok = false;
    // `unsupported-system`, `cancelled`, `permission-denied` or `udev-failed`.
    QString code;
    QString detail;
    bool alreadyInstalled = false;
    // False when the system has no folder of polkit rules: the udev rule alone is in place.
    bool polkit = true;
};

// Section 11.5 steps 1 to 4. The exit codes of `pkexec`: 126 is a closed password
// window, 127 a refused authorisation, 4 a success with no polkit rule, anything
// else but 0 is a failure.
UdevOutcome installUdevRule(const UdevEnv &env);

// A runner that starts the program with `QProcess` and waits for it.
std::unique_ptr<ProcessRunner> makeProcessRunner();

} // namespace tacetos::helper
