// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "progress.h"

#include "wizardconstants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace tacetos::wizard {

namespace {

std::optional<StepStatus> statusFromName(const QString &name)
{
    if (name == QLatin1String("running"))
        return StepStatus::Running;
    if (name == QLatin1String("waiting"))
        return StepStatus::Waiting;
    if (name == QLatin1String("failed"))
        return StepStatus::Failed;
    if (name == QLatin1String("next"))
        return StepStatus::Next;
    return std::nullopt;
}

QString isoTime(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODate);
}

bool isText(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).isString();
}

} // namespace

QString statusName(StepStatus status)
{
    switch (status) {
    case StepStatus::Running:
        return QStringLiteral("running");
    case StepStatus::Waiting:
        return QStringLiteral("waiting");
    case StepStatus::Failed:
        return QStringLiteral("failed");
    case StepStatus::Next:
        break;
    }
    return QStringLiteral("next");
}

QJsonObject toJson(const Progress &progress)
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), kProgressFormat);
    object.insert(QStringLiteral("appVersion"), progress.appVersion);
    object.insert(QStringLiteral("serial"), progress.serial);
    object.insert(QStringLiteral("model"), progress.model);
    object.insert(QStringLiteral("family"), familyName(progress.shape.family));
    object.insert(QStringLiteral("flow"), kindName(progress.shape.kind));
    if (progress.shape.kind == FlowKind::Install)
        object.insert(QStringLiteral("path"), pathName(progress.shape.path));
    object.insert(QStringLiteral("macFormat"), progress.shape.macFormat);
    object.insert(QStringLiteral("bootloaderFirst"), progress.shape.bootloaderFirst);
    object.insert(QStringLiteral("steps"), QJsonArray::fromStringList(progress.steps));
    object.insert(QStringLiteral("current"), progress.current);
    object.insert(QStringLiteral("status"), statusName(progress.status));
    if (progress.status == StepStatus::Failed)
        object.insert(QStringLiteral("error"), progress.error);
    if (!progress.backup.isEmpty())
        object.insert(QStringLiteral("backup"), progress.backup);
    if (progress.shape.kind == FlowKind::Install) {
        QJsonObject package;
        package.insert(QStringLiteral("manifest"), progress.packageManifest);
        package.insert(QStringLiteral("sha256"), progress.packageSha256);
        object.insert(QStringLiteral("package"), package);
    }
    object.insert(QStringLiteral("facts"), progress.facts);
    object.insert(QStringLiteral("started"), isoTime(progress.started));
    object.insert(QStringLiteral("updated"), isoTime(progress.updated));
    return object;
}

QString parseProgress(const QByteArray &bytes, const QString &serial, Progress *out)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("not a JSON object");
    const QJsonObject object = document.object();

    const QJsonValue format = object.value(QStringLiteral("format"));
    if (!format.isDouble())
        return QStringLiteral("no format");
    if (format.toDouble() != kProgressFormat)
        return QStringLiteral("format %1").arg(format.toDouble());

    for (const char *key :
         {"appVersion", "serial", "model", "family", "flow", "status", "started", "updated"}) {
        if (!isText(object, key))
            return QStringLiteral("key %1 missing or not a text").arg(QLatin1String(key));
    }
    if (!object.value(QStringLiteral("macFormat")).isBool())
        return QStringLiteral("key macFormat missing or not a boolean");
    // Absent in a file written before the key existed: false.
    if (object.contains(QStringLiteral("bootloaderFirst")) &&
        !object.value(QStringLiteral("bootloaderFirst")).isBool())
        return QStringLiteral("key bootloaderFirst not a boolean");
    if (!object.value(QStringLiteral("steps")).isArray())
        return QStringLiteral("key steps missing or not a list");
    if (!object.value(QStringLiteral("current")).isDouble())
        return QStringLiteral("key current missing or not a number");
    if (!object.value(QStringLiteral("facts")).isObject())
        return QStringLiteral("key facts missing or not an object");

    Progress progress;
    progress.appVersion = object.value(QStringLiteral("appVersion")).toString();
    progress.serial = object.value(QStringLiteral("serial")).toString();
    progress.model = object.value(QStringLiteral("model")).toString();
    if (progress.serial != serial)
        return QStringLiteral("serial of another iPod");

    const auto family = familyFromName(object.value(QStringLiteral("family")).toString());
    const auto kind = kindFromName(object.value(QStringLiteral("flow")).toString());
    if (!family || !kind)
        return QStringLiteral("unknown family or flow");
    progress.shape.family = *family;
    progress.shape.kind = *kind;
    progress.shape.macFormat = object.value(QStringLiteral("macFormat")).toBool();
    progress.shape.bootloaderFirst = object.value(QStringLiteral("bootloaderFirst")).toBool();
    if (*kind == FlowKind::Install) {
        const auto path = pathFromName(object.value(QStringLiteral("path")).toString());
        if (!path)
            return QStringLiteral("unknown path");
        progress.shape.path = *path;
        const QJsonValue package = object.value(QStringLiteral("package"));
        if (!package.isObject() ||
            !package.toObject().value(QStringLiteral("manifest")).isString() ||
            !package.toObject().value(QStringLiteral("sha256")).isString())
            return QStringLiteral("key package missing or not complete");
        progress.packageManifest = package.toObject().value(QStringLiteral("manifest")).toString();
        progress.packageSha256 = package.toObject().value(QStringLiteral("sha256")).toString();
    } else if (object.contains(QStringLiteral("path"))) {
        return QStringLiteral("a path for an uninstall");
    }

    for (const QJsonValue &name : object.value(QStringLiteral("steps")).toArray()) {
        if (!name.isString())
            return QStringLiteral("a step that is not a text");
        progress.steps << name.toString();
    }
    if (progress.steps != stepsFor(progress.shape))
        return QStringLiteral("steps are not the list of this flow");

    const double current = object.value(QStringLiteral("current")).toDouble();
    if (current != static_cast<int>(current) || current < 0 || current >= progress.steps.size())
        return QStringLiteral("current out of the list");
    progress.current = static_cast<int>(current);

    const auto status = statusFromName(object.value(QStringLiteral("status")).toString());
    if (!status)
        return QStringLiteral("unknown status");
    progress.status = *status;
    if (progress.status == StepStatus::Failed) {
        if (!isText(object, "error"))
            return QStringLiteral("a failed step with no error");
        progress.error = object.value(QStringLiteral("error")).toString();
    }

    progress.started =
        QDateTime::fromString(object.value(QStringLiteral("started")).toString(), Qt::ISODate);
    progress.updated =
        QDateTime::fromString(object.value(QStringLiteral("updated")).toString(), Qt::ISODate);
    if (!progress.started.isValid() || !progress.updated.isValid())
        return QStringLiteral("a time that cannot be read");

    if (object.contains(QStringLiteral("backup"))) {
        if (!isText(object, "backup"))
            return QStringLiteral("key backup not a text");
        progress.backup = object.value(QStringLiteral("backup")).toString();
    }
    // A step after a backup step needs the folder of that backup.
    for (int i = 0; i < progress.current; ++i) {
        if (progress.steps.at(i).startsWith(QLatin1String("backup-")) &&
            (progress.backup.isEmpty() || !QFileInfo(progress.backup).isDir()))
            return QStringLiteral("the backup folder is gone");
    }

    progress.facts = object.value(QStringLiteral("facts")).toObject();
    *out = progress;
    return QString();
}

ProgressStore::ProgressStore(const QString &dataDir) : m_dataDir(dataDir) {}

QString ProgressStore::safeSerial(const QString &serial)
{
    QString safe = serial;
    for (QChar &c : safe) {
        const bool plain = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                           (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                           (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                           c == QLatin1Char('-') || c == QLatin1Char('_');
        if (!plain)
            c = QLatin1Char('_');
    }
    return safe;
}

QString ProgressStore::folder() const
{
    return QDir(m_dataDir).filePath(QString(kProgressFolder));
}

QString ProgressStore::filePath(const QString &serial) const
{
    return QDir(folder()).filePath(safeSerial(serial) + QStringLiteral(".json"));
}

QString ProgressStore::lockPath(const QString &serial) const
{
    return QDir(folder()).filePath(safeSerial(serial) + QString(kLockSuffix));
}

bool ProgressStore::exists(const QString &serial) const
{
    return QFileInfo::exists(filePath(serial));
}

bool ProgressStore::save(const Progress &progress, QString *error) const
{
    if (!QDir().mkpath(folder())) {
        if (error)
            *error = QStringLiteral("cannot create the folder");
        return false;
    }
    QSaveFile file(filePath(progress.serial));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson(progress)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

ProgressRead ProgressStore::load(const QString &serial) const
{
    ProgressRead read;
    const QString path = filePath(serial);
    QFile file(path);
    if (!file.exists())
        return read;
    if (!file.open(QIODevice::ReadOnly)) {
        read.outcome = ProgressRead::Outcome::Refused;
        read.reason = QStringLiteral("cannot be read: ") + file.errorString();
        return read;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    read.reason = parseProgress(bytes, serial, &read.progress);
    if (read.reason.isEmpty()) {
        read.outcome = ProgressRead::Outcome::Ok;
        return read;
    }
    read.outcome = ProgressRead::Outcome::Refused;
    read.progress = Progress();
    const QString rejected = path + QString(kRejectedSuffix);
    QFile::remove(rejected);
    QFile::rename(path, rejected);
    return read;
}

bool ProgressStore::remove(const QString &serial) const
{
    return QFile::remove(filePath(serial)) || !exists(serial);
}

} // namespace tacetos::wizard
