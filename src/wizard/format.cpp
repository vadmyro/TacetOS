// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "format.h"

#include "backup.h"
#include "helperconstants.h"
#include "volume.h"
#include "wizarderrors.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QMutexLocker>
#include <algorithm>

// The rule of the typed name (bootloader-legacy.md section 22), from the
// installer, whose header has the same name as this one.
namespace tacetos::installer {
bool confirmationMatches(const QString &shown, const QString &typed);
}

namespace tacetos::wizard {

namespace {

const QString kStageVolume = QStringLiteral("volume");
const QString kStageErase = QStringLiteral("erase");

// How often a wait looks at `stop`.
constexpr qint64 kStopLookMs = 100;

bool systemMade(const QString &name)
{
    if (name.startsWith(kLinuxTrashPrefix))
        return true;
    return std::any_of(std::begin(kSystemMadeNames), std::end(kSystemMadeNames),
                       [&name](const char *made) { return name == QLatin1String(made); });
}

} // namespace

QString newVolumeLabel(const QString &oldLabel)
{
    const QString label = oldLabel.toUpper();
    const bool valid = !label.trimmed().isEmpty() && label.size() <= kFat32LabelMax &&
                       std::all_of(label.cbegin(), label.cend(), [](QChar c) {
                           return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                                  (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                                  c == QLatin1Char(' ') || c == QLatin1Char('_');
                       });
    return valid ? label : QString(kDefaultVolumeLabel);
}

bool isEmptyFat32(const MusicVolume &volume)
{
    if (device::classifyFilesystem(volume.filesystem) != device::FilesystemKind::Fat32)
        return false;
    const QDir root(volume.mountPoint);
    if (volume.mountPoint.isEmpty() || !root.exists())
        return false;
    const QStringList names =
        root.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    return std::all_of(names.cbegin(), names.cend(), systemMade);
}

DetectionVolumeWatch::DetectionVolumeWatch(Mounter mounter, QObject *parent)
    : QObject(parent), m_mounter(std::move(mounter))
{
}

void DetectionVolumeWatch::forget(const QString &serial)
{
    QMutexLocker locker(&m_mutex);
    m_ready.remove(serial);
}

void DetectionVolumeWatch::mount(const QString &serial)
{
    if (m_mounter)
        m_mounter(serial);
}

std::optional<MusicVolume> DetectionVolumeWatch::waitReady(const QString &serial, qint64 ms,
                                                           const std::function<bool()> &stop)
{
    const QDeadlineTimer deadline(ms);
    QMutexLocker locker(&m_mutex);
    while (!m_ready.contains(serial)) {
        if ((stop && stop()) || deadline.hasExpired())
            return std::nullopt;
        m_changed.wait(&m_mutex, QDeadlineTimer(std::min(kStopLookMs, deadline.remainingTime())));
    }
    return m_ready.value(serial);
}

void DetectionVolumeWatch::seen(const device::DetectedIpod &ipod)
{
    if (ipod.serial.isEmpty())
        return;
    QMutexLocker locker(&m_mutex);
    if (ipod.state == device::IpodState::Ready && ipod.volume && !ipod.volume->mountPoint.isEmpty())
        m_ready.insert(ipod.serial,
                       {ipod.volume->mountPoint, ipod.volume->filesystem, ipod.volume->label});
    else
        m_ready.remove(ipod.serial);
    m_changed.wakeAll();
}

void DetectionSightWatch::clear()
{
    QMutexLocker locker(&m_mutex);
    m_queue.clear();
}

std::optional<IpodSight> DetectionSightWatch::next(qint64 ms, const std::function<bool()> &stop)
{
    const QDeadlineTimer deadline(ms);
    QMutexLocker locker(&m_mutex);
    while (m_queue.isEmpty()) {
        if ((stop && stop()) || deadline.hasExpired())
            return std::nullopt;
        m_changed.wait(&m_mutex, QDeadlineTimer(std::min(kStopLookMs, deadline.remainingTime())));
    }
    return m_queue.takeFirst();
}

void DetectionSightWatch::seen(const device::DetectedIpod &ipod)
{
    QMutexLocker locker(&m_mutex);
    IpodSight sight;
    sight.kind = IpodSight::Kind::Seen;
    sight.ipod = ipod;
    sight.portPath = ipod.portPath;
    m_queue.append(sight);
    m_changed.wakeAll();
}

void DetectionSightWatch::gone(const QString &portPath)
{
    QMutexLocker locker(&m_mutex);
    IpodSight sight;
    sight.kind = IpodSight::Kind::Gone;
    sight.portPath = portPath;
    m_queue.append(sight);
    m_changed.wakeAll();
}

StepOutcome FormatStep::run(StepContext &context)
{
    const FlowShape shape = context.shape();
    const QString serial = context.serial();
    const QString folder = context.backupFolder();

    // Section 6.1: nothing is erased without a verified full backup. The
    // conversion also reads the copy of the firmware partition again
    // (section 5.6).
    QString why;
    if (!isVerifiedFullBackup(folder, serial, shape.family, &why))
        return StepOutcome::failed(QString(error::backupNotVerified), why);
    if (m_convert && shape.family == Family::Partition &&
        checkedFirmwarePartition(folder, serial, &why).isEmpty())
        return StepOutcome::failed(QString(error::backupNotVerified), why);

    if (context.isResume()) {
        if (const auto outcome = resumed(context))
            return *outcome;
    }

    // Section 6.2: the name to type, and what will be erased, from the
    // manifest that has just counted it.
    const QString label = m_sources.volumeLabel();
    const QString name = label.isEmpty() ? m_sources.modelName() : label;
    const BackupManifest manifest = readManifest(folder).manifest;
    qint64 bytes = 0;
    for (const BackupFile &file : manifest.files)
        bytes += file.size;
    QJsonObject info;
    info.insert(QStringLiteral("ipodName"), name);
    info.insert(QStringLiteral("files"), manifest.files.size());
    info.insert(QStringLiteral("bytes"), double(bytes));
    const UserAnswer answer = context.waitForUser(WaitKind::TypedName, info);
    if (answer.cancelled)
        return StepOutcome::cancelled();
    if (shape.family == Family::Dfu && !m_convert)
        return emptyByFiles(context, name, answer.text);

    // Section 6.3 steps 1 and 2.
    QJsonObject args;
    args.insert(QStringLiteral("ipodName"), name);
    args.insert(QStringLiteral("typedName"), answer.text);
    args.insert(QStringLiteral("volumeLabel"), newVolumeLabel(label));
    if (m_convert)
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
    m_sources.volumes().forget(serial);
    const HelperAnswer result = m_sources.helper().callWithToken(
        QString(m_convert ? helper::cmd::convertToFat32 : helper::cmd::formatMusic), args, watch);
    if (result.code == helper::err::cancelled)
        return StepOutcome::cancelled();
    if (!result.ok())
        return StepOutcome::failed(result.code, result.detail);
    return volumeBack(context);
}

// Section 3.3: the rule of resume of each step. Empty when the step runs again
// from the start.
std::optional<StepOutcome> FormatStep::resumed(StepContext &context)
{
    if (m_convert) {
        // A Windows iPod: the conversion reached its last write.
        const HelperAnswer state =
            m_sources.helper().call(QString(helper::cmd::readState), QJsonObject());
        if (!state.ok())
            return StepOutcome::failed(state.code, state.detail);
        if (state.data.value(QStringLiteral("table")).toString() == QLatin1String("dos"))
            return volumeBack(context);
        return std::nullopt;
    }
    // A FAT32 volume with nothing in it: the format was done.
    VolumeWatch &volumes = m_sources.volumes();
    volumes.mount(context.serial());
    const auto volume = volumes.waitReady(context.serial(), m_sources.volumeReturnMs(),
                                          [&context] { return context.stopRequested(); });
    if (context.stopRequested())
        return StepOutcome::cancelled();
    if (volume && isEmptyFat32(*volume))
        return StepOutcome::done();
    return std::nullopt;
}

// Section 6.4: an iPod Classic is emptied through the file system. The helper
// is not used, so the typed name is checked here, with its rule.
StepOutcome FormatStep::emptyByFiles(StepContext &context, const QString &name,
                                     const QString &typed)
{
    if (!installer::confirmationMatches(name, typed))
        return StepOutcome::failed(QString(helper::err::confirmationMismatch),
                                   QStringLiteral("the typed name is not the name of the iPod"));
    const auto volume =
        m_sources.volumes().waitReady(context.serial(), m_sources.volumeReturnMs(),
                                      [&context] { return context.stopRequested(); });
    if (context.stopRequested())
        return StepOutcome::cancelled();
    if (!volume)
        return StepOutcome::failed(QString(error::ipodGone));
    if (device::classifyFilesystem(volume->filesystem) != device::FilesystemKind::Fat32)
        return StepOutcome::failed(QString(error::badFlow),
                                   QStringLiteral("the volume of the Classic is not FAT32"));

    const QDir root(volume->mountPoint);
    QStringList names =
        root.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    names.erase(std::remove_if(names.begin(), names.end(), systemMade), names.end());
    // The first deletion is the first write: no cancel after it.
    if (context.stopRequested())
        return StepOutcome::cancelled();
    qint64 done = 0;
    context.progress(kStageErase, 0, names.size());
    for (const QString &entry : std::as_const(names)) {
        const QFileInfo info(root.filePath(entry));
        if (info.isDir() && !info.isSymLink())
            QDir(info.filePath()).removeRecursively();
        else
            QFile::remove(info.filePath());
        context.progress(kStageErase, ++done, names.size());
    }
    if (!isEmptyFat32(*volume)) {
        if (!root.exists())
            return StepOutcome::failed(QString(error::ipodGone));
        return StepOutcome::failed(QString(error::eraseFailed),
                                   QStringLiteral("some entries are left on the volume"));
    }
    return StepOutcome::done();
}

// Section 6.3 steps 3 and 4.
StepOutcome FormatStep::volumeBack(StepContext &context)
{
    VolumeWatch &volumes = m_sources.volumes();
    context.progress(kStageVolume, 0, 0);
    volumes.mount(context.serial());
    const auto volume = volumes.waitReady(context.serial(), m_sources.volumeReturnMs(),
                                          [&context] { return context.stopRequested(); });
    if (context.stopRequested())
        return StepOutcome::cancelled();
    if (!volume)
        return StepOutcome::failed(QString(error::volumeNotBack));
    if (!isEmptyFat32(*volume))
        return StepOutcome::failed(QString(error::volumeNotBack),
                                   QStringLiteral("the volume is not an empty FAT32 volume"));
    return StepOutcome::done();
}

} // namespace tacetos::wizard
