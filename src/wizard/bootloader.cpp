// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "bootloader.h"

#include "backup.h"
#include "helperconstants.h"
#include "package.h"
#include "wizarderrors.h"

namespace tacetos::wizard {

namespace {

const QString kClean = QStringLiteral("clean");
const QString kInstalled = QStringLiteral("installed");
const QString kInconsistent = QStringLiteral("inconsistent");

QString stateOf(const HelperAnswer &answer)
{
    return answer.data.value(QStringLiteral("state")).toString();
}

const QString kNext = QStringLiteral("next");
const QString kOfferRestore = QStringLiteral("offer-restore");

} // namespace

StepOutcome DiskModeStep::run(StepContext &context)
{
    QJsonObject info;
    info.insert(QStringLiteral("model"), context.model());
    const UserAnswer answer = context.waitForUser(WaitKind::DiskMode, info);
    if (answer.cancelled)
        return StepOutcome::cancelled();
    // The iPod may still be restarting: it is ready when detection says so.
    const auto volume = m_sources.volumes().waitReady(
        context.serial(), m_sources.returnMs(), [&context] { return context.stopRequested(); });
    if (context.stopRequested())
        return StepOutcome::cancelled();
    if (!volume)
        return StepOutcome::failed(QString(error::ipodNotBack));
    return StepOutcome::done();
}

// Section 8.2, last paragraph: the offer to write the backup back, and only
// after the user's yes the command (bootloader-legacy.md section 19). The
// iPod is then as it was before the bootloader: the step stops there, and the
// user presses Resume to try again.
StepOutcome BootloaderStep::offerRestore(StepContext &context, const QString &folder,
                                         const QString &failure)
{
    const QString file = checkedFirmwarePartition(folder, context.serial());
    if (file.isEmpty())
        return StepOutcome::failed(QString(error::backupNotVerified));
    QJsonObject info;
    info.insert(QStringLiteral("offer"), QStringLiteral("restore-backup"));
    info.insert(QStringLiteral("backup"), file);
    const UserAnswer answer = context.waitForUser(WaitKind::Offer, info);
    if (answer.cancelled)
        return StepOutcome::cancelled();
    if (!answer.yes)
        return StepOutcome::failed(QString(helper::err::checkFailed),
                                   failure.isEmpty()
                                       ? QStringLiteral("the state of the iPod is ") + kInconsistent
                                       : failure);
    QJsonObject args;
    args.insert(QStringLiteral("backupFile"), file);
    args.insert(QStringLiteral("dataDir"), folder);
    HelperWatch watch;
    watch.progress = [&context](const QJsonObject &event) {
        if (!event.value(QStringLiteral("cancellable")).toBool(true))
            context.firstWrite();
        context.progress(event.value(QStringLiteral("stage")).toString(),
                         qint64(event.value(QStringLiteral("done")).toDouble()),
                         qint64(event.value(QStringLiteral("total")).toDouble()));
    };
    const HelperAnswer result =
        m_sources.helper().callWithToken(QString(helper::cmd::restoreBackup), args, watch);
    if (!result.ok()) {
        context.setFact(kNext, QStringLiteral("restore-failed"));
        return StepOutcome::failed(result.code, result.detail);
    }
    context.setFact(kNext, QStringLiteral("restored"));
    return StepOutcome::failed(QString(error::bootloaderRestored));
}

// Section 8.2, last paragraph: a failure after the first write, from the
// `next` of the helper (bootloader-legacy.md section 19). The screen reads it
// in the facts of the step.
StepOutcome BootloaderStep::failedWrite(StepContext &context, const QString &folder,
                                        const HelperAnswer &result)
{
    const QString next = result.data.value(kNext).toString();
    if (next == kOfferRestore)
        return offerRestore(context, folder, result.detail);
    // `offer-retry`, `report-state`, `restored`, `restore-failed`: the
    // helper did what it had to, the step says it and stops.
    if (!next.isEmpty() && next != QLatin1String("none"))
        context.setFact(kNext, next);
    return StepOutcome::failed(result.code, result.detail);
}

StepOutcome BootloaderStep::run(StepContext &context)
{
    // Step 1: the package again, and the copy of the firmware partition of
    // the flow, its sha256 read again (section 5.6).
    PackageFiles files;
    const PackageCheck package =
        sameAsFlow(checkPackage(context.packageManifest(), m_sources.target(), &files),
                   context.packageSha256());
    if (!package.code.isEmpty())
        return StepOutcome::failed(package.code, package.message);
    const QString folder = context.backupFolder();
    QString why;
    if (checkedFirmwarePartition(folder, context.serial(), &why).isEmpty())
        return StepOutcome::failed(QString(error::backupNotVerified), why);

    // Step 2.
    HelperCommands &helper = m_sources.helper();
    const HelperAnswer state = helper.call(QString(helper::cmd::readState), QJsonObject());
    if (!state.ok())
        return StepOutcome::failed(state.code, state.detail);
    const QString before = stateOf(state);
    if (context.isResume()) {
        // Section 3.3: after a cut, the state says how far the step went.
        if (before == kInstalled) {
            context.setFact(QStringLiteral("recordMessage"),
                            QStringLiteral("the record was not seen before the cut"));
            return StepOutcome::done();
        }
        if (before == kInconsistent)
            return offerRestore(context, folder, QString());
    }
    if (before != kClean && before != kInstalled)
        return StepOutcome::failed(QString(helper::err::checkFailed),
                                   QStringLiteral("the state of the iPod is ") + before);

    // Step 3.
    QJsonObject args;
    args.insert(QStringLiteral("scanToken"), state.data.value(QStringLiteral("scanToken")));
    args.insert(QStringLiteral("expectedState"), before);
    args.insert(QStringLiteral("bootloaderFile"), files.bootloader);
    args.insert(QStringLiteral("bootloaderVersion"), files.version);
    args.insert(QStringLiteral("dataDir"), folder);
    HelperWatch watch;
    watch.progress = [&context](const QJsonObject &event) {
        if (!event.value(QStringLiteral("cancellable")).toBool(true))
            context.firstWrite();
        context.progress(event.value(QStringLiteral("stage")).toString(),
                         qint64(event.value(QStringLiteral("done")).toDouble()),
                         qint64(event.value(QStringLiteral("total")).toDouble()));
    };
    watch.stop = [&context] { return context.stopRequested(); };
    const QString cmd =
        QString(before == kClean ? helper::cmd::installBootloader : helper::cmd::updateBootloader);
    const HelperAnswer result = helper.call(cmd, args, watch);
    if (result.code == helper::err::cancelled)
        return StepOutcome::cancelled();
    if (!result.ok())
        return failedWrite(context, folder, result);

    // Step 5: where the install record is, or why there is none.
    const QString record = result.data.value(QStringLiteral("record")).toString();
    if (!record.isEmpty())
        context.setFact(QStringLiteral("record"), record);
    else
        context.setFact(QStringLiteral("recordMessage"),
                        result.data.value(QStringLiteral("recordMessage")));

    // Step 4.
    const HelperAnswer after = helper.call(QString(helper::cmd::readState), QJsonObject());
    if (!after.ok())
        return StepOutcome::failed(after.code, after.detail);
    if (stateOf(after) != kInstalled)
        return StepOutcome::failed(QString(error::verifyFailed),
                                   QStringLiteral("the state of the iPod is ") + stateOf(after));
    return StepOutcome::done();
}

} // namespace tacetos::wizard
