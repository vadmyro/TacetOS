// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "index.h"

#include "constants.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace tacetos::update {

namespace {

constexpr int kFormat = 1;

// The largest whole number a JSON number holds exactly.
constexpr double kExactMax = 9007199254740992.0;

bool isWholeNumber(const QJsonValue &value, double low)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    return std::floor(number) == number && number >= low && number <= kExactMax;
}

bool isText(const QJsonValue &value)
{
    return value.isString() && !value.toString().isEmpty();
}

// A file name of the channel: no folder, nothing that goes up.
bool isPlainName(const QString &name)
{
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..") &&
           !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'));
}

bool isSha256(const QString &text)
{
    static const QRegularExpression hex(
        QRegularExpression::anchoredPattern(QStringLiteral("[0-9a-f]{64}")));
    return hex.match(text).hasMatch();
}

IndexReading refused(const QString &error)
{
    IndexReading reading;
    reading.error = error;
    return reading;
}

} // namespace

const IndexPackage *Index::package(const QString &target) const
{
    for (const IndexPackage &entry : packages) {
        if (entry.target == target)
            return &entry;
    }
    return nullptr;
}

IndexReading parseIndex(const QByteArray &data)
{
    if (data.size() > device::kIndexMaxBytes)
        return refused(QStringLiteral("it is larger than %1 bytes").arg(device::kIndexMaxBytes));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return refused(QStringLiteral("it is not JSON (%1)").arg(parseError.errorString()));
    if (!document.isObject())
        return refused(QStringLiteral("it is not a JSON object"));
    const QJsonObject root = document.object();

    const QJsonValue format = root.value(QLatin1String("format"));
    if (!format.isDouble())
        return refused(QStringLiteral("\"format\" is missing or not a number"));
    if (format.toDouble() != kFormat)
        return refused(
            QStringLiteral("its format is %1, not %2").arg(format.toDouble()).arg(kFormat));

    if (!isText(root.value(QLatin1String("version"))))
        return refused(QStringLiteral("\"version\" is missing or not a text"));
    if (!isText(root.value(QLatin1String("built"))))
        return refused(QStringLiteral("\"built\" is missing or not a text"));
    const QJsonValue packages = root.value(QLatin1String("packages"));
    if (!packages.isArray())
        return refused(QStringLiteral("\"packages\" is missing or not a list"));

    IndexReading reading;
    reading.index.version = root.value(QLatin1String("version")).toString();
    reading.index.built = root.value(QLatin1String("built")).toString();

    QSet<QString> targets;
    const QJsonArray list = packages.toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (!list.at(i).isObject())
            return refused(QStringLiteral("package %1 is not an object").arg(i + 1));
        const QJsonObject object = list.at(i).toObject();
        if (!isText(object.value(QLatin1String("target"))))
            return refused(
                QStringLiteral("package %1: \"target\" is missing or not a text").arg(i + 1));
        IndexPackage entry;
        entry.target = object.value(QLatin1String("target")).toString();
        const QString where = QStringLiteral("package %1 (%2)").arg(i + 1).arg(entry.target);
        if (targets.contains(entry.target))
            return refused(QStringLiteral("%1 is there twice").arg(entry.target));
        targets.insert(entry.target);

        const QJsonValue manifest = object.value(QLatin1String("manifest"));
        if (!manifest.isString() || !isPlainName(manifest.toString()))
            return refused(
                QStringLiteral("%1: \"manifest\" is missing or not a file name").arg(where));
        const QJsonValue size = object.value(QLatin1String("size"));
        if (!isWholeNumber(size, 1))
            return refused(QStringLiteral("%1: \"size\" is missing or not a size").arg(where));
        const QJsonValue sha256 = object.value(QLatin1String("sha256"));
        if (!sha256.isString() || !isSha256(sha256.toString()))
            return refused(QStringLiteral("%1: \"sha256\" is missing or not a sha256").arg(where));

        entry.manifest = manifest.toString();
        entry.size = static_cast<qint64>(size.toDouble());
        entry.sha256 = sha256.toString();
        reading.index.packages.append(entry);
    }

    reading.ok = true;
    return reading;
}

} // namespace tacetos::update
