// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>

// The messages of docs/specs/helper.md sections 3 and 5.1: one JSON object per
// line, read the strict way. No access to a disk here.

namespace tacetos::helper {

// Section 3.1 rule 1. `readByte` gives the next byte, or -1 at the end of the input.
class LineReader
{
public:
    enum class Kind { Line, TooLong, End };
    struct Item
    {
        Kind kind = Kind::End;
        QByteArray line; // without the line feed, for `Line`
    };

    explicit LineReader(std::function<int()> readByte) : m_readByte(std::move(readByte)) {}

    // A line longer than `kMaxRequestLine` is thrown away up to its line feed and
    // reported as `TooLong`. An unfinished last line is still a line.
    Item next();

private:
    std::function<int()> m_readByte;
};

struct Request
{
    qint64 id = 0;
    QString cmd;
    QJsonObject args;
};

struct Refusal
{
    QString code;
    QString detail;
    // The id of the request when it could be read, 0 otherwise.
    qint64 id = 0;
};

struct ParseResult
{
    bool ok = false;
    Request request;
    Refusal refusal;
};

// Section 3.5. `lastId` is the greatest id seen so far; the caller raises it to
// `request.id` (or to `refusal.id` when it is not 0) after each call.
ParseResult parseRequest(const QByteArray &line, qint64 lastId);

// Whether a JSON value is an integer in the range of section 3.1 rule 3.
bool isInteger(const QJsonValue &value, qint64 *out = nullptr);

// One line of output: compact JSON and a line feed.
QByteArray eventLine(const QJsonObject &event);

QJsonObject makeHello(bool classic, bool usb);
QJsonObject makeAccepted(qint64 id);
QJsonObject makeProgress(qint64 id, const QString &stage, qint64 done, qint64 total,
                         bool cancellable);
QJsonObject makeLog(qint64 id, const QString &level, const QString &text);
QJsonObject makeResultOk(qint64 id, const QJsonObject &data);
QJsonObject makeResultError(qint64 id, const QString &code, const QString &detail,
                            const QJsonObject &data = {});

// The version of the helper, the same as the app it ships with.
QString helperVersion();
// `linux`, `windows` or `macos`.
QString systemName();

} // namespace tacetos::helper
