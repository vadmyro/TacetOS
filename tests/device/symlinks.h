// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Some tests build fake trees with real symbolic links (the sysfs tree, a link
// out of a volume). On Windows QFile::link makes .lnk shortcuts, which are no
// links for the code under test: those tests are skipped there.

#pragma once

#include <QTest>

#ifdef Q_OS_WIN
#define TACETOS_NEEDS_SYMLINKS() QSKIP("QFile::link makes .lnk shortcuts on Windows")
#else
#define TACETOS_NEEDS_SYMLINKS() static_cast<void>(0)
#endif
