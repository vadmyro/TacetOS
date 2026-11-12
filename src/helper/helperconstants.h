// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>
#include <QtGlobal>

// Every fixed value of docs/specs/helper.md section 14, and the error codes of
// section 13, in one file, with the names of the spec.

namespace tacetos::helper {

inline constexpr int kProtocol = 1;
inline constexpr int kHelloTimeoutMs = 10000;
inline constexpr int kMaxRequestLine = 65536;
inline constexpr int kProgressIntervalMs = 250;
inline constexpr int kExitNormal = 0;
inline constexpr int kExitCommandLine = 2;
inline constexpr int kExitOutputClosed = 3;
// Section 3.1 rule 3: sizes stay below 2^53 so that every JSON reader keeps them exact.
inline constexpr qint64 kMaxJsonInteger = qint64(1) << 53;

// The sector sizes of an image file (section 5.1).
inline constexpr int kImageSectorSizes[] = {512, 2048, 4096};

// Section 13. The last three are not in the spec's table: they are what the
// client says when there is no helper to answer (sections 2.3, 3.2).
namespace err {
inline constexpr QLatin1String badRequest("bad-request");
inline constexpr QLatin1String unknownCommand("unknown-command");
inline constexpr QLatin1String busy("busy");
inline constexpr QLatin1String badTarget("bad-target");
inline constexpr QLatin1String badDataDir("bad-data-dir");
inline constexpr QLatin1String diskGone("disk-gone");
inline constexpr QLatin1String diskChanged("disk-changed");
inline constexpr QLatin1String stateChanged("state-changed");
inline constexpr QLatin1String notRemovableUsb("not-removable-usb");
inline constexpr QLatin1String notAnIpod("not-an-ipod");
inline constexpr QLatin1String systemDisk("system-disk");
inline constexpr QLatin1String unsupportedModel("unsupported-model");
inline constexpr QLatin1String diskLocked("disk-locked");
inline constexpr QLatin1String permissionDenied("permission-denied");
inline constexpr QLatin1String inUse("in-use");
inline constexpr QLatin1String openFailed("open-failed");
inline constexpr QLatin1String devBuild("dev-build");
inline constexpr QLatin1String cancelled("cancelled");
inline constexpr QLatin1String helperCrashed("helper-crashed");
inline constexpr QLatin1String helperMissing("helper-missing");
inline constexpr QLatin1String helperMismatch("helper-mismatch");
inline constexpr QLatin1String helperSilent("helper-silent");
} // namespace err

// The names of the closed list of commands (section 6.1) that exist so far.
namespace cmd {
inline constexpr QLatin1String readState("read-state");
} // namespace cmd

} // namespace tacetos::helper
