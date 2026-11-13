// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "record.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace tacetos::installer {

namespace {

constexpr int kRecordFormat = 1;

// Names written by this code only: a record is read back by the same code.
QJsonObject toJson(const InstallRecord &r)
{
    QJsonObject o;
    o[QLatin1String("format")] = kRecordFormat;
    o[QLatin1String("date")] = r.date.toString(Qt::ISODate);
    o[QLatin1String("operation")] = r.operation;
    o[QLatin1String("target")] = r.target;
    o[QLatin1String("serial")] = r.serial;
    o[QLatin1String("diskBytes")] = double(r.diskBytes);
    o[QLatin1String("osEntryBefore")] = QString::fromLatin1(r.osEntryBefore.toHex());
    o[QLatin1String("bootloaderVersion")] = r.bootloaderVersion;
    o[QLatin1String("bootloaderChecksum")] = double(r.bootloaderChecksum);
    o[QLatin1String("delta")] = double(r.delta);
    o[QLatin1String("backupFile")] = r.backupFile;
    return o;
}

} // namespace

QString recordFileName(const InstallRecord &r)
{
    return QStringLiteral("record-%1-%2-%3-%4.json")
        .arg(r.operation, r.target, r.date.toString(QStringLiteral("yyyyMMdd-HHmmss")),
             QString::number(r.diskBytes));
}

bool saveRecord(const InstallRecord &record, const QString &dir, QString &path, QString &why,
                const FileFault &fault)
{
    if (dir.isEmpty() || !QDir().mkpath(QDir(dir).absolutePath())) {
        why = QStringLiteral("the folder of the record cannot be created");
        return false;
    }
    const QDir folder(dir);
    const QString name = recordFileName(record);
    QString candidate = folder.filePath(name);
    for (int n = 2; QFile::exists(candidate); ++n) {
        candidate =
            folder.filePath(QStringLiteral("%1-%2.json").arg(name.left(name.size() - 5)).arg(n));
    }
    QFile f(candidate);
    if (!f.open(QIODevice::WriteOnly)) {
        why = f.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(toJson(record)).toJson(QJsonDocument::Indented);
    const QString injected = fault ? fault(candidate, 0) : QString();
    if (!injected.isEmpty() || f.write(bytes) != bytes.size() || !f.flush()) {
        why = injected.isEmpty() ? f.errorString() : injected;
        f.close();
        QFile::remove(candidate);
        return false;
    }
    f.close();
    path = candidate;
    return true;
}

std::optional<InstallRecord> loadRecord(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    if (o.value(QLatin1String("format")).toInt() != kRecordFormat)
        return std::nullopt;
    InstallRecord r;
    r.date = QDateTime::fromString(o.value(QLatin1String("date")).toString(), Qt::ISODate);
    r.operation = o.value(QLatin1String("operation")).toString();
    r.target = o.value(QLatin1String("target")).toString();
    r.serial = o.value(QLatin1String("serial")).toString();
    r.diskBytes = qint64(o.value(QLatin1String("diskBytes")).toDouble());
    r.osEntryBefore =
        QByteArray::fromHex(o.value(QLatin1String("osEntryBefore")).toString().toLatin1());
    r.bootloaderVersion = o.value(QLatin1String("bootloaderVersion")).toString();
    r.bootloaderChecksum = quint32(o.value(QLatin1String("bootloaderChecksum")).toDouble());
    r.delta = qint64(o.value(QLatin1String("delta")).toDouble());
    r.backupFile = o.value(QLatin1String("backupFile")).toString();
    if (!r.date.isValid() || r.operation.isEmpty() || r.target.isEmpty())
        return std::nullopt;
    return r;
}

std::optional<InstallRecord> findInstallRecord(const QString &dir, const QString &target,
                                               const QString &serial, qint64 diskBytes)
{
    std::optional<InstallRecord> best;
    const QStringList names =
        QDir(dir).entryList({QStringLiteral("record-install-*.json")}, QDir::Files);
    for (const QString &name : names) {
        const std::optional<InstallRecord> r = loadRecord(QDir(dir).filePath(name));
        if (!r || r->operation != QLatin1String("install") || r->target != target ||
            r->serial != serial || r->diskBytes != diskBytes)
            continue;
        if (!best || r->date > best->date)
            best = r;
    }
    return best;
}

} // namespace tacetos::installer
