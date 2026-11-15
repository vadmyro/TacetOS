// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "backup.h"

#include "constants.h"
#include "helperclient.h"
#include "helperconstants.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>

namespace tacetos::wizard {

namespace {

const QString kStageCopy = QStringLiteral("copy");
const QString kStageFirmware = QStringLiteral("firmware-partition");
const QString kStageCheck = QStringLiteral("check");

QString isoUtc(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime fromIso(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}

bool sameName(const QString &a, const QString &b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

template <typename List> bool inList(const List &names, const QString &name)
{
    return std::any_of(std::begin(names), std::end(names),
                       [&](const char *n) { return sameName(QString::fromUtf8(n), name); });
}

QString joinPath(const QString &rel, const QString &name)
{
    return rel.isEmpty() ? name : rel + QLatin1Char('/') + name;
}

// The entries of a folder in the order of section 5.5: folders first, then
// files, each sorted by name. Links are counted and left out.
QFileInfoList sortedEntries(const QString &folder, int *symlinks)
{
    QFileInfoList all = QDir(folder).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
    QFileInfoList kept;
    for (const QFileInfo &info : std::as_const(all)) {
        if (info.isSymLink()) {
            ++*symlinks;
            continue;
        }
        kept << info;
    }
    std::sort(kept.begin(), kept.end(), [](const QFileInfo &a, const QFileInfo &b) {
        if (a.isDir() != b.isDir())
            return a.isDir();
        return a.fileName() < b.fileName();
    });
    return kept;
}

void walk(const QString &root, const QString &rel, BackupPlan &plan)
{
    const QFileInfoList entries =
        sortedEntries(rel.isEmpty() ? root : root + QLatin1Char('/') + rel, &plan.symlinks);
    for (const QFileInfo &info : entries) {
        const QString path = joinPath(rel, info.fileName());
        if (info.isDir()) {
            plan.folders << path;
            walk(root, path, plan);
        } else {
            plan.files << path;
            plan.bytes += info.size();
        }
    }
}

void addFile(const QFileInfo &info, const QString &path, BackupPlan &plan)
{
    plan.files << path;
    plan.bytes += info.size();
}

void addFolder(const QString &root, const QString &path, BackupPlan &plan)
{
    plan.folders << path;
    walk(root, path, plan);
}

bool isPlaylistFile(const QString &name)
{
    return inList(kPlaylistSuffixes, QFileInfo(name).suffix());
}

struct PieceCopy
{
    enum class Failure { None, Read, Open, Write, Cancelled };
    Failure failure = Failure::None;
    QString detail;
    QString sha256;
    qint64 bytes = 0;
};

// Copies `from` to `to` by pieces of `kCopyPiece`, the sha256 computed while
// reading. `fault` is asked before each piece written.
PieceCopy copyPieces(const QString &from, const QString &to, const installer::FileFault &fault,
                     const std::function<bool()> &stop, const std::function<void(qint64)> &advance)
{
    PieceCopy out;
    QFile in(from);
    if (!in.open(QIODevice::ReadOnly)) {
        out.failure = PieceCopy::Failure::Read;
        out.detail = in.errorString();
        return out;
    }
    QFile file(to);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.failure = PieceCopy::Failure::Open;
        out.detail = file.errorString();
        return out;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (true) {
        if (stop && stop()) {
            out.failure = PieceCopy::Failure::Cancelled;
            break;
        }
        const QByteArray piece = in.read(kCopyPiece);
        if (piece.isEmpty() && in.error() != QFileDevice::NoError) {
            out.failure = PieceCopy::Failure::Read;
            out.detail = in.errorString();
            break;
        }
        if (piece.isEmpty() && in.atEnd())
            break;
        const QString fail = fault ? fault(to, out.bytes) : QString();
        if (!fail.isEmpty() || file.write(piece) != piece.size()) {
            out.failure = PieceCopy::Failure::Write;
            out.detail = fail.isEmpty() ? file.errorString() : fail;
            break;
        }
        hash.addData(piece);
        out.bytes += piece.size();
        if (advance)
            advance(piece.size());
        if (in.atEnd())
            break;
    }
    if (out.failure == PieceCopy::Failure::None && !file.flush()) {
        out.failure = PieceCopy::Failure::Write;
        out.detail = file.errorString();
    }
    file.close();
    if (out.failure != PieceCopy::Failure::None) {
        QFile::remove(to);
        return out;
    }
    out.sha256 = QString::fromLatin1(hash.result().toHex());
    return out;
}

QString fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray piece = file.read(kCopyPiece);
        if (piece.isEmpty() && file.error() != QFileDevice::NoError)
            return QString();
        hash.addData(piece);
    }
    return QString::fromLatin1(hash.result().toHex());
}

// A write to the computer failed: a full disk, or something else of its disk.
QString writeFailureCode(const QString &detail, const QString &folder)
{
    if (detail == QLatin1String(installer::kNoSpace))
        return QString(error::backupFullDisk);
    const QStorageInfo storage(folder);
    if (storage.isValid() && storage.bytesAvailable() < kCopyPiece)
        return QString(error::backupFullDisk);
    return QString(error::backupVerifyFailed);
}

bool setModified(const QString &path, const QDateTime &time)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite) &&
           file.setFileTime(time, QFileDevice::FileModificationTime);
}

// Replaces `to` by `from`, which is in the same folder.
bool replaceFile(const QString &from, const QString &to)
{
    if (QFileInfo::exists(to) && !QFile::remove(to))
        return false;
    return QFile::rename(from, to);
}

QHash<QString, BackupFile> readPartial(const QString &path)
{
    QHash<QString, BackupFile> done;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return done;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        // A line cut in the middle is the file being copied at the cut.
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (const auto entry = backupFileFromJson(doc.object()))
            done.insert(entry->path, *entry);
    }
    return done;
}

} // namespace

QString backupKindName(BackupKind kind)
{
    switch (kind) {
    case BackupKind::Full:
        return QStringLiteral("full");
    case BackupKind::Keep:
        return QStringLiteral("keep");
    case BackupKind::Settings:
        return QStringLiteral("settings");
    }
    return QString();
}

std::optional<BackupKind> backupKindFromName(const QString &name)
{
    for (BackupKind kind : {BackupKind::Full, BackupKind::Keep, BackupKind::Settings}) {
        if (backupKindName(kind) == name)
            return kind;
    }
    return std::nullopt;
}

QJsonObject backupFileJson(const BackupFile &file)
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), file.path);
    object.insert(QStringLiteral("size"), double(file.size));
    object.insert(QStringLiteral("modified"), isoUtc(file.modified));
    object.insert(QStringLiteral("sha256"), file.sha256);
    return object;
}

std::optional<BackupFile> backupFileFromJson(const QJsonObject &object)
{
    BackupFile file;
    file.path = object.value(QStringLiteral("path")).toString();
    file.size = qint64(object.value(QStringLiteral("size")).toDouble(-1));
    file.modified = fromIso(object.value(QStringLiteral("modified")).toString());
    file.sha256 = object.value(QStringLiteral("sha256")).toString();
    const QStringList parts = file.path.split(QLatin1Char('/'));
    // A path that leaves the backup is refused: no empty part, no `.` or `..`.
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return std::nullopt;
    }
    if (file.size < 0 || !file.modified.isValid() || file.sha256.size() != 64)
        return std::nullopt;
    return file;
}

QByteArray manifestJson(const BackupManifest &manifest)
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), kBackupManifestFormat);
    object.insert(QStringLiteral("kind"), backupKindName(manifest.kind));
    object.insert(QStringLiteral("serial"), manifest.serial);
    object.insert(QStringLiteral("model"), manifest.model);
    object.insert(QStringLiteral("appVersion"), manifest.appVersion);
    object.insert(QStringLiteral("created"), isoUtc(manifest.created));
    object.insert(QStringLiteral("volumeLabel"), manifest.volumeLabel);
    QJsonArray files;
    for (const BackupFile &file : manifest.files)
        files.append(backupFileJson(file));
    object.insert(QStringLiteral("files"), files);
    object.insert(QStringLiteral("folders"), QJsonArray::fromStringList(manifest.folders));
    if (manifest.firmwarePartition) {
        QJsonObject part;
        part.insert(QStringLiteral("file"), manifest.firmwarePartition->file);
        part.insert(QStringLiteral("size"), double(manifest.firmwarePartition->size));
        part.insert(QStringLiteral("sha256"), manifest.firmwarePartition->sha256);
        object.insert(QStringLiteral("firmwarePartition"), part);
    }
    object.insert(QStringLiteral("complete"), manifest.complete);
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

ManifestRead readManifest(const QString &backupFolder)
{
    ManifestRead read;
    QFile file(QDir(backupFolder).filePath(kManifestFile));
    if (!file.open(QIODevice::ReadOnly)) {
        read.error = QStringLiteral("no manifest");
        return read;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!doc.isObject()) {
        read.error = QStringLiteral("the manifest is not JSON: ") + parse.errorString();
        return read;
    }
    const QJsonObject object = doc.object();
    if (object.value(QStringLiteral("format")).toInt() != kBackupManifestFormat) {
        read.error = QStringLiteral("unknown manifest format");
        return read;
    }
    BackupManifest &m = read.manifest;
    const auto kind = backupKindFromName(object.value(QStringLiteral("kind")).toString());
    if (!kind) {
        read.error = QStringLiteral("unknown backup kind");
        return read;
    }
    m.kind = *kind;
    m.serial = object.value(QStringLiteral("serial")).toString();
    m.model = object.value(QStringLiteral("model")).toString();
    m.appVersion = object.value(QStringLiteral("appVersion")).toString();
    m.created = fromIso(object.value(QStringLiteral("created")).toString());
    m.volumeLabel = object.value(QStringLiteral("volumeLabel")).toString();
    m.complete = object.value(QStringLiteral("complete")).toBool(false);
    for (const QJsonValue &value : object.value(QStringLiteral("files")).toArray()) {
        const auto entry = backupFileFromJson(value.toObject());
        if (!entry) {
            read.error = QStringLiteral("a file of the manifest cannot be read");
            return read;
        }
        m.files << *entry;
    }
    for (const QJsonValue &value : object.value(QStringLiteral("folders")).toArray())
        m.folders << value.toString();
    const QJsonValue part = object.value(QStringLiteral("firmwarePartition"));
    if (part.isObject()) {
        FirmwarePartitionCopy copy;
        copy.file = part.toObject().value(QStringLiteral("file")).toString();
        copy.size = qint64(part.toObject().value(QStringLiteral("size")).toDouble(-1));
        copy.sha256 = part.toObject().value(QStringLiteral("sha256")).toString();
        if (copy.file.isEmpty() || copy.file.contains(QLatin1Char('/')) || copy.size < 0) {
            read.error = QStringLiteral("the firmware partition of the manifest cannot be read");
            return read;
        }
        m.firmwarePartition = copy;
    }
    if (m.serial.isEmpty() || !m.created.isValid()) {
        read.error = QStringLiteral("the manifest has no serial or date");
        return read;
    }
    return read;
}

QString serialFolderName(const QString &serial)
{
    QString name = serial;
    for (QChar &c : name) {
        const bool ascii = c.unicode() < 128;
        if (!(ascii && (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))))
            c = QLatin1Char('_');
    }
    return name;
}

QString dateFolderName(const QDateTime &local)
{
    return local.toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
}

QString datedBackupFolder(const QString &root, const QString &serial, const QDateTime &local)
{
    return QDir(root).filePath(serialFolderName(serial) + QLatin1Char('/') + dateFolderName(local));
}

QString defaultBackupRoot()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documents).filePath(kDefaultBackupFolder);
}

QString backupVolumeFolder(const QString &backupFolder)
{
    return QDir(backupFolder).filePath(kBackupVolumeFolder);
}

BackupPlan planBackup(const QString &volumeRoot, BackupKind kind)
{
    BackupPlan plan;
    if (kind == BackupKind::Full) {
        walk(volumeRoot, QString(), plan);
        return plan;
    }
    const QFileInfoList root = sortedEntries(volumeRoot, &plan.symlinks);
    for (const QFileInfo &info : root) {
        const QString name = info.fileName();
        const bool firmware = info.isDir() && sameName(name, device::kFirmwareFolder);
        if (kind == BackupKind::Keep) {
            if (firmware || (info.isDir() && sameName(name, device::kPlaylistsFolder)))
                addFolder(volumeRoot, name, plan);
            else if (!info.isDir() && isPlaylistFile(name))
                addFile(info, name, plan);
            continue;
        }
        if (!firmware)
            continue;
        // Section 7.3: only the kept files and folders of the firmware folder.
        plan.folders << name;
        const QFileInfoList inside = sortedEntries(info.filePath(), &plan.symlinks);
        for (const QFileInfo &entry : inside) {
            const QString path = name + QLatin1Char('/') + entry.fileName();
            if (entry.isDir() && inList(device::kKeptFirmwareFolders, entry.fileName()))
                addFolder(volumeRoot, path, plan);
            else if (!entry.isDir() && inList(device::kKeptFirmwareFiles, entry.fileName()))
                addFile(entry, path, plan);
        }
    }
    return plan;
}

bool isVerifiedBackup(const QString &backupFolder, const QString &serial, QString *why)
{
    auto fail = [why](const QString &reason) {
        if (why)
            *why = reason;
        return false;
    };
    const ManifestRead read = readManifest(backupFolder);
    if (!read.error.isEmpty())
        return fail(read.error);
    const BackupManifest &m = read.manifest;
    if (!m.complete)
        return fail(QStringLiteral("the manifest is not complete"));
    if (m.serial != serial)
        return fail(QStringLiteral("the backup is of another iPod"));
    const QDir volume(backupVolumeFolder(backupFolder));
    for (const BackupFile &file : m.files) {
        const QFileInfo info(volume.filePath(file.path));
        if (!info.isFile() || info.size() != file.size)
            return fail(QStringLiteral("a file of the backup is missing or has another size"));
    }
    if (m.firmwarePartition) {
        const QFileInfo info(QDir(backupFolder).filePath(m.firmwarePartition->file));
        if (!info.isFile() || info.size() != m.firmwarePartition->size)
            return fail(QStringLiteral("the copy of the firmware partition is missing"));
    }
    return true;
}

QString checkedFirmwarePartition(const QString &backupFolder, const QString &serial, QString *why)
{
    if (!isVerifiedBackup(backupFolder, serial, why))
        return QString();
    const BackupManifest m = readManifest(backupFolder).manifest;
    if (!m.firmwarePartition) {
        if (why)
            *why = QStringLiteral("the backup holds no firmware partition");
        return QString();
    }
    const QString path = QDir(backupFolder).filePath(m.firmwarePartition->file);
    if (fileSha256(path) != m.firmwarePartition->sha256) {
        if (why)
            *why = QStringLiteral("the copy of the firmware partition is damaged");
        return QString();
    }
    return path;
}

HelperFirmwarePartition::HelperFirmwarePartition(helper::HelperClient &client,
                                                 const QJsonObject &target, const QString &port)
    : m_client(client), m_target(target), m_port(port)
{
}

HelperAnswer HelperFirmwarePartition::call(const QString &cmd, const QJsonObject &args)
{
    QMutex mutex;
    QWaitCondition arrived;
    QHash<int, QJsonObject> results;
    int id = -1;
    QEventLoop loop;
    const bool sameThread = QThread::currentThread() == m_client.thread();

    const QMetaObject::Connection connection = QObject::connect(
        &m_client, &helper::HelperClient::finished, &m_client,
        [&](int got, const QJsonObject &result) {
            QMutexLocker locker(&mutex);
            results.insert(got, result);
            arrived.wakeAll();
            if (sameThread && got == id)
                loop.quit();
        },
        Qt::DirectConnection);
    auto send = [&] {
        const int sent = m_client.request(cmd, args, m_port);
        QMutexLocker locker(&mutex);
        id = sent;
    };
    QJsonObject result;
    if (sameThread) {
        send();
        if (!results.contains(id))
            loop.exec();
        result = results.value(id);
    } else {
        QMetaObject::invokeMethod(&m_client, send, Qt::BlockingQueuedConnection);
        QMutexLocker locker(&mutex);
        while (!results.contains(id))
            arrived.wait(&mutex);
        result = results.value(id);
    }
    QObject::disconnect(connection);

    HelperAnswer answer;
    if (!result.value(QStringLiteral("ok")).toBool()) {
        answer.code = result.value(QStringLiteral("error")).toString();
        answer.detail = result.value(QStringLiteral("detail")).toString();
        if (answer.code.isEmpty())
            answer.code = QString(helper::err::badRequest);
    }
    answer.data = result.value(QStringLiteral("data")).toObject();
    return answer;
}

HelperAnswer HelperFirmwarePartition::scanToken(QString *token)
{
    QJsonObject args;
    args.insert(QStringLiteral("target"), m_target);
    HelperAnswer answer = call(QString(helper::cmd::readState), args);
    *token = answer.data.value(QStringLiteral("scanToken")).toString();
    return answer;
}

HelperAnswer HelperFirmwarePartition::backup(const QString &dataDir)
{
    QString token;
    const HelperAnswer state = scanToken(&token);
    if (!state.ok())
        return state;
    QJsonObject args;
    args.insert(QStringLiteral("target"), m_target);
    args.insert(QStringLiteral("scanToken"), token);
    args.insert(QStringLiteral("dataDir"), dataDir);
    return call(QString(helper::cmd::backupFirmware), args);
}

HelperAnswer HelperFirmwarePartition::restore(const QString &file, const QString &dataDir)
{
    QString token;
    const HelperAnswer state = scanToken(&token);
    if (!state.ok())
        return state;
    QJsonObject args;
    args.insert(QStringLiteral("target"), m_target);
    args.insert(QStringLiteral("scanToken"), token);
    args.insert(QStringLiteral("backupFile"), file);
    args.insert(QStringLiteral("dataDir"), dataDir);
    return call(QString(helper::cmd::restoreBackup), args);
}

StepOutcome BackupStep::run(StepContext &context)
{
    m_report = BackupReport();
    const bool partition = context.shape().family == Family::Partition;
    FirmwarePartition *firmware = m_sources.firmwarePartition();
    if (partition && firmware == nullptr)
        return StepOutcome::failed(QString(error::badFlow),
                                   QStringLiteral("no access to the firmware partition"));

    // Section 3.3: a resume runs again in the same dated folder.
    QString folder = context.backupFolder();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        folder = datedBackupFolder(m_sources.backupRoot(), context.serial(), m_sources.now());
        if (!QDir().mkpath(folder))
            return StepOutcome::failed(QString(error::backupFullDisk),
                                       QStringLiteral("the backup folder cannot be made"));
        context.setBackupFolder(folder);
    }
    m_report.folder = folder;
    const QDir backup(folder);
    const QDir volume(backupVolumeFolder(folder));
    const QDir source(m_sources.volumeRoot());
    const QString partialPath = backup.filePath(kManifestPartialFile);

    const BackupPlan plan = planBackup(m_sources.volumeRoot(), m_kind);
    m_report.symlinks = plan.symlinks;
    const QHash<QString, BackupFile> done = readPartial(partialPath);

    // Section 5.2: the space, less what a cut run already copied.
    qint64 doneBytes = 0;
    for (const BackupFile &file : done)
        doneBytes += file.size;
    qint64 need = (m_kind == BackupKind::Full ? m_sources.volumeUsedBytes() : plan.bytes) -
                  doneBytes + kBackupMargin;
    if (partition)
        need += m_sources.firmwarePartitionBytes();
    const qint64 free = m_sources.freeBytes(folder);
    if (free >= 0 && free < need)
        return StepOutcome::failed(QString(error::backupFullDisk), QString::number(need));

    if (!QDir().mkpath(volume.path()))
        return StepOutcome::failed(QString(error::backupFullDisk),
                                   QStringLiteral("the backup folder cannot be made"));
    QStringList refusedFolders;
    for (const QString &path : plan.folders) {
        if (!volume.mkpath(path))
            refusedFolders << path;
    }

    QFile partial(partialPath);
    if (!partial.open(QIODevice::WriteOnly | QIODevice::Append))
        return StepOutcome::failed(writeFailureCode(partial.errorString(), folder),
                                   partial.errorString());

    const installer::FileFault fault = m_sources.fault();
    const auto stop = [&context] { return context.stopRequested(); };
    qint64 progressDone = 0;
    const auto advance = [&](qint64 bytes) {
        progressDone += bytes;
        context.progress(kStageCopy, progressDone, plan.bytes);
    };
    int changed = 0;
    QList<BackupFile> files;

    for (const QString &path : plan.files) {
        const QString target = volume.filePath(path);
        const QString part = target + kPartSuffix;
        const auto previous = done.constFind(path);
        if (previous != done.cend() && QFileInfo(target).isFile() &&
            QFileInfo(target).size() == previous->size) {
            files << *previous;
            ++m_report.skipped;
            advance(previous->size);
            continue;
        }
        bool placed = false;
        for (int attempt = 0; attempt < 2 && !placed; ++attempt) {
            const QFileInfo before(source.filePath(path));
            const qint64 size = before.size();
            const QDateTime modified = before.lastModified();
            const qint64 startDone = progressDone;
            const PieceCopy copy = copyPieces(source.filePath(path), part, fault, stop, advance);
            if (copy.failure == PieceCopy::Failure::Cancelled)
                return StepOutcome::cancelled();
            if (copy.failure == PieceCopy::Failure::Read) {
                m_report.unread << path;
                progressDone = startDone + size;
                break;
            }
            if (copy.failure == PieceCopy::Failure::Open) {
                if (writeFailureCode(copy.detail, folder) == error::backupFullDisk)
                    return StepOutcome::failed(QString(error::backupFullDisk), copy.detail);
                m_report.refused << path;
                progressDone = startDone + size;
                break;
            }
            if (copy.failure == PieceCopy::Failure::Write)
                return StepOutcome::failed(writeFailureCode(copy.detail, folder), copy.detail);
            // Section 5.5 step 3: read back from the disk of the computer.
            if (fileSha256(part) != copy.sha256) {
                QFile::remove(part);
                return StepOutcome::failed(QString(error::backupVerifyFailed));
            }
            // Step 4: the source changed during the copy.
            const QFileInfo after(source.filePath(path));
            if (after.size() != size || after.lastModified() != modified || copy.bytes != size) {
                QFile::remove(part);
                progressDone = startDone;
                if (attempt == 0)
                    continue;
                ++changed;
                break;
            }
            if (!setModified(part, modified) || !replaceFile(part, target)) {
                QFile::remove(part);
                return StepOutcome::failed(QString(error::backupVerifyFailed),
                                           QStringLiteral("the copy cannot be put in place"));
            }
            BackupFile entry{path, size, modified, copy.sha256};
            const QByteArray line =
                QJsonDocument(backupFileJson(entry)).toJson(QJsonDocument::Compact) + '\n';
            if (partial.write(line) != line.size() || !partial.flush())
                return StepOutcome::failed(writeFailureCode(partial.errorString(), folder),
                                           partial.errorString());
            files << entry;
            ++m_report.copied;
            placed = true;
        }
        if (changed > 0)
            return StepOutcome::failed(QString(error::sourceChanged));
    }
    partial.close();

    if (!m_report.unread.isEmpty())
        return StepOutcome::failed(QString(error::backupReadFailed),
                                   QString::number(m_report.unread.size()));
    m_report.refused << refusedFolders;
    if (!m_report.refused.isEmpty())
        return StepOutcome::failed(QString(error::backupNameRefused),
                                   QString::number(m_report.refused.size()));

    BackupManifest manifest;
    manifest.kind = m_kind;
    manifest.serial = context.serial();
    manifest.model = context.model();
    manifest.appVersion = m_sources.appVersion();
    manifest.created = QDateTime::currentDateTimeUtc();
    manifest.volumeLabel = m_sources.volumeLabel();
    manifest.files = files;
    manifest.folders = plan.folders;

    // Then the firmware partition, by the helper, read again here.
    if (partition) {
        if (context.stopRequested())
            return StepOutcome::cancelled();
        context.progress(kStageFirmware, 0, m_sources.firmwarePartitionBytes());
        const HelperAnswer answer = firmware->backup(folder);
        if (!answer.ok())
            return StepOutcome::failed(answer.code, answer.detail);
        const QFileInfo copy(answer.data.value(QStringLiteral("path")).toString());
        if (QDir::cleanPath(copy.absolutePath()) != QDir::cleanPath(backup.absolutePath()))
            return StepOutcome::failed(QString(error::backupVerifyFailed),
                                       QStringLiteral("the helper put the copy elsewhere"));
        const QString sha = fileSha256(copy.filePath());
        if (sha.isEmpty())
            return StepOutcome::failed(QString(error::backupVerifyFailed),
                                       QStringLiteral("the copy of the partition cannot be read"));
        manifest.firmwarePartition = FirmwarePartitionCopy{copy.fileName(), copy.size(), sha};
        context.progress(kStageFirmware, copy.size(), copy.size());
    }

    // Section 5.4: written last, in one move.
    QSaveFile out(backup.filePath(kManifestFile));
    if (!out.open(QIODevice::WriteOnly) || out.write(manifestJson(manifest)) < 0 || !out.commit())
        return StepOutcome::failed(writeFailureCode(out.errorString(), folder), out.errorString());
    QFile::remove(partialPath);

    context.progress(kStageCheck, 0, 0);
    QString why;
    if (!isVerifiedBackup(folder, context.serial(), &why))
        return StepOutcome::failed(QString(error::backupVerifyFailed), why);
    context.setFact(QStringLiteral("manifest"), backup.filePath(kManifestFile));
    return StepOutcome::done();
}

RestoreResult restoreFromBackup(const QString &backupFolder, const QString &volumeRoot,
                                const QString &serial, FirmwarePartition *firmware,
                                const RestoreOptions &options)
{
    RestoreResult result;
    auto fail = [&result](const QString &code, const QString &detail = QString()) {
        result.code = code;
        result.detail = detail;
        return result;
    };
    const ManifestRead read = readManifest(backupFolder);
    if (!read.error.isEmpty())
        return fail(QString(error::backupNotVerified), read.error);
    const BackupManifest &m = read.manifest;
    if (!m.complete || m.serial != serial)
        return fail(QString(error::backupNotVerified),
                    QStringLiteral("the backup is not complete or is of another iPod"));
    if (m.kind == BackupKind::Full)
        return fail(QString(error::badFlow), QStringLiteral("a full backup is put back by pieces"));
    if (options.firmwarePartition && (!m.firmwarePartition || firmware == nullptr))
        return fail(QString(error::backupNotVerified),
                    QStringLiteral("the backup holds no firmware partition"));

    // Section 5.7: every sha256 first. A difference writes nothing.
    const QDir volume(backupVolumeFolder(backupFolder));
    int damaged = 0;
    for (const BackupFile &file : m.files) {
        const QString path = volume.filePath(file.path);
        if (QFileInfo(path).size() != file.size || fileSha256(path) != file.sha256)
            ++damaged;
    }
    const QString partitionPath =
        m.firmwarePartition ? QDir(backupFolder).filePath(m.firmwarePartition->file) : QString();
    if (options.firmwarePartition && fileSha256(partitionPath) != m.firmwarePartition->sha256)
        ++damaged;
    if (damaged > 0)
        return fail(QString(error::backupDamaged), QStringLiteral("0"));

    const QDir target(volumeRoot);
    for (const QString &folder : m.folders) {
        if (!target.mkpath(folder))
            return fail(QString(error::backupNameRefused), folder);
    }
    for (const BackupFile &file : m.files) {
        const QString to = target.filePath(file.path);
        const QString part = to + kPartSuffix;
        const PieceCopy copy =
            copyPieces(volume.filePath(file.path), part, options.fault, options.stop, {});
        if (copy.failure == PieceCopy::Failure::Cancelled)
            return fail(QString(error::cancelled), QString::number(result.restored));
        if (copy.failure != PieceCopy::Failure::None || copy.sha256 != file.sha256 ||
            fileSha256(part) != file.sha256) {
            QFile::remove(part);
            return fail(QString(error::backupDamaged), QString::number(result.restored));
        }
        setModified(part, file.modified);
        if (!replaceFile(part, to)) {
            QFile::remove(part);
            return fail(QString(error::backupDamaged), QString::number(result.restored));
        }
        ++result.restored;
    }
    if (options.firmwarePartition) {
        const HelperAnswer answer = firmware->restore(partitionPath, backupFolder);
        if (!answer.ok())
            return fail(answer.code, answer.detail);
    }
    return result;
}

} // namespace tacetos::wizard
