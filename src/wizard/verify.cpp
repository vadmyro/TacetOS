// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "verify.h"

#include "constants.h"
#include "firmware.h"
#include "helperconstants.h"
#include "package.h"
#include "wizarderrors.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

namespace tacetos::wizard {

namespace {

const QString kInstalled = QStringLiteral("installed");
const QString kClean = QStringLiteral("clean");

StepOutcome differs(const QString &what)
{
    return StepOutcome::failed(QString(error::verifyFailed), what);
}

} // namespace

StepOutcome VerifyStep::run(StepContext &context)
{
    const FlowShape shape = context.shape();
    const bool install = shape.kind == FlowKind::Install;

    const device::DetectedIpod ipod = m_sources.detected();
    if (ipod.serial.isEmpty() || ipod.state != device::IpodState::Ready)
        return differs(QStringLiteral("the iPod is not ready"));
    const QString root = m_sources.volumeRoot();
    if (root.isEmpty())
        return differs(QStringLiteral("no music volume"));

    // The firmware folder: the files of the package after an install, nothing
    // after an uninstall.
    if (install) {
        PackageFiles files;
        const PackageCheck package =
            sameAsFlow(checkPackage(context.packageManifest(), m_sources.target(), &files),
                       context.packageSha256());
        if (!package.code.isEmpty())
            return StepOutcome::failed(package.code, package.message);
        const QString folder = firmwareFolderDifference(root, files.firmware, m_sources.target());
        if (!folder.isEmpty())
            return differs(folder);
    } else if (QDir(root).exists(QString(device::kFirmwareFolder))) {
        return differs(QStringLiteral("the firmware folder is still there"));
    }

    // The Classic: the firmware it runs now, as detection shows it.
    if (shape.family == Family::Dfu) {
        if (install && ipod.running != device::RunningFirmware::Other)
            return differs(QStringLiteral("the iPod does not run TacetOS"));
        if (!install && ipod.running != device::RunningFirmware::Apple)
            return differs(QStringLiteral("the iPod does not run the Apple firmware"));
        return StepOutcome::done();
    }

    // An older iPod: the bootloader, as the helper reads it.
    const HelperAnswer state =
        m_sources.helper().call(QString(helper::cmd::readState), QJsonObject());
    if (!state.ok())
        return StepOutcome::failed(state.code, state.detail);
    const QString read = state.data.value(QStringLiteral("state")).toString();
    if (read != (install ? kInstalled : kClean))
        return differs(QStringLiteral("the state of the iPod is ") + read);
    return StepOutcome::done();
}

StepOutcome FinishStep::run(StepContext &context)
{
    context.setFact(QStringLiteral("backup"), context.backupFolder());
    if (context.shape().kind == FlowKind::Install) {
        PackageFiles files;
        checkPackage(context.packageManifest(), m_sources.target(), &files);
        context.setFact(QStringLiteral("version"), files.version);
        return StepOutcome::done();
    }
    const QString manifest =
        context.fact(QString(step::backupSettings), QStringLiteral("manifest")).toString();
    if (!manifest.isEmpty())
        context.setFact(QStringLiteral("settings"), QFileInfo(manifest).absolutePath());
    return StepOutcome::done();
}

} // namespace tacetos::wizard
