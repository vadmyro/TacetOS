// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "download.h"

#include "diagnose.h"
#include "index.h"
#include "reader.h"
#include "version.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <optional>

namespace tacetos::wizard {

namespace {

const QString kStage = QStringLiteral("download");

// The reads of one run of the step, made in the thread of the runner: each one
// waits in its own event loop, and ends early when the user stops.
class Fetcher
{
public:
    Fetcher(StepContext &context, qint64 timeoutMs) : m_context(context), m_reader(&m_network)
    {
        m_reader.setTimeout(timeoutMs);
    }

    // Nothing when the user stopped first.
    std::optional<update::ChannelRead> read(const update::ChannelAddress &address,
                                            const QString &name, qint64 limit)
    {
        std::optional<update::ChannelRead> result;
        QEventLoop loop;
        QObject::connect(&m_reader, &update::ChannelReader::finished, &loop,
                         [&](const update::ChannelRead &read) {
                             result = read;
                             loop.quit();
                         });
        QTimer watch;
        QObject::connect(&watch, &QTimer::timeout, &loop, [&] {
            if (m_context.stopRequested())
                loop.quit();
        });
        watch.start(100);
        m_reader.read(address, name, limit);
        loop.exec();
        if (!result)
            m_reader.cancel();
        return result;
    }

private:
    StepContext &m_context;
    QNetworkAccessManager m_network;
    update::ChannelReader m_reader;
};

struct Listed
{
    QString name;
    qint64 size = -1;
    QString sha256;
};

QString sha256Of(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// The sha256 of a file, empty when it cannot be read.
QString sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

bool isRight(const QString &path, qint64 size, const QString &sha256)
{
    const QFileInfo info(path);
    return info.isFile() && info.size() == size && sha256OfFile(path) == sha256;
}

// A name the channel gives: no folder part, so that nothing is written outside
// the folder of the package.
bool plainName(const QString &name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) &&
           !name.contains(QLatin1Char('\\')) && name != QLatin1String(".") &&
           name != QLatin1String("..");
}

bool isSha256(const QString &text)
{
    if (text.size() != 64)
        return false;
    for (const QChar c : text) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
              (c >= QLatin1Char('a') && c <= QLatin1Char('f'))))
            return false;
    }
    return true;
}

// The two files of a manifest, or an empty list when it cannot be used.
QList<Listed> listedFiles(const QByteArray &bytes, const QString &version, QString *why)
{
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    const QJsonObject manifest = doc.object();
    if (!doc.isObject()) {
        *why = QStringLiteral("the manifest is not a JSON object");
        return {};
    }
    if (manifest.value(QStringLiteral("version")).toString() != version) {
        *why = QStringLiteral("the manifest is not of version %1").arg(version);
        return {};
    }
    const QJsonArray list = manifest.value(QStringLiteral("files")).toArray();
    QList<Listed> files;
    for (const QJsonValue &value : list) {
        const QJsonObject entry = value.toObject();
        Listed file;
        file.name = entry.value(QStringLiteral("name")).toString();
        const QJsonValue size = entry.value(QStringLiteral("size"));
        file.size = size.isDouble() ? qint64(size.toDouble()) : -1;
        file.sha256 = entry.value(QStringLiteral("sha256")).toString();
        if (!plainName(file.name) || file.size < 0 || !isSha256(file.sha256)) {
            *why = QStringLiteral("a file of the manifest has no name, size or sha256");
            return {};
        }
        files.append(file);
    }
    if (files.size() != 2) {
        *why = QStringLiteral("the manifest does not list two files");
        return {};
    }
    return files;
}

class Download
{
public:
    Download(const DownloadSources &sources, StepContext &context)
        : m_sources(sources), m_context(context), m_fetch(context, sources.channelTimeoutMs()),
          m_address(sources.channel())
    {
    }

    StepOutcome run();

private:
    // Reads `name` from the channel, at most `limit` bytes; `outcome` is set when
    // it did not come.
    std::optional<QByteArray> fetch(const QString &name, qint64 limit, StepOutcome *outcome);
    // Writes `bytes` as `<path>.part`, checks it, then gives it its name.
    std::optional<StepOutcome> put(const QString &path, const QByteArray &bytes, qint64 size,
                                   const QString &sha256);

    const DownloadSources &m_sources;
    StepContext &m_context;
    Fetcher m_fetch;
    update::ChannelAddress m_address;
};

std::optional<QByteArray> Download::fetch(const QString &name, qint64 limit, StepOutcome *outcome)
{
    const std::optional<update::ChannelRead> read = m_fetch.read(m_address, name, limit);
    if (!read) {
        *outcome = StepOutcome::cancelled();
        return std::nullopt;
    }
    switch (read->outcome) {
    case update::ChannelRead::Outcome::Read:
        return read->data;
    case update::ChannelRead::Outcome::Missing:
        *outcome = StepOutcome::failed(QString(error::updateGone), read->reason);
        return std::nullopt;
    case update::ChannelRead::Outcome::Failed:
        break;
    }
    *outcome = StepOutcome::failed(
        QString(read->overLimit ? error::downloadDamaged : error::downloadFailed),
        name + QStringLiteral(": ") + read->reason);
    return std::nullopt;
}

std::optional<StepOutcome> Download::put(const QString &path, const QByteArray &bytes, qint64 size,
                                         const QString &sha256)
{
    const QString name = QFileInfo(path).fileName();
    if (bytes.size() != size || sha256Of(bytes) != sha256)
        return StepOutcome::failed(QString(error::downloadDamaged),
                                   name + QStringLiteral(": not the size or sha256 given for it"));
    const QString part = path + QString(kPartSuffix);
    QFile file(part);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
        const QString why = file.errorString();
        file.close();
        QFile::remove(part);
        return StepOutcome::failed(QString(error::downloadFailed),
                                   part + QStringLiteral(": ") + why);
    }
    file.close();
    // Read back from the disk: what counts is what was written.
    if (!isRight(part, size, sha256)) {
        QFile::remove(part);
        return StepOutcome::failed(QString(error::downloadDamaged),
                                   name + QStringLiteral(": the file written is not the one read"));
    }
    QFile::remove(path);
    if (!QFile::rename(part, path)) {
        QFile::remove(part);
        return StepOutcome::failed(QString(error::downloadFailed),
                                   QStringLiteral("%1 cannot take its name").arg(part));
    }
    return std::nullopt;
}

StepOutcome Download::run()
{
    StepOutcome outcome;

    // 1. The index again, and the model of the iPod in it.
    m_context.progress(kStage, 0, 0);
    const std::optional<QByteArray> indexBytes =
        fetch(QString(device::kUpdateIndexFile), device::kIndexMaxBytes, &outcome);
    if (!indexBytes)
        return outcome;
    const update::IndexReading reading = update::parseIndex(*indexBytes);
    if (!reading.ok)
        return StepOutcome::failed(QString(error::updateGone),
                                   QStringLiteral("the index is refused: ") + reading.error);
    const QString target = m_sources.target();
    const update::IndexPackage *entry = reading.index.package(target);
    if (!entry)
        return StepOutcome::failed(QString(error::updateGone),
                                   QStringLiteral("the index has no package for ") + target);
    const QString version = reading.index.version;
    QString why;
    if (!update::isNewer(version, m_sources.ipodVersion(), &why))
        return StepOutcome::failed(QString(error::updateGone),
                                   why.isEmpty()
                                       ? QStringLiteral("%1 is not newer than %2 on the iPod")
                                             .arg(version, m_sources.ipodVersion())
                                       : why);
    if (!plainName(entry->manifest))
        return StepOutcome::failed(QString(error::downloadDamaged),
                                   QStringLiteral("the index gives a manifest in a folder"));

    const QString folder = downloadFolder(m_sources.updatesFolder(), version, target);
    if (!QDir().mkpath(folder))
        return StepOutcome::failed(QString(error::downloadFailed),
                                   QStringLiteral("cannot create ") + folder);

    // 4. A file already there, right, is kept; a `.part` left by a cut starts again.
    const QString manifestPath = QDir(folder).filePath(entry->manifest);
    QFile::remove(manifestPath + QString(kPartSuffix));
    const bool manifestThere = isRight(manifestPath, entry->size, entry->sha256);
    QByteArray manifest;
    if (manifestThere) {
        QFile file(manifestPath);
        if (file.open(QIODevice::ReadOnly))
            manifest = file.readAll();
    } else {
        const std::optional<QByteArray> read = fetch(entry->manifest, entry->size, &outcome);
        if (!read)
            return outcome;
        manifest = *read;
        if (manifest.size() != entry->size || sha256Of(manifest) != entry->sha256)
            return StepOutcome::failed(QString(error::downloadDamaged),
                                       entry->manifest +
                                           QStringLiteral(": not the size or sha256 of the index"));
    }
    const QList<Listed> files = listedFiles(manifest, version, &why);
    if (files.isEmpty())
        return StepOutcome::failed(QString(error::downloadDamaged), why);

    // 2. The space: the manifest and its two files, plus the margin.
    qint64 need = entry->size + device::kDownloadMarginBytes;
    qint64 total = 0;
    for (const Listed &file : files)
        total += file.size;
    need += total;
    const qint64 free = m_sources.freeBytes(folder);
    if (free >= 0 && free < need)
        return StepOutcome::failed(
            QString(error::downloadSpace),
            QStringLiteral("%1 needed, %2 free").arg(sizeText(need), sizeText(free)));

    // 3. The manifest, then its two files.
    if (!manifestThere) {
        if (const auto failed = put(manifestPath, manifest, entry->size, entry->sha256))
            return *failed;
    }
    qint64 done = 0;
    m_context.progress(kStage, done, total);
    for (const Listed &file : files) {
        if (m_context.stopRequested())
            return StepOutcome::cancelled();
        const QString path = QDir(folder).filePath(file.name);
        QFile::remove(path + QString(kPartSuffix));
        if (!isRight(path, file.size, file.sha256)) {
            const std::optional<QByteArray> read = fetch(file.name, file.size, &outcome);
            if (!read)
                return outcome;
            if (const auto failed = put(path, *read, file.size, file.sha256))
                return *failed;
        }
        done += file.size;
        m_context.progress(kStage, done, total);
    }

    // 5. The package of the flow, as a manifest chosen by hand.
    m_context.setPackage(manifestPath, entry->sha256, version);
    return StepOutcome::done();
}

} // namespace

StepOutcome DownloadStep::run(StepContext &context)
{
    if (context.stopRequested())
        return StepOutcome::cancelled();
    Download download(m_sources, context);
    return download.run();
}

QString downloadFolder(const QString &updatesFolder, const QString &version, const QString &target)
{
    return QDir(QDir(updatesFolder).filePath(version)).filePath(target);
}

void removeOlderUpdates(const QString &updatesFolder, const QString &keep)
{
    const QDir folder(updatesFolder);
    for (const QString &name : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name != keep)
            QDir(folder.filePath(name)).removeRecursively();
    }
}

} // namespace tacetos::wizard
