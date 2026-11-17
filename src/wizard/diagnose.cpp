// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "diagnose.h"

#include "package.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

namespace tacetos::wizard {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("tacetos::wizard::Diagnose", text);
}

Check make(const char *id, Side side, bool blocking)
{
    Check check;
    check.id = QLatin1String(id);
    check.side = side;
    check.blocking = blocking;
    return check;
}

Check &fail(Check &check, const QString &message, CheckResult result = CheckResult::Fail)
{
    check.result = result;
    check.message = message;
    return check;
}

Check &skip(Check &check)
{
    return fail(check, tr("Checked once the computer is ready"), CheckResult::Skipped);
}

bool passed(const QList<Check> &checks, const char *id)
{
    for (const Check &check : checks) {
        if (check.id == QLatin1String(id))
            return check.result == CheckResult::Pass || check.result == CheckResult::Warn;
    }
    return false;
}

QStringList classicVersions(int capacityGb)
{
    QStringList versions;
    for (const ClassicAppleFirmware &entry : kClassicAppleFirmware) {
        const QString version = QLatin1String(entry.version);
        if ((capacityGb == 0 || entry.capacityGb == capacityGb) && !versions.contains(version))
            versions << version;
    }
    return versions;
}

QList<Check> computerChecks(const DiagnoseInput &in)
{
    QList<Check> checks;
    const bool classic = in.family == Family::Dfu;

    Check system = make("system", Side::Computer, true);
    if (in.os != Os::Linux)
        fail(system, tr("Installing on this system comes in a later version of TacetOS"));
    checks << system;

    Check helper = make("helper", Side::Computer, true);
    if (!in.helper.found)
        fail(helper, tr("The helper program is missing. Reinstall TacetOS"));
    else if (!in.helper.protocolOk)
        fail(helper, tr("The helper does not match this version of TacetOS. Reinstall TacetOS"));
    checks << helper;
    const bool helperOk = helper.result == CheckResult::Pass;

    Check writes = make("writes", Side::Computer, false);
    if (!helperOk)
        skip(writes);
    else if (!in.helper.realWrites)
        fail(writes,
             tr("This development build does not write to iPods. Every step runs up to the "
                "first write, then stops"),
             CheckResult::Warn);
    checks << writes;

    if (in.os == Os::Linux) {
        Check udisks = make("udisks", Side::Computer, true);
        if (!in.udisks.present) {
            fail(udisks, tr("The disk service udisks2 is missing"));
            udisks.command = udisksCommand(in.osRelease, false);
        } else if (!in.udisks.running) {
            fail(udisks, tr("udisks2 is not running"));
            udisks.command = udisksCommand(in.osRelease, true);
        } else if (!versionAtLeast(in.udisks.version, QString(kOldestUdisks))) {
            fail(udisks, tr("udisks2 is too old (%1, %2 or newer needed)")
                             .arg(in.udisks.version, QString(kOldestUdisks)));
            udisks.command = udisksCommand(in.osRelease, false);
        }
        checks << udisks;
    }

    if (classic) {
        Check build = make("classic-build", Side::Computer, true);
        if (!helperOk)
            skip(build);
        else if (!in.helper.usb || !in.helper.classic)
            fail(build, tr("Classic install is not available in this build"));
        checks << build;

        if (in.os == Os::Linux) {
            Check rule = make("udev-rule", Side::Computer, true);
            if (!in.udevRulePresent) {
                fail(rule, tr("TacetOS needs a USB permission to talk to the iPod in DFU mode"));
                rule.action = QStringLiteral("install-udev-rule");
            }
            checks << rule;
        }
    }

    Check folder = make("backup-folder", Side::Computer, true);
    if (in.backup.onIpod)
        fail(folder, tr("The backup folder is on an iPod. Choose a folder on this computer"));
    else if (!in.backup.writable)
        fail(folder, tr("Cannot write to the backup folder %1").arg(in.backup.path));
    checks << folder;

    Check space = make("backup-space", Side::Computer, true);
    if (folder.result != CheckResult::Pass)
        fail(space, tr("Checked once the backup folder is ready"), CheckResult::Skipped);
    else if (in.backup.freeBytes < in.backupNeedBytes)
        fail(space, tr("Not enough space for the backup: %1 needed, %2 free")
                        .arg(sizeText(in.backupNeedBytes), sizeText(in.backup.freeBytes)));
    checks << space;
    return checks;
}

QList<Check> ipodChecks(const DiagnoseInput &in, bool helperOk)
{
    QList<Check> checks;
    const bool classic = in.family == Family::Dfu;
    const bool install = in.kind == FlowKind::Install;
    const IpodFacts &ipod = in.ipod;

    const auto add = [&](Check check, bool needs) {
        if (!helperOk || !needs)
            skip(check);
        checks << check;
    };

    Check model = make("model", Side::Ipod, true);
    if (!ipod.helperRead)
        fail(model, tr("The iPod could not be read (%1)").arg(ipod.readError));
    else if (!ipod.modelSupported || !ipod.methodAgrees)
        fail(model, tr("An iPod TacetOS does not support"));
    add(model, true);
    const bool modelOk = helperOk && model.result == CheckResult::Pass;

    Check serial = make("serial", Side::Ipod, true);
    if (ipod.serial.isEmpty())
        fail(serial, tr("The serial number of this iPod could not be read"));
    add(serial, true);

    if (classic) {
        Check single = make("single-ipod", Side::Ipod, true);
        if (ipod.otherIpods > 0)
            fail(single, tr("Unplug the other iPod first"));
        add(single, true);
    }

    Check volume = make("volume", Side::Ipod, true);
    if (!ipod.volumeReady)
        fail(volume, ipod.volumeMessage.isEmpty() ? tr("The music volume of the iPod is not ready")
                                                  : ipod.volumeMessage);
    add(volume, true);
    const bool volumeOk = helperOk && volume.result == CheckResult::Pass;

    Check format = make("format", Side::Ipod, true);
    switch (ipod.filesystem) {
    case Filesystem::Fat32:
        break;
    case Filesystem::HfsPlus:
        // Section 2.2 rule 2: a Mac iPod only takes the clean install, and
        // an iPod Classic none yet (section 6.4).
        if (install && classic)
            fail(format, macClassicRefusal());
        else if (install && in.path == InstallPath::Keep)
            fail(format, tr("This iPod is formatted for Mac. TacetOS needs a Windows format, "
                            "which erases the iPod"));
        else if (install)
            fail(format, tr("This iPod is formatted for Mac"), CheckResult::Warn);
        break;
    case Filesystem::Other:
    case Filesystem::Unknown:
        fail(format, tr("This file system cannot be used"));
        break;
    }
    add(format, volumeOk);

    // On an uninstall, the Classic needs no version of Apple's firmware
    // (bootloader-classic.md section 14.1).
    if (install || !classic) {
        Check apple = make("apple-firmware", Side::Ipod, true);
        if (!classic) {
            if (!ipod.firmwarePartitionOk)
                fail(apple, tr("Apple's firmware is not on this iPod. Restore it with Apple's "
                               "software first"));
        } else if (ipod.appleVersion.isEmpty()) {
            fail(apple, tr("The version of Apple's firmware could not be read"));
        } else {
            const QStringList versions = classicVersions(ipod.capacityGb);
            if (!versions.contains(ipod.appleVersion))
                fail(apple, tr("Update the iPod with Apple's software first: version %1 needed, "
                               "%2 found")
                                .arg(versions.join(tr(" or ")), ipod.appleVersion));
        }
        add(apple, modelOk);
    }

    if (!classic) {
        Check state = make("bootloader-state", Side::Ipod, true);
        if (ipod.bootloaderState == QLatin1String("inconsistent"))
            fail(state, tr("The firmware partition of this iPod is not as expected. Nothing will "
                           "be written"));
        else if (ipod.bootloaderState != QLatin1String("clean") &&
                 ipod.bootloaderState != QLatin1String("installed"))
            fail(state, tr("The firmware partition of this iPod could not be read"));
        add(state, modelOk);
    }

    if (install) {
        Check package = make("package", Side::Ipod, true);
        if (!in.package.code.isEmpty())
            fail(package, in.package.message);
        add(package, modelOk);

        Check room = make("firmware-room", Side::Ipod, true);
        const qint64 available = in.path == InstallPath::Clean ? ipod.volumeSize : ipod.volumeFree;
        if (available < in.firmwareNeedBytes)
            fail(room, tr("Not enough space on the iPod for TacetOS: %1 needed, %2 free")
                           .arg(sizeText(in.firmwareNeedBytes), sizeText(available)));
        add(room, volumeOk && package.result == CheckResult::Pass);
    }

    Check battery = make("battery", Side::Ipod, true);
    if (!in.batteryCharged.has_value())
        fail(battery, tr("Is the iPod charged?"), CheckResult::Ask);
    else if (!*in.batteryCharged)
        fail(battery, tr("Charge the iPod for an hour first"));
    add(battery, true);
    return checks;
}

} // namespace

// "92 GB", "4.5 GB", "40 MB": decimal units, as the systems show free space.
QString sizeText(qint64 bytes)
{
    const auto unit = [bytes](double size, const char *name) {
        const double value = static_cast<double>(bytes) / size;
        return QString::number(value, 'f', value < 10 ? 1 : 0) + QLatin1Char(' ') +
               QLatin1String(name);
    };
    if (bytes >= 1000LL * 1000 * 1000)
        return unit(1e9, "GB");
    if (bytes >= 1000LL * 1000)
        return unit(1e6, "MB");
    return unit(1e3, "KB");
}

QString macClassicRefusal()
{
    return tr("This iPod Classic is formatted for Mac, which TacetOS cannot change yet. "
              "Restore it with Apple's software on a Windows computer first");
}

QList<Check> diagnose(const DiagnoseInput &input)
{
    QList<Check> checks = computerChecks(input);
    // Section 4.1 rule 2: the iPod is read through the helper.
    checks << ipodChecks(input, passed(checks, "helper"));
    return checks;
}

const Check *firstBlocking(const QList<Check> &checks)
{
    for (const Check &check : checks) {
        if (check.blocking &&
            (check.result == CheckResult::Fail || check.result == CheckResult::Ask ||
             check.result == CheckResult::Skipped))
            return &check;
    }
    return nullptr;
}

bool canGoOn(const QList<Check> &checks)
{
    return firstBlocking(checks) == nullptr;
}

QString udisksCommand(const QString &osRelease, bool notRunning)
{
    if (notRunning)
        return QStringLiteral("sudo systemctl enable --now udisks2");

    QString id;
    QString like;
    for (const QString &line : osRelease.split(QLatin1Char('\n'))) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 &&
            (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))))
            value = value.mid(1, value.size() - 2);
        if (key == QLatin1String("ID"))
            id = value;
        else if (key == QLatin1String("ID_LIKE"))
            like = value;
    }
    QStringList names{id};
    names << like.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &name : names) {
        const QString n = name.toLower();
        if (n == QLatin1String("fedora") || n == QLatin1String("rhel") ||
            n == QLatin1String("centos"))
            return QStringLiteral("sudo dnf install udisks2");
        if (n == QLatin1String("debian") || n == QLatin1String("ubuntu"))
            return QStringLiteral("sudo apt install udisks2");
        if (n == QLatin1String("arch"))
            return QStringLiteral("sudo pacman -S udisks2");
    }
    return QString();
}

bool versionAtLeast(const QString &version, const QString &oldest)
{
    const QStringList a = version.split(QLatin1Char('.'));
    const QStringList b = oldest.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        bool okA = true;
        bool okB = true;
        const int x = i < a.size() ? a.at(i).toInt(&okA) : 0;
        const int y = i < b.size() ? b.at(i).toInt(&okB) : 0;
        if (!okA || !okB)
            return false;
        if (x != y)
            return x > y;
    }
    return true;
}

QString readOsRelease(const QString &root)
{
    for (const char *name : {"etc/os-release", "usr/lib/os-release"}) {
        QFile file(QDir(root).filePath(QLatin1String(name)));
        if (file.open(QIODevice::ReadOnly))
            return QString::fromUtf8(file.read(64 * 1024));
    }
    return QString();
}

FolderInfo folderInfo(const QString &path, const QStringList &ipodRoots)
{
    FolderInfo info;
    info.path = path;
    if (!QDir::isAbsolutePath(path))
        return info;

    // The folder may not exist yet: the nearest folder that does tells where
    // it will be.
    QString existing = QDir::cleanPath(path);
    while (!QFileInfo(existing).isDir()) {
        const QString parent = QFileInfo(existing).absolutePath();
        if (parent == existing)
            return info;
        existing = parent;
    }
    const QStorageInfo storage(existing);
    info.writable = QFileInfo(existing).isWritable();
    info.freeBytes = storage.isValid() ? storage.bytesAvailable() : -1;

    const QString clean = QDir::cleanPath(path);
    for (const QString &root : ipodRoots) {
        const QString r = QDir::cleanPath(root);
        if (clean == r || clean.startsWith(r + QLatin1Char('/')))
            info.onIpod = true;
        const QStorageInfo ipod(r);
        if (ipod.isValid() && storage.isValid() && ipod.device() == storage.device() &&
            ipod.rootPath() == storage.rootPath() && ipod.rootPath() == r)
            info.onIpod = true;
    }
    return info;
}

StepOutcome DiagnoseStep::run(StepContext &context)
{
    const FlowShape shape = context.shape();
    DiagnoseInput in;
    in.os = m_sources.os();
    in.osRelease = m_sources.osRelease();
    in.udisks = m_sources.udisks();
    in.helper = m_sources.helper();
    in.udevRulePresent = m_sources.udevRulePresent();
    in.backup = m_sources.backupFolder();
    in.backupNeedBytes = m_sources.backupNeedBytes(shape.path);
    in.family = shape.family;
    in.kind = shape.kind;
    in.path = shape.path;
    in.ipod = m_sources.ipod();
    if (shape.kind == FlowKind::Install) {
        in.package =
            sameAsFlow(m_sources.package(context.packageManifest()), context.packageSha256());
        in.firmwareNeedBytes = m_sources.firmwareNeedBytes(in.package);
    }

    for (;;) {
        m_checks = diagnose(in);
        const Check *blocking = firstBlocking(m_checks);
        if (blocking && blocking->result == CheckResult::Ask && blocking->id == "battery") {
            const UserAnswer answer = context.waitForUser(WaitKind::Battery);
            if (answer.cancelled)
                return StepOutcome::cancelled();
            in.batteryCharged = answer.yes;
            continue;
        }
        break;
    }

    if (shape.family == Family::Partition && !in.ipod.bootloaderState.isEmpty())
        context.setFact(QStringLiteral("state"), in.ipod.bootloaderState);
    if (const Check *blocking = firstBlocking(m_checks))
        return StepOutcome::failed(QString(error::checkBlocking), blocking->id);
    return StepOutcome::done();
}

} // namespace tacetos::wizard
