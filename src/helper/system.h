// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "checks.h"
#include "dfuaround.h"
#include "dfuimage.h"
#include "udisks.h"

#include <QString>
#include <functional>
#include <memory>
#include <optional>

// What the helper asks of the computer (docs/specs/helper.md section 11), behind
// small interfaces so that the tests give recorded answers and never reach
// udisks2, pkexec or a disk.

namespace tacetos::helper {

// An open file of a disk (section 11.1), or the reason there is none.
struct OpenResult
{
    // -1 when the disk could not be opened. The caller owns the descriptor.
    int fd = -1;
    // `permission-denied`, `in-use` or `open-failed` (section 7.1 step 6).
    QString code;
    QString detail;
};

// The answer of the disk service to a change it was asked for.
struct ServiceResult
{
    // Empty when it was done; otherwise `permission-denied`, `in-use`, `disk-gone`
    // or `open-failed`.
    QString code;
    QString detail;
    bool ok() const { return code.isEmpty(); }
};

// The disk service of the system (udisks2).
class DiskService
{
public:
    virtual ~DiskService() = default;
    // The version of the service, empty when it cannot be reached.
    virtual QString version() = 0;
    // Every object of the service. None, with the reason in `error`, when it
    // cannot be reached.
    virtual std::optional<tacetos::device::UdisksObjects> objects(QString *error) = 0;
    // `OpenDevice` of the block device object `blockPath`: mode read only, or
    // read and write after the checks (section 11.1).
    virtual OpenResult open(const QString &blockPath, bool write) = 0;
    // Unmounts every mounted file system of the disk whose block device object is
    // `blockPath`, without the force option (section 11.3). Done when nothing was
    // mounted. Stops at the first refusal.
    virtual ServiceResult unmountAll(const QString &blockPath) = 0;
    // Cuts the power of the drive of that disk, as an eject does (section 11.3).
    virtual ServiceResult powerOff(const QString &blockPath) = 0;
    // Whether the service still lists the disk.
    virtual bool listed(const QString &blockPath) = 0;
};

struct SystemLayer;

// Makes the layer under the Classic transfer for the disk `blockPath` (section 6.9),
// or null when this build has no USB access.
using ClassicLayerMaker = std::function<std::unique_ptr<installer::ClassicSystemLayer>(
    SystemLayer &system, const QString &blockPath)>;

// Runs a program and gives its exit code (-1 when it could not be started).
class ProcessRunner
{
public:
    virtual ~ProcessRunner() = default;
    virtual int run(const QString &program, const QStringList &arguments, QString *error) = 0;
};

struct UdevEnv
{
    // `/etc/udev/rules.d/70-tacetos-dfu.rules` on a real system.
    QString rulesFile;
    // The path of `pkexec`, empty when it is missing (section 11.5).
    QString pkexecPath;
    // Where the helper makes its temporary folder.
    QString tempBase;
    ProcessRunner *runner = nullptr;
};

// The whole layer under the commands. The default values are those of a real
// Linux system.
struct SystemLayer
{
    // Null when this build or system has no disk service.
    std::unique_ptr<DiskService> disks;
    QString sysRoot = QStringLiteral("/sys");
    QString mountinfo = QStringLiteral("/proc/self/mountinfo");
    QString swaps = QStringLiteral("/proc/swaps");
    UdevEnv udev;
    // True on Linux, the only system with this layer so far.
    bool linuxSystem = false;
    // The gate of section 9: whether a write to a real disk is allowed. Always
    // the build option; only the tests of the helper loop, which run it in their
    // own process on image files, set it to true. No request, file or variable
    // reaches it.
    bool realWrites = installer::realWritesAllowed();
    // Section 6.9: the USB side of the Classic transfer. Empty without USB access.
    ClassicLayerMaker classicLayer;
    // The generated parts of the Classic image (bootloader-classic.md section 8).
    // Empty when this build has none: the Classic install is not available.
    QString dfuPartsDir = installer::builtInDfuPartsDir();
    // How the Classic transfer waits; empty for the real clock. The tests move a
    // clock of their own instead.
    std::function<void(int milliseconds)> sleep;
};

// The real layer: udisks2 over D-Bus, the real files, the real `pkexec`.
std::unique_ptr<SystemLayer> makeRealSystem();

} // namespace tacetos::helper
