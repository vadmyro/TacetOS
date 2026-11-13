// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <functional>

// A way for the tests to make a write to a file of the computer fail, as a full
// disk does (the backup, the install record, the list of paused programs). It is
// asked before each piece written, with the path and the bytes already written;
// an empty answer lets the write go on, any other text is the error of the write.
// Nothing sets it in the app: only the tests, and the test program of the helper.

namespace tacetos::installer {

using FileFault = std::function<QString(const QString &path, qint64 written)>;

inline constexpr char kNoSpace[] = "No space left on device";

} // namespace tacetos::installer
