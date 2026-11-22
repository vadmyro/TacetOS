// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "classiccmds.h"

#include "constants.h"
#include "dfuaround.h"
#include "dfuimage.h"
#include "helperconstants.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QThread>

#if TACETOS_HELPER_HAS_USB
#include "libusblink.h"
#endif

namespace tacetos::helper {

using namespace tacetos::installer;

namespace {

CommandOutcome failure(const QString &code, const QString &detail, const QJsonObject &data = {})
{
    CommandOutcome o;
    o.error = code;
    o.detail = detail;
    o.data = data;
    return o;
}

bool parseState(const QString &text, BootloaderState *state)
{
    if (text == QLatin1String("present"))
        *state = BootloaderState::Present;
    else if (text == QLatin1String("absent"))
        *state = BootloaderState::Absent;
    else if (text == QLatin1String("unknown"))
        *state = BootloaderState::Unknown;
    else
        return false;
    return true;
}

// The names of the stages of section 10.1.
QString stageName(ClassicStage stage)
{
    switch (stage) {
    case ClassicStage::Check:
        return QStringLiteral("check");
    case ClassicStage::Eject:
        return QStringLiteral("eject");
    case ClassicStage::Pause:
        return QStringLiteral("spin-down");
    case ClassicStage::WaitDevice:
        return QStringLiteral("wait-device");
    case ClassicStage::Transfer:
        return QStringLiteral("send");
    case ClassicStage::Watch:
    case ClassicStage::Done:
        break;
    }
    return QStringLiteral("resume");
}

QJsonObject namesOf(const QList<SystemProcess> &processes)
{
    QJsonArray names;
    for (const SystemProcess &p : processes)
        names.append(p.name);
    QJsonObject o;
    o.insert(QStringLiteral("names"), names);
    return o;
}

} // namespace

CommandOutcome runClassicDfuOnDisk(const QJsonObject &args, SystemLayer &system,
                                   const RealDisk &disk, CommandHooks &hooks)
{
    hooks.stage(QStringLiteral("check"));
    const QString operation = args.value(QStringLiteral("operation")).toString();
    const bool install = operation == QLatin1String("install");
    if (!install && operation != QLatin1String("uninstall"))
        return failure(err::badRequest, QStringLiteral("the operation is install or uninstall"));
    if (install != args.contains(QStringLiteral("bootloaderFile")))
        return failure(err::badRequest,
                       install ? QStringLiteral("an install needs the bootloader file")
                               : QStringLiteral("an uninstall takes no bootloader file"));
    BootloaderState before = BootloaderState::Unknown;
    if (!parseState(args.value(QStringLiteral("stateBefore")).toString(), &before))
        return failure(err::badRequest,
                       QStringLiteral("stateBefore is present, absent or unknown"));

    // Step 1: the disk is a Classic, in normal mode or Disk Mode.
    if (disk.usbProduct !=
        QStringLiteral("%1").arg(kClassicNormalProductId, 4, 16, QLatin1Char('0')))
        return failure(
            err::notClassic,
            QStringLiteral("the USB product %1 is not an iPod Classic").arg(disk.usbProduct));
    if (system.dfuPartsDir.isEmpty() || !system.classicLayer)
        return failure(err::unavailable,
                       system.dfuPartsDir.isEmpty()
                           ? QStringLiteral("this build has no parts of the Classic image")
                           : QStringLiteral("this build has no USB access"));
    const DfuPartsLoad parts = loadDfuParts(system.dfuPartsDir);
    if (!parts.ok)
        return failure(err::unavailable, parts.message);

    // Step 2: the image is built and checked here; the app never sends one.
    DfuImage image;
    qint64 payload = 0;
    if (install) {
        QFile file(args.value(QStringLiteral("bootloaderFile")).toString());
        if (!file.open(QIODevice::ReadOnly) || file.size() > kBootFileMax)
            return failure(err::checkFailed, QStringLiteral("the bootloader file cannot be read"));
        image = buildInstallerImage(parts.parts, file.readAll());
        payload = parts.parts.installerPayload.size();
    } else {
        image = buildUninstallerImage(parts.parts);
        payload = parts.parts.uninstallerPayload.size();
    }
    if (!image.ok)
        return failure(err::checkFailed, image.message);

    // Section 9: a development build stops here. It does not eject the iPod or ask
    // for DFU mode, since it would send nothing.
    if (!system.realWrites)
        return failure(err::devBuild, QStringLiteral("this build does not write to real iPods"));
    std::unique_ptr<ClassicSystemLayer> layer = system.classicLayer(system, disk.blockPath);
    if (!layer)
        return failure(err::unavailable, QStringLiteral("this build has no USB access"));

    ClassicDfuJob job;
    job.operation = install ? ClassicOperation::Install : ClassicOperation::Uninstall;
    job.image = image.image;
    job.payloadLength = payload;
    job.hasHardDisk = true;
    job.stateFile =
        QDir(args.value(QStringLiteral("dataDir")).toString()).filePath(QString(kPausedStateFile));
    job.stateBefore = before;
    // Step 8, the iPod coming back, is the detection of the app (section 6.9).
    job.watchAfter = false;
    if (system.sleep)
        job.send.sleep = system.sleep;

    AroundOptions options;
    QElapsedTimer inStage;
    inStage.start();
    qint64 lastSecond = -1;
    bool sending = false;
    options.sleep = [&](int ms) {
        if (system.sleep)
            system.sleep(ms);
        else
            QThread::msleep(ulong(ms));
        // One `progress` per second of a wait, so that the app shows the countdown.
        const qint64 second = inStage.elapsed() / 1000;
        if (second != lastSecond) {
            lastSecond = second;
            hooks.progress(second, 0);
        }
    };
    options.cancelled = [&] { return !sending && hooks.cancelRequested(); };
    options.notify = [&](const QString &text) { hooks.log(QStringLiteral("info"), text); };
    options.stage = [&](ClassicStage stage) {
        inStage.restart();
        lastSecond = -1;
        if (stage == ClassicStage::Transfer) {
            // The transfer cannot be stopped half way: no cancel from here on.
            sending = true;
            hooks.beforeFirstWrite();
        }
        hooks.stage(stageName(stage));
    };
    options.quiet = [&] {
        inStage.restart();
        lastSecond = -1;
        hooks.stage(QStringLiteral("quiet"));
    };

    const ClassicDfuOutcome out = runClassicDfu(*layer, job, options);
    hooks.stage(QStringLiteral("resume"));
    if (out.ok) {
        QJsonObject data;
        data.insert(QStringLiteral("operation"), operation);
        data.insert(QStringLiteral("imageBytes"), double(job.image.size()));
        CommandOutcome o;
        o.ok = true;
        o.data = data;
        return o;
    }
    if (out.cancelled)
        return failure(err::cancelled, out.message);
    const QString detail =
        out.hint.isEmpty() ? out.message : out.message + QLatin1Char(' ') + out.hint;
    switch (out.stage) {
    case ClassicStage::Check:
        return failure(err::checkFailed, detail);
    case ClassicStage::Eject:
        return failure(err::inUse, detail);
    default:
        break;
    }
    QJsonObject data;
    data.insert(QStringLiteral("stage"), stageName(out.stage));
    data.insert(QStringLiteral("hint"), out.hint);
    // For the log only: which USB call failed, and how, under the message for the user.
    if (!out.transfer.detail.isEmpty())
        data.insert(QStringLiteral("usbDetail"), out.transfer.detail);
    return failure(err::dfuFailed, detail, data);
}

CommandOutcome runResumePaused(const QJsonObject &args, SystemLayer &system)
{
    const QString stateFile =
        QDir(args.value(QStringLiteral("dataDir")).toString()).filePath(QString(kPausedStateFile));
    CommandOutcome o;
    o.ok = true;
    // Nothing was paused: the usual case, and always the case on Linux.
    if (!QFile::exists(stateFile) || !system.classicLayer) {
        o.data = namesOf({});
        return o;
    }
    std::unique_ptr<ClassicSystemLayer> layer = system.classicLayer(system, QString());
    if (!layer) {
        o.data = namesOf({});
        return o;
    }
    const ResumeResult r = resumeLeftover(*layer, stateFile);
    o.data = namesOf(r.resumed);
    if (!r.failed.isEmpty())
        return failure(err::unsupportedSystem,
                       QStringLiteral("%1 programs are still paused").arg(r.failed.size()),
                       namesOf(r.failed));
    return o;
}

#if TACETOS_HELPER_HAS_USB
namespace {

// Linux: udisks2 ejects the iPod, libusb sees the bus. No program of the system
// holds an iPod in DFU mode, so nothing is paused (bootloader-classic.md 15.2).
class LinuxClassicLayer : public ClassicSystemLayer
{
public:
    LinuxClassicLayer(SystemLayer &system, QString blockPath)
        : m_system(system), m_block(std::move(blockPath))
    {
    }

    HostSystem hostSystem() const override { return HostSystem::Linux; }

    QList<UsbDeviceInfo> listUsbDevices() override
    {
        QString error;
        m_seen = scanDfuDevices(&error);
        QList<UsbDeviceInfo> out;
        for (const UsbBusDevice &d : m_seen) {
            UsbDeviceInfo info;
            info.id = d.id;
            info.bus = d.bus;
            info.address = d.address;
            out.append(info);
        }
        return out;
    }

    std::unique_ptr<UsbDfuLink> makeLink(const UsbDeviceInfo &device) override
    {
        for (const UsbBusDevice &d : m_seen) {
            if (d.bus == device.bus && d.address == device.address)
                return std::make_unique<LibusbDfuLink>(d);
        }
        UsbBusDevice d;
        d.id = device.id;
        d.bus = device.bus;
        d.address = device.address;
        return std::make_unique<LibusbDfuLink>(d);
    }

    bool unmountIpod(QString *error) override
    {
        if (!m_system.disks)
            return false;
        const ServiceResult unmounted = m_system.disks->unmountAll(m_block);
        const ServiceResult off = unmounted.ok() ? m_system.disks->powerOff(m_block) : unmounted;
        if (!off.ok() && error)
            *error = off.detail;
        return off.ok();
    }

    bool ipodListed() override { return m_system.disks && m_system.disks->listed(m_block); }
    QList<SystemProcess> findAppleProcesses() override { return {}; }
    bool pauseProcess(const SystemProcess &, QString *) override { return true; }
    bool resumeProcess(const SystemProcess &, QString *) override { return true; }

private:
    SystemLayer &m_system;
    QString m_block;
    QList<UsbBusDevice> m_seen;
};

} // namespace

std::unique_ptr<ClassicSystemLayer> makeLinuxClassicLayer(SystemLayer &system,
                                                          const QString &blockPath)
{
    return std::make_unique<LinuxClassicLayer>(system, blockPath);
}
#else
std::unique_ptr<ClassicSystemLayer> makeLinuxClassicLayer(SystemLayer &, const QString &)
{
    return nullptr;
}
#endif

} // namespace tacetos::helper
