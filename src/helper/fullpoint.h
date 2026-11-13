// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "filefault.h"

// Test builds only (docs/specs/helper.md section 15.2): makes a folder of the
// computer answer "no space left" to the helper. Like `killpoint.h`, without
// `TACETOS_TEST_KILLPOINTS` nothing here reads an environment variable: the normal
// helper always answers an empty fault, which means no fault.
//
// `TACETOS_TEST_FULL` is `backup` (the copies of the firmware partition and of the
// map) or `record` (the install record). A copy fails after its first piece, so a
// half-written file has to be removed; the record fails at once.

#if defined(TACETOS_TEST_KILLPOINTS)
#include <QByteArray>
#include <QString>
#endif

namespace tacetos::helper {

#if defined(TACETOS_TEST_KILLPOINTS)
inline installer::FileFault testFault(const char *what)
{
    if (qgetenv("TACETOS_TEST_FULL") != what)
        return {};
    // A copy has its first piece written: the disk fills up while it runs.
    const bool afterFirstPiece = qstrcmp(what, "backup") == 0;
    return [afterFirstPiece](const QString &, qint64 written) {
        return (written > 0 || !afterFirstPiece) ? QString::fromLatin1(installer::kNoSpace)
                                                 : QString();
    };
}
#else
inline installer::FileFault testFault(const char *)
{
    return {};
}
#endif

} // namespace tacetos::helper
