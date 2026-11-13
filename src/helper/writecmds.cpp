// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "writecmds.h"

#include "checks.h"
#include "convert.h"
#include "fddevice.h"
#include "firmware.h"
#include "format.h"
#include "fullpoint.h"
#include "gate.h"
#include "helperconstants.h"
#include "killpoint.h"
#include "operation.h"
#include "recognise.h"
#include "sectordevice.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

namespace tacetos::helper {

using namespace tacetos::installer;

namespace {

const QString kCancelled = QStringLiteral("cancelled");

CommandOutcome failure(const QString &code, const QString &detail, const QJsonObject &data = {})
{
    CommandOutcome o;
    o.error = code;
    o.detail = detail;
    o.data = data;
    return o;
}

CommandOutcome success(const QJsonObject &data)
{
    CommandOutcome o;
    o.ok = true;
    o.data = data;
    return o;
}

// Puts the stopping points of section 10 around any disk: every read is a place
// where a cancel is looked at, the first write is the point of no return, and the
// bytes passing are counted for the `progress` events.
class WatchedDevice : public SectorDevice
{
public:
    WatchedDevice(SectorDevice &inner, CommandHooks &hooks) : m_inner(inner), m_hooks(hooks)
    {
        m_timer.start();
    }

    bool written() const { return m_written; }
    // A cancel stopped a read or the first write.
    bool stoppedByCancel() const { return m_stopped; }

    int sectorSize() const override { return m_inner.sectorSize(); }
    qint64 sectorCount() const override { return m_inner.sectorCount(); }
    bool isRealDisk() const override { return m_inner.isRealDisk(); }

    IoResult readSectors(qint64 first, qint64 count, QByteArray &out) override
    {
        const bool asked = m_hooks.cancelRequested();
        if (asked && !m_written) {
            m_stopped = true;
            return IoResult::failure(kCancelled);
        }
        m_kill.beforeRead();
        const IoResult r = m_inner.readSectors(first, count, out);
        tick(out.size());
        return r;
    }

    IoResult writeSectors(qint64 first, const QByteArray &data) override
    {
        if (!m_written) {
            if (!m_hooks.beforeFirstWrite()) {
                m_stopped = true;
                return IoResult::failure(kCancelled);
            }
            m_written = true;
            m_done = 0;
        }
        m_kill.beforeWrite();
        if (const qint64 torn = m_kill.tornSectors(data.size() / sectorSize())) {
            m_inner.writeSectors(first, data.left(int(torn * sectorSize())));
            KillPoints::die();
        }
        const IoResult r = m_inner.writeSectors(first, data);
        m_kill.afterWrite();
        tick(data.size());
        return r;
    }

    IoResult flush() override
    {
        const IoResult r = m_inner.flush();
        m_kill.afterFlush();
        return r;
    }

private:
    void tick(qint64 bytes)
    {
        m_done += bytes;
        if (m_timer.elapsed() >= kProgressIntervalMs) {
            m_timer.restart();
            m_hooks.progress(m_done, 0);
        }
    }

    SectorDevice &m_inner;
    CommandHooks &m_hooks;
    KillPoints m_kill;
    QElapsedTimer m_timer;
    qint64 m_done = 0;
    bool m_written = false;
    bool m_stopped = false;
};

QString stateName(State s)
{
    switch (s) {
    case State::Clean:
        return QStringLiteral("clean");
    case State::Installed:
        return QStringLiteral("installed");
    case State::Inconsistent:
        return QStringLiteral("inconsistent");
    case State::NotRead:
        break;
    }
    return QStringLiteral("unread");
}

QString stepName(WriteStep s)
{
    switch (s) {
    case WriteStep::None:
        return QStringLiteral("none");
    case WriteStep::RemoveBootloader:
        return QStringLiteral("remove-bootloader");
    case WriteStep::MoveImages:
        return QStringLiteral("move-images");
    case WriteStep::MoveDirectory:
        return QStringLiteral("move-directory");
    case WriteStep::WriteProgram:
        return QStringLiteral("write-program");
    case WriteStep::WriteDirectory:
        return QStringLiteral("write-directory");
    case WriteStep::Verify:
        return QStringLiteral("verify");
    }
    return QStringLiteral("none");
}

QString nextName(NextAction a)
{
    switch (a) {
    case NextAction::None:
        return QStringLiteral("none");
    case NextAction::OfferRetry:
        return QStringLiteral("offer-retry");
    case NextAction::ReportState:
        return QStringLiteral("report-state");
    case NextAction::OfferRestore:
        return QStringLiteral("offer-restore");
    case NextAction::Restored:
        return QStringLiteral("restored");
    case NextAction::RestoreFailed:
        return QStringLiteral("restore-failed");
    }
    return QStringLiteral("none");
}

// What the disk is now, as the scan of section 5.3 saw it.
struct Scan
{
    Recognition rec;
    ScanSnapshot snap;
};

// Section 7.1 step 7: the recognition, and the token of the request.
// A real disk adds the USB bus and device numbers to its token (section 5.3).
CommandOutcome scanAndCompare(SectorDevice &dev, const QString &token, Scan &scan, int usbBus = -1,
                              int usbDevice = -1)
{
    scan.rec = recognise(dev, true);
    if (scan.rec.verdict == Verdict::ReadError)
        return failure(err::openFailed, scan.rec.message);
    if (scan.rec.verdict == Verdict::UnknownGeneration)
        return failure(err::unsupportedModel, scan.rec.message);
    if (scan.rec.verdict != Verdict::Recognised)
        return failure(err::notAnIpod, scan.rec.message);
    scan.snap = takeScanSnapshot(dev, scan.rec);
    QString why;
    const QString now = scanTokenFor(dev, scan.rec, scan.snap, &why, usbBus, usbDevice);
    if (now.isEmpty())
        return failure(err::openFailed, why);
    if (now != token)
        return failure(err::diskChanged, QStringLiteral("the disk is not the one that was read"));
    return {};
}

bool isError(const CommandOutcome &o)
{
    return !o.error.isEmpty();
}

// A command that opens the image twice: read only for the scan, then read and
// write for the work (section 6.5).
struct Opened
{
    FileSectorDevice reader;
    FileSectorDevice writer;
    Opened(const ResolvedTarget &t)
        : reader(t.path, t.sectorSize, FileSectorDevice::Mode::ReadOnly),
          writer(t.path, t.sectorSize, FileSectorDevice::Mode::ReadWrite)
    {
    }
};

CommandOutcome mapOperation(const OperationResult &res, const WatchedDevice &dev,
                            CommandHooks &hooks)
{
    QJsonObject data;
    data.insert(QStringLiteral("backup"), res.report.backupPath);
    if (res.ok) {
        data.insert(QStringLiteral("record"), res.recordPath);
        data.insert(QStringLiteral("recordMessage"), res.recordMessage);
        QJsonArray warnings;
        for (const QString &w : res.report.warnings)
            warnings.append(w);
        data.insert(QStringLiteral("warnings"), warnings);
        return success(data);
    }
    if (!res.wroteAnything) {
        if (dev.stoppedByCancel())
            return failure(err::cancelled, res.message);
        const int check = res.report.failingCheck;
        data.insert(QStringLiteral("check"), check);
        if (check == 1)
            return failure(err::diskChanged, res.message, data);
        if (check == 6)
            return failure(err::backupFailed, res.message, data);
        if (check == 8)
            return failure(err::devBuild, res.message, data);
        return failure(err::checkFailed, res.message, data);
    }
    // Section 19: what the app needs to act on the failure.
    data.insert(QStringLiteral("step"), stepName(res.failedStep));
    data.insert(QStringLiteral("state"), stateName(res.stateAfter));
    data.insert(QStringLiteral("next"), nextName(res.next));
    data.insert(QStringLiteral("backupWrittenBack"), res.restoredBackup);
    hooks.log(QStringLiteral("error"), res.message);
    return failure(res.failedStep == WriteStep::Verify ? err::verifyFailed : err::writeFailed,
                   res.message, data);
}

QByteArray readBootloaderFile(const QString &path, QString *why)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *why = QStringLiteral("the bootloader file cannot be read");
        return {};
    }
    if (f.size() > kBootFileMax) {
        *why = QStringLiteral("the bootloader file is longer than %1 bytes").arg(kBootFileMax);
        return {};
    }
    return f.readAll();
}

// Section 6.4 on a disk that is already open for reading.
CommandOutcome backupOnDevice(SectorDevice &image, const QJsonObject &args, CommandHooks &hooks,
                              int usbBus = -1, int usbDevice = -1)
{
    WatchedDevice dev(image, hooks);
    hooks.stage(QStringLiteral("check"));
    Scan scan;
    CommandOutcome bad = scanAndCompare(dev, args.value(QStringLiteral("scanToken")).toString(),
                                        scan, usbBus, usbDevice);
    if (isError(bad))
        return dev.stoppedByCancel() ? failure(err::cancelled, bad.detail) : bad;
    hooks.stage(QStringLiteral("backup"));
    const QString path =
        freeFilePath(args.value(QStringLiteral("dataDir")).toString(),
                     backupFileName(scan.snap.target, QDateTime::currentDateTime()));
    QString why;
    if (!copyDiskRange(dev, scan.snap.firmwareStart, scan.snap.firmwareBytes, path, why,
                       testFault("backup"))) {
        if (dev.stoppedByCancel())
            return failure(err::cancelled, why);
        return failure(err::backupFailed, why);
    }
    QJsonObject data;
    data.insert(QStringLiteral("path"), path);
    return success(data);
}

CommandOutcome runBackup(const QJsonObject &args, const ResolvedTarget &t, CommandHooks &hooks)
{
    hooks.stage(QStringLiteral("open"));
    FileSectorDevice image(t.path, t.sectorSize, FileSectorDevice::Mode::ReadOnly);
    if (const IoResult r = image.open(); !r)
        return failure(err::openFailed, r.error);
    return backupOnDevice(image, args, hooks);
}

CommandOutcome runBootloader(const QString &cmdName, const QJsonObject &args,
                             const ResolvedTarget &t, CommandHooks &hooks)
{
    const bool install = cmdName == cmd::installBootloader;
    const bool uninstall = cmdName == cmd::uninstallBootloader;
    const QString expected = args.value(QStringLiteral("expectedState")).toString();
    // Section 6.5: `clean` for an install, `installed` for the two others.
    if (expected != (install ? QLatin1String("clean") : QLatin1String("installed")))
        return failure(
            err::badRequest,
            QStringLiteral("expectedState \"%1\" does not fit %2").arg(expected, cmdName));

    QByteArray file;
    if (!uninstall) {
        QString why;
        file = readBootloaderFile(args.value(QStringLiteral("bootloaderFile")).toString(), &why);
        if (file.isEmpty())
            return failure(err::checkFailed,
                           why.isEmpty() ? QStringLiteral("the bootloader file is empty") : why);
    }

    hooks.stage(QStringLiteral("open"));
    Opened opened(t);
    if (const IoResult r = opened.reader.open(); !r)
        return failure(err::openFailed, r.error);
    hooks.stage(QStringLiteral("check"));
    Scan scan;
    CommandOutcome bad =
        scanAndCompare(opened.reader, args.value(QStringLiteral("scanToken")).toString(), scan);
    if (isError(bad))
        return bad;
    const FirmwareState fw = readFirmware(opened.reader, scan.rec);
    if (stateName(fw.state) != expected)
        return failure(
            err::stateChanged,
            QStringLiteral("the iPod is %1, not %2 as shown").arg(stateName(fw.state), expected));

    if (const IoResult r = opened.writer.open(); !r)
        return failure(err::openFailed, r.error);
    WatchedDevice dev(opened.writer, hooks);

    OperationRequest req;
    req.op = install ? Operation::Install : (uninstall ? Operation::Uninstall : Operation::Update);
    req.scan = scan.snap;
    req.bootloaderFile = file;
    req.backupDir = args.value(QStringLiteral("dataDir")).toString();
    req.recordDir = req.backupDir;
    req.backupFault = testFault("backup");
    req.recordFault = testFault("record");
    req.bootloaderVersion = args.value(QStringLiteral("bootloaderVersion")).toString();
    const OperationResult res = runOperation(dev, req);
    return mapOperation(res, dev, hooks);
}

CommandOutcome runRestore(const QJsonObject &args, const ResolvedTarget &t, CommandHooks &hooks)
{
    // Section 6.6: the backup must be in the data folder.
    const QString dataDir = QDir(args.value(QStringLiteral("dataDir")).toString()).canonicalPath();
    const QString backup =
        QFileInfo(args.value(QStringLiteral("backupFile")).toString()).canonicalFilePath();
    if (backup.isEmpty() || !backup.startsWith(dataDir + QLatin1Char('/')))
        return failure(err::badRequest, QStringLiteral("the backup is not in the data folder"));

    hooks.stage(QStringLiteral("open"));
    Opened opened(t);
    if (const IoResult r = opened.reader.open(); !r)
        return failure(err::openFailed, r.error);
    hooks.stage(QStringLiteral("check"));
    Scan scan;
    CommandOutcome bad =
        scanAndCompare(opened.reader, args.value(QStringLiteral("scanToken")).toString(), scan);
    if (isError(bad))
        return bad;
    if (!writeAllowed(opened.reader))
        return failure(err::devBuild, QStringLiteral("this build does not write to real iPods"));
    if (const IoResult r = opened.writer.open(); !r)
        return failure(err::openFailed, r.error);
    WatchedDevice dev(opened.writer, hooks);
    const RestoreResult res = restoreBackup(dev, scan.snap, backup);
    if (res.ok)
        return success({});
    if (dev.stoppedByCancel())
        return failure(err::cancelled, res.message);
    if (res.mismatch)
        return failure(err::backupMismatch, res.message);
    if (!dev.written())
        return failure(err::checkFailed, res.message);
    return failure(err::writeFailed, res.message);
}

CommandOutcome runFormatOrConvert(const QString &cmdName, const QJsonObject &args,
                                  const ResolvedTarget &t, CommandHooks &hooks)
{
    const bool convert = cmdName == cmd::convertToFat32;
    // An image has no file system mounted on it: there is nothing to unmount.
    hooks.stage(QStringLiteral("unmount"));
    hooks.stage(QStringLiteral("open"));
    Opened opened(t);
    if (const IoResult r = opened.reader.open(); !r)
        return failure(err::openFailed, r.error);
    hooks.stage(QStringLiteral("check"));
    Scan scan;
    CommandOutcome bad =
        scanAndCompare(opened.reader, args.value(QStringLiteral("scanToken")).toString(), scan);
    if (isError(bad))
        return bad;
    if (!writeAllowed(opened.reader))
        return failure(err::devBuild, QStringLiteral("this build does not write to real iPods"));
    if (const IoResult r = opened.writer.open(); !r)
        return failure(err::openFailed, r.error);
    WatchedDevice dev(opened.writer, hooks);

    FormatRequest fr;
    fr.label = args.value(QStringLiteral("volumeLabel")).toString();
    fr.ipodName = args.value(QStringLiteral("ipodName")).toString();
    fr.typedName = args.value(QStringLiteral("typedName")).toString();
    fr.slotUnmounted = true;

    if (!convert) {
        const FormatResult res = formatMusicPartition(dev, fr);
        if (res.ok) {
            QJsonObject data;
            data.insert(QStringLiteral("sizeBytes"),
                        double(qint64(res.geometry.total) * dev.sectorSize()));
            return success(data);
        }
        if (dev.stoppedByCancel())
            return failure(err::cancelled, res.message);
        return failure(res.wroteAnything ? err::writeFailed : err::checkFailed, res.message);
    }

    ConvertRequest cr;
    static_cast<FormatRequest &>(cr) = fr;
    cr.scan = scan.snap;
    cr.backupDir = args.value(QStringLiteral("dataDir")).toString();
    cr.backupFault = testFault("backup");
    const ConvertResult res = convertMacToFat32(dev, cr);
    QJsonObject info;
    info.insert(QStringLiteral("backup"), res.backupPath);
    info.insert(QStringLiteral("mapBackup"), res.mapBackupPath);
    if (!res.ok) {
        if (dev.stoppedByCancel())
            return failure(err::cancelled, res.message);
        if (!res.wroteAnything)
            return failure(err::checkFailed, res.message, info);
        info.insert(QStringLiteral("formatted"), res.formatted);
        info.insert(QStringLiteral("tableWritten"), res.tableWritten);
        return failure(err::writeFailed, res.message, info);
    }
    // Section 6.8: the result of the recognition again, with a new token.
    hooks.stage(QStringLiteral("verify"));
    CommandOutcome again = readState(
        opened.writer, [&](const QString &level, const QString &text) { hooks.log(level, text); },
        [](const QString &) {});
    if (!again.ok)
        return again;
    again.data.insert(QStringLiteral("backup"), res.backupPath);
    again.data.insert(QStringLiteral("mapBackup"), res.mapBackupPath);
    return again;
}

} // namespace

bool isLegacyCommand(const QString &name)
{
    return name == cmd::backupFirmware || name == cmd::installBootloader ||
           name == cmd::updateBootloader || name == cmd::uninstallBootloader ||
           name == cmd::restoreBackup || name == cmd::formatMusic || name == cmd::convertToFat32;
}

QString checkConfirmation(const QString &cmdName, const QJsonObject &args)
{
    if (cmdName != cmd::formatMusic && cmdName != cmd::convertToFat32)
        return {};
    if (confirmationMatches(args.value(QStringLiteral("ipodName")).toString(),
                            args.value(QStringLiteral("typedName")).toString()))
        return {};
    return QStringLiteral("the typed name is not the name of the iPod");
}

QString checkDataDir(const QString &dir)
{
    if (!QDir::isAbsolutePath(dir))
        return QStringLiteral("the data folder is not an absolute path");
    const QFileInfo info(dir);
    if (info.exists() && !info.isDir())
        return QStringLiteral("the data folder is a file");
    if (!QDir().mkpath(dir))
        return QStringLiteral("the data folder cannot be made");
    return {};
}

CommandOutcome runLegacyCommandOnDisk(const QString &cmdName, const QJsonObject &args,
                                      SystemLayer &system, const RealDisk &disk,
                                      CommandHooks &hooks)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(cmdName)
    Q_UNUSED(args)
    Q_UNUSED(system)
    Q_UNUSED(disk)
    Q_UNUSED(hooks)
    return failure(err::unsupportedSystem, QStringLiteral("no disk service on this system yet"));
#else
    // Step 6: for reading only. The disk is opened for writing nowhere in this
    // function (section 11.1: only after every check).
    hooks.stage(QStringLiteral("open"));
    const OpenResult opened = system.disks->open(disk.blockPath, false);
    if (opened.fd < 0)
        return failure(opened.code, opened.detail);
    FdSectorDevice dev(opened.fd, disk.sizeBytes, false);
    if (cmdName == cmd::backupFirmware)
        return backupOnDevice(dev, args, hooks, disk.usbBus, disk.usbDevice);

    // Step 7: the recognition and the token, which is check 1 of the spec of the
    // installer: the disk read now is the disk that was shown.
    hooks.stage(QStringLiteral("check"));
    Scan scan;
    const CommandOutcome bad = scanAndCompare(
        dev, args.value(QStringLiteral("scanToken")).toString(), scan, disk.usbBus, disk.usbDevice);
    if (isError(bad))
        return bad;
    if (cmdName == cmd::installBootloader || cmdName == cmd::updateBootloader ||
        cmdName == cmd::uninstallBootloader) {
        const QString expected = args.value(QStringLiteral("expectedState")).toString();
        // Section 6.5: `clean` for an install, `installed` for the two others.
        if (expected != (cmdName == cmd::installBootloader ? QLatin1String("clean")
                                                           : QLatin1String("installed")))
            return failure(
                err::badRequest,
                QStringLiteral("expectedState \"%1\" does not fit %2").arg(expected, cmdName));
        const FirmwareState fw = readFirmware(dev, scan.rec);
        if (stateName(fw.state) != expected)
            return failure(err::stateChanged, QStringLiteral("the iPod is %1, not %2 as shown")
                                                  .arg(stateName(fw.state), expected));
    }
    // Step 9: a development build never writes to a real disk.
    if (!writeAllowed(dev))
        return failure(err::devBuild, QStringLiteral("this build does not write to real iPods"));
    // The writes of a real disk are not made yet: nothing is opened for writing.
    return failure(err::unsupportedSystem,
                   QStringLiteral("writing to a real disk is not made in this build yet"));
#endif
}

CommandOutcome runLegacyCommand(const QString &cmdName, const QJsonObject &args,
                                const ResolvedTarget &t, CommandHooks &hooks)
{
    if (cmdName == cmd::backupFirmware)
        return runBackup(args, t, hooks);
    if (cmdName == cmd::installBootloader || cmdName == cmd::updateBootloader ||
        cmdName == cmd::uninstallBootloader)
        return runBootloader(cmdName, args, t, hooks);
    if (cmdName == cmd::restoreBackup)
        return runRestore(args, t, hooks);
    return runFormatOrConvert(cmdName, args, t, hooks);
}

} // namespace tacetos::helper
