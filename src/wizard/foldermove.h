// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <QStringList>
#include <functional>

// The firmware folder of an iPod Classic moved from the default name to the
// folder of its model (docs/specs/firmware-branding.md section 8.3). Each
// function does nothing when its work is already done, so that a cut run can
// run again.

namespace tacetos::wizard {

// Step 1: true when an install must update the bootloader before the firmware.
// The default folder is on the volume, `folder` is another name, and the last
// install record of `serial` in `records` does not say that its bootloader
// knows `folder`. False when the last record is an uninstall: no bootloader is
// there to miss the new folder.
bool bootloaderFirst(const QString &volumeRoot, const QString &folder, const QStringList &records,
                     const QString &serial);

// Step 2: the folder `from` of the volume renamed to `to`, or, when both are
// there, the files of `from` that `to` lacks moved in, and `from` removed once
// empty (a file in both keeps the copy of `to`). `beforeWrite` is called once,
// before the first change. Empty when done, else what failed.
QString moveFirmwareFolder(const QString &volumeRoot, const QString &from, const QString &to,
                           const std::function<void()> &beforeWrite = {});

// Step 4: in the settings files and the screen files of the firmware folder
// `folderPath`, each path that starts with the folder `from` starts with `to`.
// A file is written only when it changes, in one move. `beforeWrite` is
// called once, before the first write. Empty when done, else what failed.
QString rewriteFolderPaths(const QString &folderPath, const QString &from, const QString &to,
                           const std::function<void()> &beforeWrite = {});

// The text of one file with its paths rewritten, as `rewriteFolderPaths` does.
QByteArray rewrittenPaths(const QByteArray &text, const QString &from, const QString &to);

} // namespace tacetos::wizard
