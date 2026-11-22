// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "classicdfu.h"

#include "backup.h"
#include "constants.h"
#include "helperconstants.h"
#include "markers.h"
#include "package.h"
#include "wizarderrors.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace tacetos::wizard {

namespace {

const QString kStageSend = QStringLiteral("send");
const QString kStageResume = QStringLiteral("resume");
const QString kStageDfuDetected = QStringLiteral("dfu-detected");
const QString kStageDiskModeInstead = QStringLiteral("disk-mode-instead");
const QString kStageWaitDisk = QStringLiteral("wait-disk");
const QString kNotTaken = QStringLiteral("notTaken");
const QString kNotRemoved = QStringLiteral("notRemoved");

// Section 9.1: the conditions of `bootloader-classic.md` section 3 that the
// diagnosis checks. Condition 4, the firmware, is `firmwareCopied`.
const char *const kConditions[] = {"format",      "apple-firmware", "battery",
                                   "single-ipod", "classic-build",  "udev-rule"};

QJsonObject readRecord(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Section 7.4 step 6, in short: the folder of the firmware is on the volume and
// its info file says it is for this model.
bool firmwareCopied(const QString &root, const QString &target)
{
    const device::MarkerRead info =
        device::readMarkerFile(root, QString(device::kFirmwareFolder) + QLatin1Char('/') +
                                         QString(device::kFirmwareInfoFile));
    const device::InfoFile parsed =
        device::parseInfoFile(info.text, QString(device::kDistributionKey));
    return parsed.target.compare(target, Qt::CaseInsensitive) == 0;
}

} // namespace

QString classicStateBefore(const QStringList &files, const QString &serial)
{
    QDateTime latest;
    QString operation;
    for (const QString &path : files) {
        const QJsonObject record = readRecord(path);
        if (record.value(QStringLiteral("serial")).toString() != serial)
            continue;
        const QDateTime date =
            QDateTime::fromString(record.value(QStringLiteral("date")).toString(), Qt::ISODate);
        if (!date.isValid())
            continue;
        if (!latest.isValid() || date >= latest) {
            latest = date;
            operation = record.value(QStringLiteral("operation")).toString();
        }
    }
    if (operation == QLatin1String("install"))
        return QStringLiteral("present");
    if (operation == QLatin1String("uninstall"))
        return QStringLiteral("absent");
    return QStringLiteral("unknown");
}

QString appleSerialOf(const QStringList &files, const QString &seen)
{
    if (seen.isEmpty())
        return {};
    QDateTime latest;
    QString serial;
    for (const QString &path : files) {
        const QJsonObject record = readRecord(path);
        if (record.value(QStringLiteral("serialUnderTacetos")).toString() != seen)
            continue;
        const QDateTime date =
            QDateTime::fromString(record.value(QStringLiteral("date")).toString(), Qt::ISODate);
        if (!date.isValid())
            continue;
        if (!latest.isValid() || date >= latest) {
            latest = date;
            serial = record.value(QStringLiteral("serial")).toString();
        }
    }
    return serial;
}

// Section 9.1: any failure stops the step before the iPod is ejected.
std::optional<StepOutcome> ClassicDfuStep::conditions(StepContext &context) const
{
    const DiagnoseSources &sources = m_sources.diagnose();
    const FlowShape shape = context.shape();
    DiagnoseInput in;
    in.os = sources.os();
    in.osRelease = sources.osRelease();
    in.udisks = sources.udisks();
    in.helper = sources.helper();
    in.udevRulePresent = sources.udevRulePresent();
    in.backup = sources.backupFolder();
    in.family = shape.family;
    in.kind = shape.kind;
    in.path = shape.path;
    in.ipod = sources.ipod();
    // The question was answered by the step `diagnose` of this flow.
    in.batteryCharged = true;
    const QList<Check> checks = diagnose(in);
    for (const char *id : kConditions) {
        for (const Check &check : checks) {
            if (check.id != QLatin1String(id) || check.result == CheckResult::Pass ||
                check.result == CheckResult::Warn)
                continue;
            return StepOutcome::failed(QString(error::checkBlocking), check.id);
        }
    }
    if (m_operation == ClassicOperation::Install &&
        !firmwareCopied(m_sources.volumeRoot(), m_sources.target()))
        return StepOutcome::failed(QString(error::firmwareWriteFailed),
                                   QStringLiteral("the firmware is not on the music volume"));
    return std::nullopt;
}

// Section 9.3.
bool ClassicDfuStep::writeRecord(StepContext &context, const PackageFiles &files) const
{
    QJsonObject record;
    record.insert(QStringLiteral("format"), kInstallRecordFormat);
    record.insert(QStringLiteral("operation"), QStringLiteral("install"));
    record.insert(QStringLiteral("date"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    record.insert(QStringLiteral("serial"), context.serial());
    if (!m_returnedSerial.isEmpty() && m_returnedSerial != context.serial())
        record.insert(QStringLiteral("serialUnderTacetos"), m_returnedSerial);
    record.insert(QStringLiteral("model"), context.model());
    record.insert(QStringLiteral("version"), files.version);
    record.insert(QStringLiteral("bootloaderName"), QFileInfo(files.bootloader).fileName());
    record.insert(QStringLiteral("bootloaderSha256"), fileSha256(files.bootloader));
    if (context.backupFolder().isEmpty()) {
        context.setFact(QStringLiteral("recordMessage"), QStringLiteral("no backup folder"));
        return false;
    }
    const QString path = QDir(context.backupFolder()).filePath(QString(kInstallRecordFile));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        context.setFact(QStringLiteral("recordMessage"), file.errorString());
        return false;
    }
    const QByteArray bytes = QJsonDocument(record).toJson();
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        context.setFact(QStringLiteral("recordMessage"), file.errorString());
        return false;
    }
    context.setFact(QStringLiteral("record"), path);
    return true;
}

// Section 9.2 steps 4 and 5, and the table of section 9.4 for what comes back.
// `pending` is what detection said during the transfer, from the moment of the
// send.
StepOutcome ClassicDfuStep::comeBack(StepContext &context, QList<IpodSight> &pending)
{
    const QDeadlineTimer deadline(m_sources.returnMs());
    const QString port = m_sources.portPath();
    const auto stop = [&context] { return context.stopRequested(); };
    // Section 16.2 of the Classic spec: a DFU device seen again only counts
    // once the iPod has left the bus (or shown another mode) after the send.
    bool absent = false;
    bool unknown = false;
    for (;;) {
        std::optional<IpodSight> sight;
        if (!pending.isEmpty())
            sight = pending.takeFirst();
        else
            sight = m_sources.sight().next(std::max<qint64>(deadline.remainingTime(), 0), stop);
        if (context.stopRequested())
            return StepOutcome::cancelled();
        if (!sight) {
            if (unknown)
                return StepOutcome::failed(QString(error::verifyFailed),
                                           QStringLiteral("the iPod restarted, but its firmware "
                                                          "could not be told"));
            return StepOutcome::failed(QString(error::ipodNotBack));
        }
        if (!port.isEmpty() && sight->portPath != port)
            continue;
        if (sight->kind == IpodSight::Kind::Gone) {
            absent = true;
            continue;
        }
        // The serial is not compared: it changes with the mode (detection.md
        // section 9.4), the port is the key.
        switch (sight->ipod.mode) {
        case device::UsbMode::Dfu:
            if (absent)
                return StepOutcome::failed(QString(error::backInDfu));
            continue;
        case device::UsbMode::Other:
            return StepOutcome::failed(QString(error::wrongMode));
        case device::UsbMode::Normal:
            absent = true;
            break;
        case device::UsbMode::None:
            continue;
        }
        switch (sight->ipod.running) {
        case device::RunningFirmware::Other:
            if (m_operation == ClassicOperation::Uninstall) {
                // Section 10 step 3: TacetOS still starts.
                const int times =
                    context.fact(QString(step::classicRemove), kNotRemoved).toInt() + 1;
                context.setFact(kNotRemoved, times);
                return StepOutcome::failed(QString(error::stillTacetos), QString::number(times));
            }
            // For `verify`: the iPod may run Apple's firmware later in the flow (Menu held).
            context.setFact(QStringLiteral("returned"), QStringLiteral("tacetos"));
            m_returnedSerial = sight->ipod.serial;
            return StepOutcome::done();
        case device::RunningFirmware::Apple: {
            if (m_operation == ClassicOperation::Uninstall)
                return StepOutcome::done();
            const int times = context.fact(QString(step::classicBootloader), kNotTaken).toInt() + 1;
            context.setFact(kNotTaken, times);
            return StepOutcome::failed(QString(error::bootloaderNotTaken), QString::number(times));
        }
        case device::RunningFirmware::Unknown:
            unknown = true;
            break;
        }
    }
}

StepOutcome ClassicDfuStep::run(StepContext &context)
{
    // Section 9.1: the package again, the conditions, the firmware.
    const bool uninstall = m_operation == ClassicOperation::Uninstall;
    PackageFiles files;
    if (!uninstall) {
        const PackageCheck package =
            sameAsFlow(checkPackage(context.packageManifest(), m_sources.target(), &files),
                       context.packageSha256());
        if (!package.code.isEmpty())
            return StepOutcome::failed(package.code, package.message);
    }
    if (const auto refused = conditions(context))
        return *refused;

    // Section 9.2 step 2.
    const QString before = classicStateBefore(m_sources.installRecords(), context.serial());
    if (uninstall && before != QLatin1String("present") &&
        !QDir(m_sources.volumeRoot()).exists(QString(device::kFirmwareFolder))) {
        // Section 10, "Nothing to do": the state of the NOR cannot be read.
        QJsonObject info;
        info.insert(QStringLiteral("offer"), QStringLiteral("remove-anyway"));
        const UserAnswer answer = context.waitForUser(WaitKind::Offer, info);
        if (answer.cancelled || !answer.yes)
            return StepOutcome::cancelled();
    }
    const QString port = m_sources.portPath();
    IpodSightWatch &sight = m_sources.sight();
    m_returnedSerial.clear();
    QString reason;
    QString hint;
    // The USB detail of the last failed try, for the log of the app.
    QString usb;
    // The stage of the helper where the last try failed (`send`, `wait-device`...).
    QString failedStage;
    // Set while the step looks for the disk after a Start without it: the user
    // already said Start, the step sends again by itself.
    std::optional<QDeadlineTimer> diskWait;
    for (;;) {
        // Step 1: nothing is ejected before the user says "Start".
        if (!diskWait) {
            QJsonObject info;
            info.insert(QStringLiteral("model"), context.model());
            info.insert(QStringLiteral("reason"), reason);
            info.insert(QStringLiteral("hint"), hint);
            if (!usb.isEmpty())
                info.insert(QStringLiteral("usb"), usb);
            if (!failedStage.isEmpty())
                info.insert(QStringLiteral("stage"), failedStage);
            const UserAnswer answer = context.waitForUser(WaitKind::ReadyForDfu, info);
            if (answer.cancelled)
                return StepOutcome::cancelled();
        }

        // Step 3.
        QJsonObject args;
        args.insert(QStringLiteral("operation"),
                    uninstall ? QStringLiteral("uninstall") : QStringLiteral("install"));
        if (!uninstall)
            args.insert(QStringLiteral("bootloaderFile"), files.bootloader);
        args.insert(QStringLiteral("stateBefore"), before);
        args.insert(QStringLiteral("dataDir"), m_sources.dataDir());

        // Detection is read while the helper waits: DFU mode shows up, or the
        // iPod comes back as a disk (section 9.4, `disk-mode-instead`).
        sight.clear();
        QString stage;
        bool left = false;
        bool dfuSeen = false;
        bool diskInstead = false;
        QList<IpodSight> pending;
        HelperWatch watch;
        watch.progress = [&](const QJsonObject &event) {
            stage = event.value(QStringLiteral("stage")).toString();
            if (!event.value(QStringLiteral("cancellable")).toBool(true))
                context.firstWrite();
            context.progress(stage, qint64(event.value(QStringLiteral("done")).toDouble()),
                             qint64(event.value(QStringLiteral("total")).toDouble()));
        };
        watch.stop = [&] {
            if (context.stopRequested())
                return true;
            const bool sent = stage == kStageSend || stage == kStageResume;
            while (const auto seen = sight.next(0, nullptr)) {
                if (!port.isEmpty() && seen->portPath != port)
                    continue;
                if (sent) {
                    pending.append(*seen);
                } else if (seen->kind == IpodSight::Kind::Gone) {
                    left = true;
                } else if (seen->ipod.mode == device::UsbMode::Dfu) {
                    if (!dfuSeen)
                        context.progress(kStageDfuDetected, 0, 0);
                    dfuSeen = true;
                } else if (seen->ipod.mode == device::UsbMode::Normal) {
                    if (!seen->ipod.disk)
                        left = true;
                    else if (left && !dfuSeen)
                        diskInstead = true;
                }
            }
            return diskInstead;
        };
        const HelperAnswer result =
            m_sources.helper().call(QString(helper::cmd::classicDfu), args, watch);

        if (result.code == helper::err::diskGone && stage != kStageSend && stage != kStageResume) {
            // Section 9.4: Start while the iPod restarts, or is still in DFU mode.
            // Nothing was sent: the step waits for the disk, then sends again.
            if (!diskWait)
                diskWait = QDeadlineTimer(m_sources.returnMs());
            if (diskWait->hasExpired()) {
                diskWait.reset();
                reason = QString(error::noDisk);
                hint.clear();
                usb.clear();
                failedStage.clear();
                continue;
            }
            context.progress(kStageWaitDisk, 0, 0);
            const auto stop = [&context] { return context.stopRequested(); };
            const QDeadlineTimer look(std::min(kDiskLookMs, diskWait->remainingTime()));
            while (!look.hasExpired()) {
                const auto seen = sight.next(look.remainingTime(), stop);
                if (context.stopRequested())
                    return StepOutcome::cancelled();
                if (seen && (port.isEmpty() || seen->portPath == port) &&
                    seen->kind != IpodSight::Kind::Gone &&
                    seen->ipod.mode == device::UsbMode::Normal && seen->ipod.disk)
                    break;
            }
            continue;
        }
        diskWait.reset();

        if (diskInstead &&
            (result.code == helper::err::cancelled || result.code == helper::err::dfuFailed)) {
            // Not a failure: the buttons were not the right ones.
            context.progress(kStageDiskModeInstead, 0, 0);
            reason = QString(error::diskModeInstead);
            hint.clear();
            usb.clear();
            failedStage.clear();
            continue;
        }
        if (result.code == helper::err::cancelled)
            return StepOutcome::cancelled();
        if (result.code == helper::err::dfuFailed) {
            // Section 9.4: the step waits for the user again.
            reason = QString(error::dfuFailed);
            hint = result.detail;
            usb = result.data.value(QStringLiteral("usbDetail")).toString();
            failedStage = result.data.value(QStringLiteral("stage")).toString();
            continue;
        }
        if (!result.ok())
            return StepOutcome::failed(result.code, result.detail);

        // Steps 4 and 5.
        const StepOutcome outcome = comeBack(context, pending);
        if (outcome.kind == StepOutcome::Kind::Done && !uninstall)
            writeRecord(context, files);
        return outcome;
    }
}

} // namespace tacetos::wizard
