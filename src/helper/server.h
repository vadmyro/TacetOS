// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "protocol.h"

#include <QByteArray>
#include <QString>
#include <functional>

// The loop of the helper (docs/specs/helper.md sections 3 and 7): the hello, then
// one request at a time, read the strict way, checked in the order of section 7.1,
// run under the lock of section 8, answered by events.

namespace tacetos::helper {

struct ServerIo
{
    // The next byte of the input, or -1 at its end.
    std::function<int()> readByte;
    // Writes and flushes one line. False when the output cannot be written to any more.
    std::function<bool(const QByteArray &)> write;
};

class Server
{
public:
    // `lockFolder`: where the locks of section 8 are made.
    Server(ServerIo io, QString lockFolder);

    // Runs until the input ends. 0 after a normal end, 3 when the output is closed.
    int run();

private:
    bool send(const QJsonObject &event);
    bool handleLine(const LineReader::Item &item);
    bool handleReadState(const Request &request);

    ServerIo m_io;
    QString m_lockFolder;
    qint64 m_lastId = 0;
};

} // namespace tacetos::helper
