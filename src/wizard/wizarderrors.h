// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>

// The error codes of the wizard (docs/specs/install-wizard.md section 13). The
// codes of the helper are passed on as they are.

namespace tacetos::wizard::error {

inline constexpr QLatin1String noSerial("no-serial");
inline constexpr QLatin1String flowRunning("flow-running");
inline constexpr QLatin1String progressRefused("progress-refused");
inline constexpr QLatin1String progressMismatch("progress-mismatch");
inline constexpr QLatin1String noProgress("no-progress");
inline constexpr QLatin1String badFlow("bad-flow");
inline constexpr QLatin1String cancelled("cancelled");
inline constexpr QLatin1String ipodGone("ipod-gone");
inline constexpr QLatin1String checkBlocking("check-blocking");
inline constexpr QLatin1String backupFullDisk("backup-full-disk");
inline constexpr QLatin1String backupReadFailed("backup-read-failed");
inline constexpr QLatin1String backupVerifyFailed("backup-verify-failed");
inline constexpr QLatin1String sourceChanged("source-changed");
inline constexpr QLatin1String backupNameRefused("backup-name-refused");
inline constexpr QLatin1String backupNotVerified("backup-not-verified");
inline constexpr QLatin1String backupDamaged("backup-damaged");
inline constexpr QLatin1String restoreWriteFailed("restore-write-failed");
inline constexpr QLatin1String volumeNotBack("volume-not-back");
inline constexpr QLatin1String eraseFailed("erase-failed");
inline constexpr QLatin1String ipodNotBack("ipod-not-back");
inline constexpr QLatin1String verifyFailed("verify-failed");
inline constexpr QLatin1String packageFormat("package-format");
inline constexpr QLatin1String packageWrongModel("package-wrong-model");
inline constexpr QLatin1String packageIncomplete("package-incomplete");
inline constexpr QLatin1String packageDamaged("package-damaged");
inline constexpr QLatin1String packageChanged("package-changed");
inline constexpr QLatin1String firmwareNoRoom("firmware-no-room");
inline constexpr QLatin1String firmwareWriteFailed("firmware-write-failed");
inline constexpr QLatin1String bootloaderRestored("bootloader-restored");
inline constexpr QLatin1String diskModeInstead("disk-mode-instead");
inline constexpr QLatin1String noDisk("no-disk");
inline constexpr QLatin1String dfuFailed("dfu-failed");
inline constexpr QLatin1String bootloaderNotTaken("bootloader-not-taken");
inline constexpr QLatin1String backInDfu("back-in-dfu");
inline constexpr QLatin1String wrongMode("wrong-mode");
inline constexpr QLatin1String stillTacetos("still-tacetos");
inline constexpr QLatin1String nothingToDo("nothing-to-do");

} // namespace tacetos::wizard::error
