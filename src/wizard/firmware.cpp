// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "firmware.h"

#include "backup.h"
#include "constants.h"
#include "crc32.h"
#include "markers.h"
#include "package.h"
#include "wizardconstants.h"
#include "wizarderrors.h"
#include "zipreader.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <memory>
#include <optional>

#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <unistd.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace tacetos::wizard {

namespace {

const QString kStageUnpack = QStringLiteral("unpack");
const QString kStageKeep = QStringLiteral("keep");
const QString kStageCheck = QStringLiteral("check");

constexpr qint64 kPiece = qint64(1) << 20;

template <typename List> bool inList(const List &names, const QString &name)
{
    return std::any_of(std::begin(names), std::end(names), [&](const char *n) {
        return name.compare(QLatin1String(n), Qt::CaseInsensitive) == 0;
    });
}

// The path of an entry inside the firmware folder: the top folder of the zip
// is not kept. Empty for the top folder itself.
QString insidePath(const archive::ZipEntry &entry)
{
    QString rest = entry.name.section(QLatin1Char('/'), 1);
    if (rest.endsWith(QLatin1Char('/')))
        rest.chop(1);
    return rest;
}

quint32 fileCrc(const QString &path, qint64 *size)
{
    QFile file(path);
    archive::Crc32 crc;
    *size = -1;
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    *size = 0;
    while (!file.atEnd()) {
        const QByteArray piece = file.read(kPiece);
        if (piece.isEmpty())
            break;
        crc.add(piece);
        *size += piece.size();
    }
    return crc.value();
}

// Section 7.4 step 7: the volume written to the iPod. The eject that follows
// the flow flushes it again on every system.
void syncVolume(const QString &root)
{
#if defined(Q_OS_LINUX)
    const int fd = ::open(QFile::encodeName(root).constData(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::syncfs(fd);
        ::close(fd);
    }
#elif defined(Q_OS_UNIX)
    Q_UNUSED(root);
    ::sync();
#else
    Q_UNUSED(root);
#endif
}

// Section 7.3: one kept file, where it comes from and where it goes.
struct Kept
{
    QString from;
    // Inside the firmware folder.
    QString path;
    qint64 size = 0;
    QString sha256;
};

// The kept files of a firmware folder, less those of the folders of section
// 7.3 that the package has itself (its file wins).
QList<Kept> keptFiles(const QString &folder, const QSet<QString> &packagePaths)
{
    QList<Kept> kept;
    const QFileInfoList entries =
        QDir(folder).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        if (info.isSymLink())
            continue;
        if (info.isFile() && inList(device::kKeptFirmwareFiles, info.fileName())) {
            kept << Kept{info.filePath(), info.fileName(), info.size(), QString()};
            continue;
        }
        if (!info.isDir() || !inList(device::kKeptFirmwareFolders, info.fileName()))
            continue;
        QDirIterator it(info.filePath(), QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString from = it.next();
            const QString path = QDir(folder).relativeFilePath(from);
            if (!packagePaths.contains(path.toCaseFolded()))
                kept << Kept{from, path, QFileInfo(from).size(), QString()};
        }
    }
    std::sort(kept.begin(), kept.end(),
              [](const Kept &a, const Kept &b) { return a.path < b.path; });
    return kept;
}

class Install
{
public:
    Install(const FirmwareSources &sources, StepContext &context)
        : m_sources(sources), m_context(context), m_root(sources.volumeRoot()),
          m_folder(m_root.filePath(device::kFirmwareFolder)), m_new(m_folder + kNewSuffix),
          m_old(m_folder + kOldSuffix)
    {
    }

    StepOutcome run();

private:
    enum class After { FromStart, CheckAfter };

    bool exists(const QString &path) const { return QFileInfo(path).isDir(); }
    StepOutcome writeFailed(const QString &detail)
    {
        return StepOutcome::failed(QString(error::firmwareWriteFailed), detail);
    }

    After cleanUp();
    std::optional<StepOutcome> unpack();
    std::optional<StepOutcome> keep(QList<Kept> &kept);
    StepOutcome swapAndCheck(const QList<Kept> &kept);
    StepOutcome checkAfter(const QList<Kept> &kept);

    const FirmwareSources &m_sources;
    StepContext &m_context;
    QDir m_root;
    QString m_folder;
    QString m_new;
    QString m_old;
    std::unique_ptr<archive::ZipReader> m_zip;
    QSet<QString> m_packagePaths;
};

// Section 7.5: what the three folders tell, and what is done about it.
Install::After Install::cleanUp()
{
    const bool firmware = exists(m_folder);
    const bool fresh = exists(m_new);
    const bool old = exists(m_old);
    if (fresh && !old) {
        QDir(m_new).removeRecursively();
        return After::FromStart;
    }
    if (fresh && old && !firmware) {
        QDir().rename(m_new, m_folder);
        QDir(m_old).removeRecursively();
        return After::CheckAfter;
    }
    if (fresh) {
        // Not a state the step leaves: the new folder is unfinished.
        QDir(m_new).removeRecursively();
    }
    if (old && exists(m_folder)) {
        QDir(m_old).removeRecursively();
        return After::CheckAfter;
    }
    if (old) {
        QDir().rename(m_old, m_folder);
        return After::FromStart;
    }
    return m_context.fact(QString(step::firmware), QStringLiteral("swapped")).toBool()
               ? After::CheckAfter
               : After::FromStart;
}

StepOutcome Install::run()
{
    // Step 1: the package, again: the files may have changed on the computer.
    PackageFiles files;
    const PackageCheck package =
        sameAsFlow(checkPackage(m_context.packageManifest(), m_sources.target(), &files),
                   m_context.packageSha256());
    if (!package.code.isEmpty())
        return StepOutcome::failed(package.code, package.message);
    m_zip = std::make_unique<archive::ZipReader>(files.firmware);
    if (!m_zip->open({kZipEntryMax, kZipUnpackMax}))
        return StepOutcome::failed(QString(error::packageDamaged), m_zip->error());
    for (const archive::ZipEntry &entry : m_zip->entries())
        m_packagePaths.insert(insidePath(entry).toCaseFolded());

    // Step 2.
    if (cleanUp() == After::CheckAfter) {
        const QList<Kept> none;
        return checkAfter(none);
    }

    // Section 7.3: the kept files, from the old folder, or from the newest
    // settings backup when the user takes the offer.
    QString keptFrom = exists(m_folder) ? m_folder : QString();
    const QString backup = m_sources.settingsBackup();
    if (keptFrom.isEmpty() && !backup.isEmpty() && isVerifiedBackup(backup, m_context.serial())) {
        QJsonObject info;
        info.insert(QStringLiteral("offer"), QStringLiteral("settings"));
        info.insert(QStringLiteral("backup"), backup);
        info.insert(QStringLiteral("created"),
                    readManifest(backup).manifest.created.toString(Qt::ISODate));
        const UserAnswer answer = m_context.waitForUser(WaitKind::Offer, info);
        if (answer.cancelled)
            return StepOutcome::cancelled();
        if (answer.yes)
            keptFrom = QDir(backupVolumeFolder(backup)).filePath(device::kFirmwareFolder);
    }
    QList<Kept> kept = keptFrom.isEmpty() ? QList<Kept>() : keptFiles(keptFrom, m_packagePaths);
    qint64 keptBytes = 0;
    for (const Kept &file : std::as_const(kept))
        keptBytes += file.size;

    // Section 7.2: the old folder is not counted as free.
    const qint64 need = package.unpackedBytes + keptBytes + kVolumeMargin;
    const qint64 free = m_sources.freeBytes();
    if (free >= 0 && free < need)
        return StepOutcome::failed(QString(error::firmwareNoRoom), QString::number(need));

    // Steps 3 and 4. Until the swap, a cancel leaves the old folder as it was.
    if (const auto stopped = unpack())
        return *stopped;
    if (const auto stopped = keep(kept))
        return *stopped;
    if (m_context.stopRequested()) {
        QDir(m_new).removeRecursively();
        return StepOutcome::cancelled();
    }
    return swapAndCheck(kept);
}

// Step 3: each file written, flushed, closed, then read back and its CRC-32
// compared with the zip.
std::optional<StepOutcome> Install::unpack()
{
    const installer::FileFault fault = m_sources.fault();
    const auto give = [this](const StepOutcome &outcome) {
        QDir(m_new).removeRecursively();
        return std::optional<StepOutcome>(outcome);
    };
    if (!QDir().mkpath(m_new))
        return give(writeFailed(QStringLiteral("the new folder cannot be made")));
    const qint64 total = m_zip->unpackedSize();
    qint64 done = 0;
    m_context.progress(kStageUnpack, 0, total);
    for (const archive::ZipEntry &entry : m_zip->entries()) {
        const QString path = insidePath(entry);
        if (path.isEmpty())
            continue;
        const QString target = QDir(m_new).filePath(path);
        if (entry.folder) {
            if (!QDir().mkpath(target))
                return give(writeFailed(QStringLiteral("a folder cannot be made")));
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return give(writeFailed(QStringLiteral("a folder cannot be made")));
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return give(writeFailed(out.errorString()));
        bool stopped = false;
        QString writeError;
        qint64 written = 0;
        const bool ok = m_zip->extract(entry, [&](const char *data, qint64 size) {
            if (m_context.stopRequested()) {
                stopped = true;
                return false;
            }
            writeError = fault ? fault(target, written) : QString();
            if (writeError.isEmpty() && out.write(data, size) != size)
                writeError = out.errorString();
            if (!writeError.isEmpty())
                return false;
            written += size;
            done += size;
            m_context.progress(kStageUnpack, done, total);
            return true;
        });
        const bool flushed = out.flush();
        out.close();
        if (stopped)
            return give(StepOutcome::cancelled());
        if (!writeError.isEmpty() || (ok && !flushed))
            return give(writeFailed(writeError.isEmpty() ? out.errorString() : writeError));
        if (!ok)
            return give(StepOutcome::failed(QString(error::packageDamaged), m_zip->error()));
        qint64 size = 0;
        if (fileCrc(target, &size) != entry.crc || size != entry.size)
            return give(writeFailed(QStringLiteral("a file reads back different")));
    }
    return std::nullopt;
}

// Step 4: each kept file copied into the new folder, read back and compared
// with its sha256.
std::optional<StepOutcome> Install::keep(QList<Kept> &kept)
{
    const installer::FileFault fault = m_sources.fault();
    qint64 total = 0;
    for (const Kept &file : kept)
        total += file.size;
    qint64 done = 0;
    m_context.progress(kStageKeep, 0, total);
    for (Kept &file : kept) {
        if (m_context.stopRequested()) {
            QDir(m_new).removeRecursively();
            return StepOutcome::cancelled();
        }
        const QString target = QDir(m_new).filePath(file.path);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile in(file.from);
        QFile out(target);
        if (!in.open(QIODevice::ReadOnly) ||
            !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QDir(m_new).removeRecursively();
            return writeFailed(QStringLiteral("a kept file cannot be copied"));
        }
        QString failure;
        while (!in.atEnd() && failure.isEmpty()) {
            const QByteArray piece = in.read(kPiece);
            failure = fault ? fault(target, out.pos()) : QString();
            if (failure.isEmpty() && out.write(piece) != piece.size())
                failure = out.errorString();
        }
        if (failure.isEmpty() && !out.flush())
            failure = out.errorString();
        out.close();
        const QString sha = fileSha256(file.from);
        if (failure.isEmpty() && (sha.isEmpty() || fileSha256(target) != sha))
            failure = QStringLiteral("a kept file reads back different");
        if (!failure.isEmpty()) {
            QDir(m_new).removeRecursively();
            return writeFailed(failure);
        }
        file.sha256 = sha;
        done += file.size;
        m_context.progress(kStageKeep, done, total);
    }
    return std::nullopt;
}

// Step 5: from the first rename on, the step goes to its end.
StepOutcome Install::swapAndCheck(const QList<Kept> &kept)
{
    const bool hadFolder = exists(m_folder);
    if (hadFolder && !QDir().rename(m_folder, m_old)) {
        QDir(m_new).removeRecursively();
        return writeFailed(QStringLiteral("the old folder cannot be renamed"));
    }
    if (!QDir().rename(m_new, m_folder)) {
        // Section 7.5, the fourth row, at once.
        if (hadFolder)
            QDir().rename(m_old, m_folder);
        QDir(m_new).removeRecursively();
        return writeFailed(QStringLiteral("the new folder cannot be renamed"));
    }
    m_context.setFact(QStringLiteral("swapped"), true);
    if (hadFolder)
        QDir(m_old).removeRecursively();
    return checkAfter(kept);
}

// Step 6, then step 7.
StepOutcome Install::checkAfter(const QList<Kept> &kept)
{
    const auto failed = [this](const QString &why) {
        // The next run starts again from the unpack.
        m_context.setFact(QStringLiteral("swapped"), false);
        return writeFailed(why);
    };
    const QDir folder(m_folder);
    m_context.progress(kStageCheck, 0, 0);
    for (const archive::ZipEntry &entry : m_zip->entries()) {
        const QString path = insidePath(entry);
        if (path.isEmpty() || entry.folder)
            continue;
        qint64 size = 0;
        if (fileCrc(folder.filePath(path), &size) != entry.crc || size != entry.size)
            return failed(QStringLiteral("a file of the firmware is missing or different"));
    }
    for (const Kept &file : kept) {
        if (fileSha256(folder.filePath(file.path)) != file.sha256)
            return failed(QStringLiteral("a kept file is missing or different"));
    }
    const device::MarkerRead info =
        device::readMarkerFile(m_root.path(), QString(device::kFirmwareFolder) + QLatin1Char('/') +
                                                  QString(device::kFirmwareInfoFile));
    const device::InfoFile parsed =
        device::parseInfoFile(info.text, QString(device::kDistributionKey));
    if (parsed.target.compare(m_sources.target(), Qt::CaseInsensitive) != 0)
        return failed(QStringLiteral("the info file of the firmware is not for this model"));
    syncVolume(m_root.path());
    return StepOutcome::done();
}

} // namespace

StepOutcome FirmwareStep::run(StepContext &context)
{
    Install install(m_sources, context);
    return install.run();
}

} // namespace tacetos::wizard
