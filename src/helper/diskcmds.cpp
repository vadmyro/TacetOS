// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "diskcmds.h"

#include "fddevice.h"
#include "helperconstants.h"
#include "linuxdisks.h"
#include "linuxfacts.h"
#include "scsisheet.h"
#include "udevrule.h"

#include <QFile>
#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

namespace tacetos::helper {

namespace {

CommandOutcome failure(const QString &code, const QString &detail)
{
    CommandOutcome o;
    o.error = code;
    o.detail = detail;
    return o;
}

} // namespace

RealDisk checkRealDisk(const QJsonObject &target, const QString &dataDir, SystemLayer &system)
{
    RealDisk out;
    auto refuse = [&](const QString &code, const QString &detail) {
        out.code = code;
        out.detail = detail;
        return out;
    };
    if (!system.linuxSystem || !system.disks)
        return refuse(err::unsupportedSystem, QStringLiteral("this build has no disk service"));
    if (!udisksVersionSupportsOpenDevice(system.disks->version()))
        return refuse(err::unsupportedSystem, QStringLiteral("udisks2 2.7.3 or newer is needed"));

    DiskRequest request;
    request.node = target.value(QStringLiteral("node")).toString();
    request.sizeBytes = qint64(target.value(QStringLiteral("sizeBytes")).toDouble());
    request.serial = target.value(QStringLiteral("serial")).toString();
    request.port = target.value(QStringLiteral("port")).toString();

    QString error;
    const auto objects = system.disks->objects(&error);
    if (!objects)
        return refuse(err::unsupportedSystem,
                      QStringLiteral("udisks2 cannot be reached: ") + error);
    // Everything is read from the system now; nothing comes from the request.
    DiskInputs inputs;
    inputs.block = blockFactsFromObjects(*objects, request.node);
    inputs.usb = readUsbFacts(system.sysRoot, inputs.block.major, inputs.block.minor);
    inputs.mounts = tacetos::device::readMountinfo(system.mountinfo);
    QFile swaps(system.swaps);
    if (swaps.open(QIODevice::ReadOnly))
        inputs.swaps = QString::fromUtf8(swaps.readAll());
    inputs.dataDir = dataDir;

    const DiskVerdict verdict = judgeDisk(request, inputs);
    if (!verdict.ok)
        return refuse(verdict.code, verdict.detail);
    out.ok = true;
    out.lockKey = QStringLiteral("disk:%1:%2").arg(inputs.usb.port, inputs.usb.serial);
    out.blockPath = inputs.block.blockPath;
    out.sizeBytes = inputs.block.sizeBytes;
    out.usbBus = inputs.usb.busNumber;
    out.usbDevice = inputs.usb.deviceNumber;
    return out;
}

#if defined(Q_OS_WIN)
// The layer of Windows is not made yet (section 12.1).
CommandOutcome readStateOnDisk(SystemLayer &, const RealDisk &, const LogFn &, const StageFn &)
{
    return failure(err::unsupportedSystem, QStringLiteral("no disk service on this system yet"));
}

CommandOutcome readSheetOnDisk(SystemLayer &, const RealDisk &, const LogFn &)
{
    return failure(err::unsupportedSystem, QStringLiteral("no disk service on this system yet"));
}
#else
CommandOutcome readStateOnDisk(SystemLayer &system, const RealDisk &disk, const LogFn &log,
                               const StageFn &stage)
{
    const OpenResult opened = system.disks->open(disk.blockPath, false);
    if (opened.fd < 0)
        return failure(opened.code, opened.detail);
    FdSectorDevice dev(opened.fd, disk.sizeBytes, false);
    return readState(dev, log, stage, disk.usbBus, disk.usbDevice);
}

CommandOutcome readSheetOnDisk(SystemLayer &system, const RealDisk &disk, const LogFn &log)
{
    const OpenResult opened = system.disks->open(disk.blockPath, false);
    if (opened.fd < 0)
        return failure(opened.code, opened.detail);
    SheetResult sheet;
#if defined(Q_OS_LINUX)
    sheet = readDeviceSheet(sgInquiry(opened.fd));
#else
    sheet.code = QString(err::unsupportedSystem);
#endif
    ::close(opened.fd);
    if (!sheet.ok) {
        log(QStringLiteral("info"), sheet.detail);
        return failure(sheet.code, sheet.detail);
    }
    CommandOutcome o;
    o.ok = true;
    o.data.insert(QStringLiteral("text"), sheet.text);
    return o;
}

#endif

CommandOutcome runInstallUdevRule(SystemLayer *system)
{
    if (!system || !system->linuxSystem)
        return failure(err::unsupportedSystem, QStringLiteral("the udev rule is for Linux"));
    const UdevOutcome r = installUdevRule(system->udev);
    if (!r.ok)
        return failure(r.code, r.detail);
    CommandOutcome o;
    o.ok = true;
    o.data.insert(QStringLiteral("alreadyInstalled"), r.alreadyInstalled);
    return o;
}

} // namespace tacetos::helper
