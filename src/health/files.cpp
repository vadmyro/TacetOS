// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "files.h"

#include "constants.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QTime>

namespace tacetos::health {

namespace {

// The lines of a file once its frame is checked: first line, last line and
// the form of every line.
struct Lines
{
    QHash<QString, QString> values;
    QString error;
};

Lines splitLines(const QByteArray &data, const QString &name)
{
    Lines lines;
    if (data.size() > device::kMarkerMaxBytes) {
        lines.error =
            QStringLiteral("%1: larger than %2 bytes").arg(name).arg(device::kMarkerMaxBytes);
        return lines;
    }
    QList<QByteArray> raw = data.split('\n');
    // The LF that ends the last line.
    if (!raw.isEmpty() && raw.constLast().isEmpty())
        raw.removeLast();
    if (raw.isEmpty()) {
        lines.error = QStringLiteral("%1: empty").arg(name);
        return lines;
    }

    static const QRegularExpression form(QStringLiteral("^([a-z0-9_]+): (.*)$"));
    QList<QPair<QString, QString>> pairs;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        QByteArray line = raw.at(i);
        if (line.endsWith('\r'))
            line.chop(1);
        const QRegularExpressionMatch match = form.match(QString::fromLatin1(line));
        if (!match.hasMatch()) {
            lines.error =
                QStringLiteral("%1: line %2 is not a key: value line").arg(name).arg(i + 1);
            return lines;
        }
        pairs.append({match.captured(1), match.captured(2)});
    }

    const auto &[firstKey, firstValue] = pairs.constFirst();
    if (firstKey != QLatin1String("format")) {
        lines.error = QStringLiteral("%1: no format line").arg(name);
        return lines;
    }
    static const QRegularExpression number(QStringLiteral("^[0-9]+$"));
    if (!number.match(firstValue).hasMatch()) {
        lines.error = QStringLiteral("%1: bad format '%2'").arg(name, firstValue);
        return lines;
    }
    if (firstValue != QLatin1String("1")) {
        lines.error = QStringLiteral("%1: format %2 not supported").arg(name, firstValue);
        return lines;
    }
    const auto &[lastKey, lastValue] = pairs.constLast();
    if (lastKey != QLatin1String("end") || lastValue != QLatin1String("ok")) {
        lines.error = QStringLiteral("%1: no end line").arg(name);
        return lines;
    }

    for (const auto &[key, value] : pairs)
        lines.values.insert(key, value);
    return lines;
}

// Reads the "Always" keys one after the other and keeps the first problem.
class Fields
{
public:
    Fields(const QHash<QString, QString> &values, const QString &name)
        : m_values(values), m_name(name)
    {
    }

    const QString &error() const { return m_error; }

    // The value of `key`, or null after setting the error.
    const QString *value(const QString &key)
    {
        if (!m_error.isEmpty())
            return nullptr;
        const auto it = m_values.constFind(key);
        if (it == m_values.constEnd()) {
            m_error = QStringLiteral("%1: no %2 line").arg(m_name, key);
            return nullptr;
        }
        return &it.value();
    }

    void refuse(const QString &key, const QString &value)
    {
        m_error = QStringLiteral("%1: bad %2 '%3'").arg(m_name, key, value);
    }

    // One of `choices`, as its index.
    int choice(const QString &key, std::initializer_list<const char *> choices)
    {
        const QString *v = value(key);
        if (!v)
            return -1;
        int i = 0;
        for (const char *c : choices) {
            if (*v == QLatin1String(c))
                return i;
            ++i;
        }
        refuse(key, *v);
        return -1;
    }

    // Digits only, at most `max`.
    qint64 count(const QString &key, qint64 max)
    {
        const QString *v = value(key);
        if (!v)
            return 0;
        static const QRegularExpression digits(QStringLiteral("^[0-9]{1,18}$"));
        const qint64 n = digits.match(*v).hasMatch() ? v->toLongLong() : -1;
        if (n < 0 || n > max) {
            refuse(key, *v);
            return 0;
        }
        return n;
    }

    // `YYYY-MM-DD HH:MM:SS`, or `unset` (an empty text).
    QString clock()
    {
        const QString key = QStringLiteral("clock");
        const QString *v = value(key);
        if (!v || *v == QLatin1String("unset"))
            return QString();
        static const QRegularExpression form(
            QStringLiteral("^([0-9]{4})-([0-9]{2})-([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2})$"));
        const QRegularExpressionMatch m = form.match(*v);
        const bool good =
            m.hasMatch() &&
            QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()).isValid() &&
            QTime(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt()).isValid();
        if (!good) {
            refuse(key, *v);
            return QString();
        }
        return *v;
    }

    // `0` to `100`, or `unknown` (none).
    std::optional<int> level()
    {
        const QString key = QStringLiteral("battery_level");
        const QString *v = value(key);
        if (!v || *v == QLatin1String("unknown"))
            return std::nullopt;
        return int(count(key, 100));
    }

    // A key that is not "Always": its value, or an empty text.
    QString optional(const QString &key) const { return m_values.value(key); }

private:
    const QHash<QString, QString> &m_values;
    QString m_name;
    QString m_error;
};

// Millivolts and seconds are kept in these.
constexpr qint64 kIntMax = 2147483647;
constexpr qint64 kCountMax = 999999999999999999;

} // namespace

HealthFileReading parseHealthFile(const QByteArray &data)
{
    HealthFileReading reading;
    const QString name(device::kHealthFile);
    const Lines lines = splitLines(data, name);
    if (!lines.error.isEmpty()) {
        reading.error = lines.error;
        return reading;
    }

    Fields fields(lines.values, name);
    HealthFile &file = reading.file;
    file.event = Event(fields.choice(QStringLiteral("event"), {"usb", "shutdown"}));
    file.clock = fields.clock();
    file.batteryLevel = fields.level();
    file.batteryMv = int(fields.count(QStringLiteral("battery_mv"), kIntMax));
    file.charging = Charging(fields.choice(QStringLiteral("charging"), {"no", "yes", "error"}));
    file.runtimeS = fields.count(QStringLiteral("runtime_s"), kCountMax);
    file.topRuntimeS = fields.count(QStringLiteral("top_runtime_s"), kCountMax);
    file.diskKind = DiskKind(fields.choice(QStringLiteral("disk_kind"), {"hdd", "flash"}));

    const QString smartKey = QStringLiteral("smart");
    if (const QString *smart = fields.value(smartKey)) {
        static const QRegularExpression error(QStringLiteral("^error (-?[0-9]{1,9})$"));
        const QRegularExpressionMatch m = error.match(*smart);
        if (*smart == QLatin1String("ok")) {
            file.smart = SmartState::Ok;
        } else if (*smart == QLatin1String("unsupported")) {
            file.smart = SmartState::Unsupported;
        } else if (m.hasMatch()) {
            file.smart = SmartState::Error;
            file.smartError = m.captured(1).toInt();
        } else {
            fields.refuse(smartKey, *smart);
        }
    }

    if (!fields.error().isEmpty()) {
        reading.error = fields.error();
        reading.file = HealthFile();
        return reading;
    }
    file.identifyData = fields.optional(QStringLiteral("identify_data"));
    // Section 2.1: only with `smart: ok`.
    if (file.smart == SmartState::Ok)
        file.smartData = fields.optional(QStringLiteral("smart_data"));
    reading.ok = true;
    return reading;
}

ChargeFileReading parseChargeFile(const QByteArray &data)
{
    ChargeFileReading reading;
    const QString name(device::kChargeFile);
    const Lines lines = splitLines(data, name);
    if (!lines.error.isEmpty()) {
        reading.error = lines.error;
        return reading;
    }

    // Section 2.2: every key of the table is always there.
    Fields fields(lines.values, name);
    reading.file.clock = fields.clock();
    reading.file.batteryLevel = fields.level();
    reading.file.batteryMv = int(fields.count(QStringLiteral("battery_mv"), kIntMax));
    if (!fields.error().isEmpty()) {
        reading.error = fields.error();
        reading.file = ChargeFile();
        return reading;
    }
    reading.ok = true;
    return reading;
}

} // namespace tacetos::health
