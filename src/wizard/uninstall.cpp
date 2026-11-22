// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "uninstall.h"

#include "backup.h"
#include "constants.h"
#include "helperconstants.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace tacetos::wizard {

namespace {

const QString kClean = QStringLiteral("clean");
const QString kInstalled = QStringLiteral("installed");

QString stateOf(const HelperAnswer &answer)
{
    return answer.data.value(QStringLiteral("state")).toString();
}

bool firmwareFolderExists(const QString &volumeRoot)
{
    return !volumeRoot.isEmpty() && QDir(volumeRoot).exists(QString(device::kFirmwareFolder));
}

} // namespace

bool legacyNothingToDo(const QString &state, const QString &volumeRoot)
{
    return state == kClean && !firmwareFolderExists(volumeRoot);
}

StepOutcome RemoveBootloaderStep::run(StepContext &context)
{
    HelperCommands &helper = m_sources.helper();
    const HelperAnswer state = helper.call(QString(helper::cmd::readState), QJsonObject());
    if (!state.ok())
        return StepOutcome::failed(state.code, state.detail);
    const QString before = stateOf(state);
    if (before == kClean) {
        if (!context.isResume() && legacyNothingToDo(before, m_sources.volumeRoot()))
            return StepOutcome::failed(QString(error::nothingToDo));
        return StepOutcome::done();
    }
    if (before != kInstalled)
        return StepOutcome::failed(QString(helper::err::checkFailed),
                                   QStringLiteral("the state of the iPod is ") + before);

    // The copy of the firmware partition is what the helper keeps next to the
    // removal, so it must be there and verified (section 5.6).
    const QString folder = context.backupFolder();
    QString why;
    if (checkedFirmwarePartition(folder, context.serial(), &why).isEmpty())
        return StepOutcome::failed(QString(error::backupNotVerified), why);

    QJsonObject args;
    args.insert(QStringLiteral("scanToken"), state.data.value(QStringLiteral("scanToken")));
    args.insert(QStringLiteral("expectedState"), kInstalled);
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
    const HelperAnswer result = helper.call(QString(helper::cmd::uninstallBootloader), args, watch);
    if (result.code == helper::err::cancelled)
        return StepOutcome::cancelled();
    if (!result.ok()) {
        const QString next = result.data.value(QStringLiteral("next")).toString();
        if (!next.isEmpty() && next != QLatin1String("none"))
            context.setFact(QStringLiteral("next"), next);
        return StepOutcome::failed(result.code, result.detail);
    }
    const QString record = result.data.value(QStringLiteral("record")).toString();
    if (!record.isEmpty())
        context.setFact(QStringLiteral("record"), record);
    else
        context.setFact(QStringLiteral("recordMessage"),
                        result.data.value(QStringLiteral("recordMessage")));

    const HelperAnswer after = helper.call(QString(helper::cmd::readState), QJsonObject());
    if (!after.ok())
        return StepOutcome::failed(after.code, after.detail);
    if (stateOf(after) != kClean)
        return StepOutcome::failed(QString(error::verifyFailed),
                                   QStringLiteral("the state of the iPod is ") + stateOf(after));
    return StepOutcome::done();
}

// Section 9.3, for the uninstall: what a later `stateBefore` reads.
bool RemoveFirmwareStep::writeRecord(StepContext &context) const
{
    QJsonObject record;
    record.insert(QStringLiteral("format"), kInstallRecordFormat);
    record.insert(QStringLiteral("operation"), QStringLiteral("uninstall"));
    record.insert(QStringLiteral("date"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    record.insert(QStringLiteral("serial"), context.serial());
    record.insert(QStringLiteral("model"), context.model());
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

StepOutcome RemoveFirmwareStep::run(StepContext &context)
{
    // Section 10 step 1: the kept files are in the backup before the folder
    // goes (D40).
    const QString backup = context.backupFolder();
    QString why;
    if (backup.isEmpty() || !isVerifiedBackup(backup, context.serial(), &why))
        return StepOutcome::failed(QString(error::backupNotVerified), why);
    QString root;
    const bool back = waitUntil(context, m_sources.volumeWaitMs(), [&] {
        root = m_sources.volumeRoot();
        return !root.isEmpty();
    });
    if (context.stopRequested())
        return StepOutcome::cancelled();
    if (!back)
        return StepOutcome::failed(QString(error::volumeNotBack));

    const QString folder = QString(device::kFirmwareFolder);
    for (const QString &name :
         {folder + QString(kNewSuffix), folder + QString(kOldSuffix), folder}) {
        if (context.stopRequested())
            return StepOutcome::cancelled();
        const QString path = QDir(root).filePath(name);
        if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
            QDir(path).removeRecursively();
            if (QFileInfo::exists(path))
                return StepOutcome::failed(QString(error::eraseFailed), name);
        }
    }
    if (context.shape().family == Family::Dfu)
        writeRecord(context);
    return StepOutcome::done();
}

} // namespace tacetos::wizard
