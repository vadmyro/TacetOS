// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "protocol.h"

#include "checks.h"
#include "commands.h"
#include "helperconstants.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <cmath>

namespace tacetos::helper {

LineReader::Item LineReader::next()
{
    Item item;
    QByteArray line;
    bool tooLong = false;
    for (;;) {
        const int c = m_readByte();
        if (c < 0) {
            if (!tooLong && line.isEmpty()) {
                item.kind = Kind::End;
                return item;
            }
            break;
        }
        if (c == '\n')
            break;
        if (tooLong)
            continue;
        if (line.size() >= kMaxRequestLine) {
            tooLong = true;
            line.clear();
            continue;
        }
        line.append(char(c));
    }
    item.kind = tooLong ? Kind::TooLong : Kind::Line;
    item.line = line;
    return item;
}

bool isInteger(const QJsonValue &value, qint64 *out)
{
    if (!value.isDouble())
        return false;
    const double d = value.toDouble();
    if (!std::isfinite(d) || d != std::floor(d) || std::fabs(d) >= double(kMaxJsonInteger))
        return false;
    if (out)
        *out = qint64(d);
    return true;
}

static ParseResult refuse(const QString &code, const QString &detail, qint64 id = 0)
{
    ParseResult r;
    r.refusal.code = code;
    r.refusal.detail = detail;
    r.refusal.id = id;
    return r;
}

ParseResult parseRequest(const QByteArray &line, qint64 lastId)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return refuse(err::badRequest, QStringLiteral("the line is not a JSON object"));
    const QJsonObject obj = doc.object();

    // The id first, so that a refusal can carry it.
    qint64 id = 0;
    const bool idOk = obj.contains(QStringLiteral("id")) &&
                      isInteger(obj.value(QStringLiteral("id")), &id) && id > 0;
    if (!idOk)
        id = 0;

    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString key = it.key();
        if (key != QLatin1String("id") && key != QLatin1String("cmd") &&
            key != QLatin1String("args"))
            return refuse(err::badRequest, QStringLiteral("unknown field \"%1\"").arg(key));
    }
    if (!obj.contains(QStringLiteral("id")) || !obj.contains(QStringLiteral("cmd")) ||
        !obj.contains(QStringLiteral("args")))
        return refuse(err::badRequest, QStringLiteral("a field of id, cmd, args is missing"));
    if (!idOk)
        return refuse(err::badRequest, QStringLiteral("id is not a positive integer"));
    if (id <= lastId)
        return refuse(
            err::badRequest,
            QStringLiteral("id %1 is not greater than the last one (%2)").arg(id).arg(lastId), id);
    if (!obj.value(QStringLiteral("cmd")).isString())
        return refuse(err::badRequest, QStringLiteral("cmd is not a string"), id);
    if (!obj.value(QStringLiteral("args")).isObject())
        return refuse(err::badRequest, QStringLiteral("args is not an object"), id);

    const QString name = obj.value(QStringLiteral("cmd")).toString();
    const CommandSpec *spec = findCommand(name);
    if (!spec)
        return refuse(err::unknownCommand, QStringLiteral("no such command: %1").arg(name), id);
    const QString why = checkArguments(*spec, obj.value(QStringLiteral("args")).toObject());
    if (!why.isEmpty())
        return refuse(err::badRequest, why, id);

    ParseResult r;
    r.ok = true;
    r.request.id = id;
    r.request.cmd = name;
    r.request.args = obj.value(QStringLiteral("args")).toObject();
    return r;
}

QByteArray eventLine(const QJsonObject &event)
{
    return QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
}

QString helperVersion()
{
    return QStringLiteral(TACETOS_VERSION);
}

QString systemName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QJsonObject makeHello(bool classic, bool usb)
{
    QJsonObject o;
    o.insert(QStringLiteral("event"), QStringLiteral("hello"));
    o.insert(QStringLiteral("protocol"), kProtocol);
    o.insert(QStringLiteral("version"), helperVersion());
    o.insert(QStringLiteral("system"), systemName());
    o.insert(QStringLiteral("realWrites"), installer::realWritesAllowed());
    o.insert(QStringLiteral("classic"), classic);
    o.insert(QStringLiteral("usb"), usb);
    return o;
}

static QJsonObject baseEvent(const QString &event, qint64 id)
{
    QJsonObject o;
    o.insert(QStringLiteral("event"), event);
    o.insert(QStringLiteral("id"), double(id));
    return o;
}

QJsonObject makeAccepted(qint64 id)
{
    return baseEvent(QStringLiteral("accepted"), id);
}

QJsonObject makeProgress(qint64 id, const QString &stage, qint64 done, qint64 total,
                         bool cancellable)
{
    QJsonObject o = baseEvent(QStringLiteral("progress"), id);
    o.insert(QStringLiteral("stage"), stage);
    o.insert(QStringLiteral("done"), double(done));
    o.insert(QStringLiteral("total"), double(total));
    o.insert(QStringLiteral("cancellable"), cancellable);
    return o;
}

QJsonObject makeLog(qint64 id, const QString &level, const QString &text)
{
    QJsonObject o = baseEvent(QStringLiteral("log"), id);
    o.insert(QStringLiteral("level"), level);
    o.insert(QStringLiteral("text"), text);
    return o;
}

QJsonObject makeResultOk(qint64 id, const QJsonObject &data)
{
    QJsonObject o = baseEvent(QStringLiteral("result"), id);
    o.insert(QStringLiteral("ok"), true);
    o.insert(QStringLiteral("data"), data);
    return o;
}

QJsonObject makeResultError(qint64 id, const QString &code, const QString &detail,
                            const QJsonObject &data)
{
    QJsonObject o = baseEvent(QStringLiteral("result"), id);
    o.insert(QStringLiteral("ok"), false);
    o.insert(QStringLiteral("error"), code);
    o.insert(QStringLiteral("detail"), detail);
    if (!data.isEmpty())
        o.insert(QStringLiteral("data"), data);
    return o;
}

} // namespace tacetos::helper
