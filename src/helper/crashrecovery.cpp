// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "crashrecovery.h"

#include <QCoreApplication>

namespace tacetos::helper {

namespace {

QString say(const char *text)
{
    return QCoreApplication::translate("CrashRecovery", text);
}

} // namespace

CrashRecovery recoveryAfterCrash(const QString &state, bool backupKept)
{
    const QString stopped = say("The helper stopped while writing to the iPod.");
    CrashRecovery r;
    if (state == QLatin1String("clean")) {
        r.next = QStringLiteral("report-state");
        r.message = stopped + QLatin1Char(' ') + say("The iPod has no bootloader and is in order.");
    } else if (state == QLatin1String("installed")) {
        r.next = QStringLiteral("report-state");
        r.message = stopped + QLatin1Char(' ') + say("The iPod has a bootloader and is in order.");
    } else if (state == QLatin1String("inconsistent") && backupKept) {
        r.next = QStringLiteral("offer-restore");
        r.message = stopped + QLatin1Char(' ') +
                    say("The iPod is in a broken state. The backup can be written back.");
    } else {
        r.next = QStringLiteral("none");
        r.message = stopped + QLatin1Char(' ') +
                    say("The iPod may not start. Disk Mode is the way back. Keep the backup file "
                        "and ask on the Discord.");
    }
    return r;
}

} // namespace tacetos::helper
