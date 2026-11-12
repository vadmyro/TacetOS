// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "dfuaround.h"

#include "constants.h"
#include "dfuimage.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>

namespace tacetos::installer {

namespace {

void pause(const AroundOptions &options, int milliseconds)
{
    if (options.sleep)
        options.sleep(milliseconds);
    else
        QThread::msleep(quint32(milliseconds));
}

bool isCancelled(const AroundOptions &options)
{
    return options.cancelled && options.cancelled();
}

void tell(const AroundOptions &options, const QString &text)
{
    if (options.notify)
        options.notify(text);
}

QList<UsbDeviceId> idsOf(const QList<UsbDeviceInfo> &devices)
{
    QList<UsbDeviceId> ids;
    for (const UsbDeviceInfo &d : devices)
        ids.append(d.id);
    return ids;
}

QStringList namesOf(const QList<SystemProcess> &processes)
{
    QStringList names;
    for (const SystemProcess &p : processes)
        names.append(p.name);
    return names;
}

bool writeStateFile(const QString &path, const QList<SystemProcess> &processes)
{
    QJsonArray list;
    for (const SystemProcess &p : processes) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), double(p.id));
        o.insert(QStringLiteral("name"), p.name);
        list.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("paused"), list);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson());
    return f.commit();
}

QList<SystemProcess> readStateFile(const QString &path)
{
    QList<SystemProcess> processes;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return processes;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray list = doc.object().value(QStringLiteral("paused")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        SystemProcess p;
        p.id = qint64(o.value(QStringLiteral("id")).toDouble());
        p.name = o.value(QStringLiteral("name")).toString();
        if (p.id > 0 && !p.name.isEmpty())
            processes.append(p);
    }
    return processes;
}

DfuWaitResult waitFailed(const QString &message)
{
    DfuWaitResult r;
    r.message = message;
    return r;
}

} // namespace

EjectResult ejectIpod(ClassicSystemLayer &layer, const AroundOptions &options)
{
    EjectResult r;
    for (;;) {
        if (isCancelled(options)) {
            r.cancelled = true;
            r.message = QStringLiteral("Cancelled.");
            return r;
        }
        ++r.attempts;
        QString error;
        if (layer.unmountIpod(&error))
            break;
        if (r.attempts >= kEjectMaxAttempts) {
            r.message = QStringLiteral("The iPod could not be ejected. Close the programs using "
                                       "the iPod, then try again.");
            return r;
        }
        tell(options, QStringLiteral("Close the programs using the iPod."));
        pause(options, kEjectRetryMs);
    }
    // The iPod must be gone from the system before it resets into DFU mode.
    for (int waited = 0; layer.ipodListed(); waited += kEjectListedPollMs) {
        if (isCancelled(options)) {
            r.cancelled = true;
            r.message = QStringLiteral("Cancelled.");
            return r;
        }
        if (waited >= kEjectListedTimeoutMs) {
            r.message = QStringLiteral("The system still lists the iPod. Close the programs "
                                       "using it, then try again.");
            return r;
        }
        pause(options, kEjectListedPollMs);
    }
    r.ok = true;
    return r;
}

int spinDownWaitMs(bool hasHardDisk)
{
    return hasHardDisk ? kSpinDownMs : 0;
}

PausedProcesses::PausedProcesses(ClassicSystemLayer &layer, QString stateFile)
    : m_layer(layer), m_stateFile(std::move(stateFile))
{
}

PausedProcesses::~PausedProcesses()
{
    resumeAll();
}

PauseResult PausedProcesses::pauseAll()
{
    PauseResult r;
    const QList<SystemProcess> found = m_layer.findAppleProcesses();
    if (found.isEmpty()) {
        r.ok = true;
        return r;
    }
    // The file comes first: a crash after the first pause must not lose it.
    if (!m_stateFile.isEmpty() && !writeStateFile(m_stateFile, found)) {
        r.message = QStringLiteral("The list of paused programs could not be saved, so none "
                                   "was paused. Close Apple's programs and try again.");
        return r;
    }
    for (const SystemProcess &p : found) {
        QString error;
        if (m_layer.pauseProcess(p, &error)) {
            m_paused.append(p);
            r.names.append(p.name);
            continue;
        }
        resumeAll();
        r.names.clear();
        r.message = QStringLiteral("%1 could not be paused. Stop it and try again.").arg(p.name);
        return r;
    }
    r.ok = true;
    return r;
}

void PausedProcesses::resumeAll()
{
    QList<SystemProcess> stuck;
    for (const SystemProcess &p : std::as_const(m_paused)) {
        QString error;
        if (!m_layer.resumeProcess(p, &error))
            stuck.append(p);
    }
    m_paused = stuck;
    if (m_stateFile.isEmpty())
        return;
    // What the system refused to resume stays in the file for the next start.
    if (stuck.isEmpty())
        QFile::remove(m_stateFile);
    else
        writeStateFile(m_stateFile, stuck);
}

ResumeResult resumeLeftover(ClassicSystemLayer &layer, const QString &stateFile)
{
    ResumeResult r;
    const QList<SystemProcess> listed = readStateFile(stateFile);
    if (listed.isEmpty()) {
        QFile::remove(stateFile);
        return r;
    }
    const QList<SystemProcess> running = layer.findAppleProcesses();
    for (const SystemProcess &p : listed) {
        if (!running.contains(p)) {
            r.gone.append(p);
            continue;
        }
        QString error;
        if (layer.resumeProcess(p, &error))
            r.resumed.append(p);
        else
            r.failed.append(p);
    }
    if (r.failed.isEmpty())
        QFile::remove(stateFile);
    else
        writeStateFile(stateFile, r.failed);
    return r;
}

DfuWaitResult waitForQuietDfu(ClassicSystemLayer &layer, const AroundOptions &options)
{
    const QString notAppeared =
        QStringLiteral("The iPod did not appear in DFU mode. Reset it (hold Menu and Select), and "
                       "keep holding both until it appears.");
    DfuWaitResult result;
    for (;;) {
        // Step 1: a single iPod in DFU mode.
        UsbDeviceInfo found;
        for (int waited = 0;; waited += kDfuAppearPollMs) {
            if (isCancelled(options)) {
                result.cancelled = true;
                result.message = QStringLiteral("Cancelled.");
                return result;
            }
            const QList<UsbDeviceInfo> list = layer.listUsbDevices();
            const DfuScan scan = chooseDfuDevice(idsOf(list));
            if (scan.status == DfuScanStatus::Found) {
                found = list.at(scan.index);
                break;
            }
            if (scan.status == DfuScanStatus::Several || scan.status == DfuScanStatus::Wtf)
                return waitFailed(scan.message);
            if (waited >= kDfuAppearTimeoutMs)
                return waitFailed(notAppeared);
            pause(options, kDfuAppearPollMs);
        }

        // Section 16.1: the buttons may still be held. Nothing is opened for 10 seconds,
        // and the iPod must stay at the same address.
        bool wasReset = false;
        for (int left = kReleaseButtonsSeconds; left > 0 && !wasReset; --left) {
            tell(options, QStringLiteral("Release the buttons now. %1").arg(left));
            pause(options, 1000);
            if (isCancelled(options)) {
                result.cancelled = true;
                result.message = QStringLiteral("Cancelled.");
                return result;
            }
            const QList<UsbDeviceInfo> list = layer.listUsbDevices();
            const DfuScan scan = chooseDfuDevice(idsOf(list));
            if (scan.status == DfuScanStatus::Several)
                return waitFailed(scan.message);
            wasReset =
                scan.status != DfuScanStatus::Found || !list.at(scan.index).sameSocket(found);
        }
        if (!wasReset) {
            result.ok = true;
            result.device = found;
            return result;
        }
        if (++result.resets >= kMaxDfuResets) {
            DfuWaitResult stop =
                waitFailed(QStringLiteral("The iPod keeps leaving DFU mode. Release the buttons "
                                          "as soon as the message appears."));
            stop.resets = result.resets;
            return stop;
        }
    }
}

ReturnedMode watchAfterTransfer(ClassicSystemLayer &layer, const AroundOptions &options)
{
    // The iPod is still listed in DFU mode for a moment after the last request: that
    // is not a return. Only a `1223` that shows up after it was gone is one.
    bool gone = false;
    for (int waited = 0; waited < kNormalModeWaitMs; waited += kNormalModePollMs) {
        if (isCancelled(options))
            break;
        const QList<UsbDeviceInfo> list = layer.listUsbDevices();
        bool dfu = false;
        bool wtf = false;
        bool anyIpod = false;
        for (const UsbDeviceInfo &d : list) {
            if (d.id.vendor != kAppleVendorId)
                continue;
            anyIpod = true;
            if (d.id.product == kClassicNormalProductId)
                return ReturnedMode::Normal;
            if (d.id.product == kClassicDfuProductId)
                dfu = true;
            else if (chooseDfuDevice({d.id}).status == DfuScanStatus::Wtf)
                wtf = true;
        }
        if (wtf)
            return ReturnedMode::Wtf;
        if (dfu && gone)
            return ReturnedMode::Dfu;
        if (!anyIpod)
            gone = true;
        pause(options, kNormalModePollMs);
    }
    return ReturnedMode::Nothing;
}

AfterTransfer classifyAfterTransfer(ClassicOperation operation, BootloaderState before,
                                    ReturnedMode mode, BootloaderState now)
{
    AfterTransfer r;
    switch (mode) {
    case ReturnedMode::Dfu:
        r.action = AfterAction::ButtonsHeld;
        r.message = QStringLiteral("The iPod went back to DFU mode. Release the buttons as soon "
                                   "as the message appears.");
        return r;
    case ReturnedMode::Wtf:
        r.action = AfterAction::ResetWrongMode;
        r.message = QStringLiteral("The iPod is in the wrong mode. Reset it.");
        return r;
    case ReturnedMode::Nothing:
        r.action = AfterAction::ShowRestore;
        r.message = QStringLiteral("The iPod did not restart. Hold Menu and Select for about 6 "
                                   "seconds to reset it. If that does not help, enter DFU mode "
                                   "and restore it with Apple's software.");
        return r;
    case ReturnedMode::Normal:
        break;
    }
    const BootloaderState expected =
        operation == ClassicOperation::Install ? BootloaderState::Present : BootloaderState::Absent;
    if (now == expected) {
        r.action = AfterAction::Done;
        r.success = true;
        r.message = QStringLiteral("Done");
        r.writeRecord = operation == ClassicOperation::Install;
        r.deleteRecord = operation == ClassicOperation::Uninstall;
    } else if (now != BootloaderState::Unknown && now == before) {
        r.action = AfterAction::OfferRetryOnce;
        r.message = QStringLiteral("The iPod restarted but the change was not made. It is as "
                                   "before.");
    } else {
        r.action = AfterAction::ShowCheckByHand;
        r.message = QStringLiteral("The iPod restarted. The change could not be checked.");
    }
    return r;
}

RetryAdvice ClassicRetryPolicy::record(ClassicFailureKind kind)
{
    RetryAdvice a;
    switch (kind) {
    case ClassicFailureKind::BeforePayload:
        a.mayRetry = true;
        a.message = QStringLiteral("The iPod is not changed. You can try again.");
        break;
    case ClassicFailureKind::OneTone:
        if (++m_oneTone < kMaxOneToneErrors) {
            a.mayRetry = true;
            a.message = QStringLiteral("The change did not happen and the iPod is as before. You "
                                       "can try once more.");
        } else {
            a.message =
                QStringLiteral("The change failed again. Common causes: a version of Apple's "
                               "firmware that is not supported, an iPod that was restored in an "
                               "unusual way, or a boot area that holds no valid image.");
        }
        break;
    case ClassicFailureKind::TwoTones:
        a.askToReport = true;
        a.message = QStringLiteral("There is not enough free space in the iPod's flash for the "
                                   "install, so it cannot go on. Nothing was written. Please "
                                   "report it.");
        break;
    case ClassicFailureKind::ThreeTones:
    case ClassicFailureKind::NoRestart:
        m_mayInstall = false;
        break;
    }
    if (!m_mayInstall) {
        a.mayInstall = false;
        a.showRestore = true;
        a.message = QStringLiteral("The iPod may not start. Reset it, enter DFU mode, and restore "
                                   "it with Apple's software.");
    }
    return a;
}

DfuSendCheck checkUninstallerImageReadyToSend(const QByteArray &image, qint64 payloadLength)
{
    DfuSendCheck r;
    auto refuseSend = [&r](const char *text) {
        r.message = QString::fromLatin1(text);
        return r;
    };
    if (payloadLength <= 0 || payloadLength > kUninstallerMaxPayloadBytes ||
        image.size() != kDfuPrefixBytes + payloadLength)
        return refuseSend("This is not an uninstaller image of the right size.");
    if (image.left(4) != QByteArray(kDfuChipName.data(), 4))
        return refuseSend("This is not an uninstaller image.");
    r.ok = true;
    return r;
}

UsbAccessMessage describeAccessError(UsbAccessError code, HostSystem system,
                                     const QStringList &others)
{
    UsbAccessMessage m;
    const QString busyHint =
        others.isEmpty()
            ? QStringLiteral("Close the other programs that use the iPod.")
            : QStringLiteral("Close or pause: %1.").arg(others.join(QLatin1String(", ")));
    switch (code) {
    case UsbAccessError::None:
        break;
    case UsbAccessError::AccessDenied:
        m.message = QStringLiteral("No permission to use the iPod over USB.");
        if (system == HostSystem::Linux)
            m.hint = QStringLiteral("Install the udev rule for 05ac:1223 (see the help), then "
                                    "reset the iPod and enter DFU mode again.");
        else if (system == HostSystem::Windows)
            m.hint = QStringLiteral("Bind a driver to the iPod (Apple's or WinUSB, see the help), "
                                    "then enter DFU mode again.");
        else
            m.hint = QStringLiteral("Use a build of TacetOS that is signed for USB access.");
        break;
    case UsbAccessError::Busy:
    case UsbAccessError::ExclusiveAccess:
        m.message = QStringLiteral("Another program is using the iPod.");
        m.hint = busyHint;
        break;
    case UsbAccessError::NoDevice:
        m.message = QStringLiteral("The iPod is gone. It was unplugged or it reset.");
        m.hint = QStringLiteral("Check the cable, reset the iPod and enter DFU mode again.");
        break;
    case UsbAccessError::NotSupported:
        m.message = QStringLiteral("The iPod has no USB driver that TacetOS can use.");
        m.hint = QStringLiteral("Install Apple's driver, or bind WinUSB to 05AC 1223 with a "
                                "driver tool (see the help).");
        break;
    case UsbAccessError::NotFoundAtConfig:
        m.message = QStringLiteral("The driver bound to the iPod is not one TacetOS can use.");
        m.hint = QStringLiteral("Replace it with WinUSB (see the help).");
        break;
    case UsbAccessError::Other:
        m.message = QStringLiteral("The iPod could not be opened.");
        m.hint = QStringLiteral("Reset the iPod and enter DFU mode again.");
        break;
    }
    return m;
}

namespace {

ClassicDfuOutcome stopped(ClassicStage stage, const QString &message, const QString &hint = {})
{
    ClassicDfuOutcome o;
    o.stage = stage;
    o.message = message;
    o.hint = hint;
    return o;
}

ClassicDfuOutcome cancelledAt(ClassicStage stage)
{
    ClassicDfuOutcome o = stopped(stage, QStringLiteral("Cancelled."));
    o.cancelled = true;
    return o;
}

} // namespace

ClassicDfuOutcome runClassicDfu(ClassicSystemLayer &layer, const ClassicDfuJob &job,
                                const AroundOptions &options)
{
    // Before anything is touched: the image must be the one for this operation.
    const DfuSendCheck check = job.operation == ClassicOperation::Install
                                   ? checkInstallerImageReadyToSend(job.image, job.payloadLength)
                                   : checkUninstallerImageReadyToSend(job.image, job.payloadLength);
    if (!check.ok)
        return stopped(ClassicStage::Check, check.message);

    const EjectResult ejected = ejectIpod(layer, options);
    if (ejected.cancelled)
        return cancelledAt(ClassicStage::Eject);
    if (!ejected.ok)
        return stopped(ClassicStage::Eject, ejected.message);

    const int spin = spinDownWaitMs(job.hasHardDisk);
    if (spin > 0)
        pause(options, spin);
    if (isCancelled(options))
        return cancelledAt(ClassicStage::Pause);

    // From here on, whatever happens, the destructor resumes what was paused.
    PausedProcesses paused(layer, job.stateFile);
    const PauseResult pausedNow = paused.pauseAll();
    if (!pausedNow.ok)
        return stopped(ClassicStage::Pause, pausedNow.message);

    const DfuWaitResult waited = waitForQuietDfu(layer, options);
    if (waited.cancelled)
        return cancelledAt(ClassicStage::WaitDevice);
    if (!waited.ok)
        return stopped(ClassicStage::WaitDevice, waited.message);

    std::unique_ptr<UsbDfuLink> link = layer.makeLink(waited.device);
    ClassicDfuOutcome out;
    out.stage = ClassicStage::Transfer;
    out.transfer = sendDfuImage(*link, job.image, job.send);
    // The payload runs on its own now, and Apple's programs may have their iPod back.
    paused.resumeAll();
    if (!out.transfer.ok) {
        out.message = out.transfer.message;
        if (out.transfer.access != UsbAccessError::None) {
            const UsbAccessMessage m = describeAccessError(out.transfer.access, layer.hostSystem(),
                                                           namesOf(layer.findAppleProcesses()));
            out.message = m.message;
            out.hint = m.hint;
        }
        return out;
    }

    out.stage = ClassicStage::Watch;
    out.returned = watchAfterTransfer(layer, options);
    const BootloaderState now = out.returned == ReturnedMode::Normal && job.readState
                                    ? job.readState()
                                    : BootloaderState::Unknown;
    out.after = classifyAfterTransfer(job.operation, job.stateBefore, out.returned, now);
    out.stage = ClassicStage::Done;
    out.ok = out.after.success;
    out.message = out.after.message;
    return out;
}

} // namespace tacetos::installer
