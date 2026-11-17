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

QString stateOf(const HelperAnswer &answer)
{
    return answer.data.value(QStringLiteral("state")).toString();
}

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
        return StepOutcome::failed(result.code, result.detail);

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
