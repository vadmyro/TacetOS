// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "history.h"

#include "blocks.h"
#include "constants.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>

namespace tacetos::health {

namespace {

constexpr int kHistoryFormat = 1;

// Section 5.3: the ids the history keeps.
constexpr int kKeptIds[] = {kReallocatedId, kPendingId, kUncorrectableId, kSpinRetriesId};

QString sha256(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString eventName(Event event)
{
    return event == Event::Shutdown ? QStringLiteral("shutdown") : QStringLiteral("usb");
}

QString chargingName(Charging charging)
{
    switch (charging) {
    case Charging::Yes:
        return QStringLiteral("yes");
    case Charging::Error:
        return QStringLiteral("error");
    case Charging::No:
        break;
    }
    return QStringLiteral("no");
}

QJsonObject readingToJson(const HistoryReading &r)
{
    QJsonObject o;
    o.insert(QStringLiteral("read_at"), r.readAt.toUTC().toString(Qt::ISODate));
    o.insert(QStringLiteral("event"), eventName(r.event));
    if (!r.clock.isEmpty())
        o.insert(QStringLiteral("clock"), r.clock);
    if (r.batteryLevel)
        o.insert(QStringLiteral("battery_level"), *r.batteryLevel);
    o.insert(QStringLiteral("battery_mv"), r.batteryMv);
    o.insert(QStringLiteral("runtime_s"), double(r.runtimeS));
    o.insert(QStringLiteral("top_runtime_s"), double(r.topRuntimeS));
    o.insert(QStringLiteral("charging"), chargingName(r.charging));
    if (r.smart) {
        QJsonObject smart;
        for (auto it = r.smart->constBegin(); it != r.smart->constEnd(); ++it)
            smart.insert(QString::number(it.key()), double(it.value()));
        o.insert(QStringLiteral("smart"), smart);
    }
    if (r.estimateH)
        o.insert(QStringLiteral("estimate_h"), *r.estimateH);
    o.insert(QStringLiteral("health_sha256"), r.healthSha256);
    if (!r.chargeSha256.isEmpty())
        o.insert(QStringLiteral("charge_sha256"), r.chargeSha256);
    return o;
}

// A whole number in [0, max], as JSON gives it.
bool wholeNumber(const QJsonValue &v, double max, qint64 *out)
{
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (d < 0 || d > max || std::floor(d) != d)
        return false;
    *out = qint64(d);
    return true;
}

bool isSha256(const QJsonValue &v)
{
    static const QRegularExpression form(QStringLiteral("^[0-9a-f]{64}$"));
    return v.isString() && form.match(v.toString()).hasMatch();
}

// One reading of the file, or none when a key has the wrong form.
std::optional<HistoryReading> readingFromJson(const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject o = value.toObject();
    HistoryReading r;
    qint64 n = 0;

    r.readAt = QDateTime::fromString(o.value(QStringLiteral("read_at")).toString(), Qt::ISODate);
    if (!r.readAt.isValid())
        return std::nullopt;

    const QString event = o.value(QStringLiteral("event")).toString();
    if (event == QLatin1String("usb"))
        r.event = Event::Usb;
    else if (event == QLatin1String("shutdown"))
        r.event = Event::Shutdown;
    else
        return std::nullopt;

    const QJsonValue clock = o.value(QStringLiteral("clock"));
    if (!clock.isUndefined()) {
        if (!clock.isString())
            return std::nullopt;
        r.clock = clock.toString();
    }

    const QJsonValue level = o.value(QStringLiteral("battery_level"));
    if (!level.isUndefined()) {
        if (!wholeNumber(level, 100, &n))
            return std::nullopt;
        r.batteryLevel = int(n);
    }
    if (!wholeNumber(o.value(QStringLiteral("battery_mv")), 2147483647, &n))
        return std::nullopt;
    r.batteryMv = int(n);
    if (!wholeNumber(o.value(QStringLiteral("runtime_s")), 1e15, &r.runtimeS) ||
        !wholeNumber(o.value(QStringLiteral("top_runtime_s")), 1e15, &r.topRuntimeS))
        return std::nullopt;

    const QString charging = o.value(QStringLiteral("charging")).toString();
    if (charging == QLatin1String("no"))
        r.charging = Charging::No;
    else if (charging == QLatin1String("yes"))
        r.charging = Charging::Yes;
    else if (charging == QLatin1String("error"))
        r.charging = Charging::Error;
    else
        return std::nullopt;

    const QJsonValue smart = o.value(QStringLiteral("smart"));
    if (!smart.isUndefined()) {
        if (!smart.isObject())
            return std::nullopt;
        QMap<int, quint32> values;
        const QJsonObject s = smart.toObject();
        for (auto it = s.constBegin(); it != s.constEnd(); ++it) {
            bool idOk = false;
            const int id = it.key().toInt(&idOk);
            if (!idOk || id < 1 || id > 255 || !wholeNumber(it.value(), 4294967295.0, &n))
                return std::nullopt;
            values.insert(id, quint32(n));
        }
        r.smart = values;
    }

    const QJsonValue estimate = o.value(QStringLiteral("estimate_h"));
    if (!estimate.isUndefined()) {
        if (!estimate.isDouble() || estimate.toDouble() <= 0)
            return std::nullopt;
        r.estimateH = estimate.toDouble();
    }

    if (!isSha256(o.value(QStringLiteral("health_sha256"))))
        return std::nullopt;
    r.healthSha256 = o.value(QStringLiteral("health_sha256")).toString();
    const QJsonValue charge = o.value(QStringLiteral("charge_sha256"));
    if (!charge.isUndefined()) {
        if (!isSha256(charge))
            return std::nullopt;
        r.chargeSha256 = charge.toString();
    }
    return r;
}

QString fileName(const QString &key)
{
    return QStringLiteral("%1.json").arg(key);
}

} // namespace

QList<double> History::estimates() const
{
    QList<double> list;
    for (const HistoryReading &r : readings) {
        if (r.estimateH)
            list.append(*r.estimateH);
    }
    return list;
}

QString historyKey(const HealthReading &reading, const QString &usbSerial)
{
    QByteArray text;
    if (reading.ok && reading.disk.identify.ok && !reading.disk.identify.serial.isEmpty())
        text = "disk:" + reading.disk.identify.serial.toUtf8();
    else if (!usbSerial.isEmpty())
        text = "usb:" + usbSerial.toUtf8();
    else
        return QString();
    return sha256(text).left(16);
}

bool addReading(History *history, const HealthReading &reading)
{
    if (!reading.ok)
        return false;
    const QString healthSha = sha256(reading.healthBytes);
    if (!history->readings.isEmpty() && history->readings.last().healthSha256 == healthSha)
        return false;

    HistoryReading r;
    r.readAt = reading.readAt;
    r.event = reading.health.event;
    r.clock = reading.health.clock;
    r.batteryLevel = reading.health.batteryLevel;
    r.batteryMv = reading.health.batteryMv;
    r.runtimeS = reading.health.runtimeS;
    r.topRuntimeS = reading.health.topRuntimeS;
    r.charging = reading.health.charging;
    if (reading.disk.smart.ok) {
        QMap<int, quint32> values;
        for (int id : kKeptIds) {
            if (const std::optional<quint32> v = reading.disk.value(id))
                values.insert(id, *v);
        }
        r.smart = values;
    }
    r.healthSha256 = healthSha;
    if (reading.chargeOk)
        r.chargeSha256 = sha256(reading.chargeBytes);

    r.estimateH = reading.estimateH;
    if (r.estimateH) {
        for (const HistoryReading &earlier : std::as_const(history->readings)) {
            if (earlier.estimateH && earlier.chargeSha256 == r.chargeSha256) {
                r.estimateH.reset();
                break;
            }
        }
    }

    history->readings.append(r);
    while (history->readings.size() > device::kHistoryMax)
        history->readings.removeFirst();
    return true;
}

QByteArray historyToJson(const History &history)
{
    QJsonArray readings;
    for (const HistoryReading &r : history.readings)
        readings.append(readingToJson(r));
    QJsonObject root;
    root.insert(QStringLiteral("format"), kHistoryFormat);
    root.insert(QStringLiteral("readings"), readings);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

HistoryParse historyFromJson(const QByteArray &data)
{
    HistoryParse parse;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        parse.error = QStringLiteral("not a JSON object");
        return parse;
    }
    const QJsonObject root = doc.object();
    const QJsonValue format = root.value(QStringLiteral("format"));
    if (!format.isDouble() || format.toDouble() != kHistoryFormat) {
        parse.error = QStringLiteral("format %1 not supported")
                          .arg(format.isDouble() ? QString::number(format.toDouble())
                                                 : QStringLiteral("missing"));
        return parse;
    }
    const QJsonValue readings = root.value(QStringLiteral("readings"));
    if (!readings.isArray()) {
        parse.error = QStringLiteral("no readings");
        return parse;
    }
    const QJsonArray list = readings.toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        const std::optional<HistoryReading> r = readingFromJson(list.at(i));
        if (!r) {
            parse.error = QStringLiteral("bad reading %1").arg(i + 1);
            return parse;
        }
        parse.history.readings.append(*r);
    }
    parse.ok = true;
    return parse;
}

QString historyPath(const QString &dataFolder, const QString &key)
{
    return QDir(dataFolder)
        .filePath(QString(device::kHealthFolder) + QLatin1Char('/') + fileName(key));
}

std::optional<History> loadHistory(const QString &dataFolder, const QString &key, QStringList *log)
{
    const QString path = historyPath(dataFolder, key);
    const QString name = QString(device::kHealthFolder) + QLatin1Char('/') + fileName(key);
    QFile file(path);
    if (!file.exists())
        return History();
    if (!file.open(QIODevice::ReadOnly)) {
        log->append(QStringLiteral("%1: %2").arg(name, file.errorString()));
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        log->append(QStringLiteral("%1: %2").arg(name, file.errorString()));
        return std::nullopt;
    }
    file.close();

    const HistoryParse parse = historyFromJson(data);
    if (parse.ok)
        return parse.history;

    const QString bad = path + QStringLiteral(".bad");
    QFile::remove(bad);
    if (QFile::rename(path, bad))
        log->append(
            QStringLiteral("%1: %2, set aside as %3.bad").arg(name, parse.error, fileName(key)));
    else
        log->append(QStringLiteral("%1: %2, could not be set aside").arg(name, parse.error));
    return History();
}

bool saveHistory(const QString &dataFolder, const QString &key, const History &history,
                 QStringList *log)
{
    const QString path = historyPath(dataFolder, key);
    const QString name = QString(device::kHealthFolder) + QLatin1Char('/') + fileName(key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        log->append(QStringLiteral("%1: cannot make the folder").arg(name));
        return false;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        log->append(QStringLiteral("%1: %2").arg(name, out.errorString()));
        return false;
    }
    out.write(historyToJson(history));
    if (!out.commit()) {
        log->append(QStringLiteral("%1: %2").arg(name, out.errorString()));
        return false;
    }
    return true;
}

History recordReading(const QString &dataFolder, const QString &key, const HealthReading &reading,
                      QStringList *log)
{
    std::optional<History> history;
    if (!key.isEmpty())
        history = loadHistory(dataFolder, key, log);
    if (!history) {
        History alone;
        addReading(&alone, reading);
        return alone;
    }
    if (addReading(&*history, reading))
        saveHistory(dataFolder, key, *history, log);
    return *history;
}

} // namespace tacetos::health
