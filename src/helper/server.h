// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "protocol.h"
#include "system.h"

#include <QByteArray>
#include <QString>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

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
    // `lockFolder`: where the locks of section 8 are made. `system`: what the
    // commands on a real disk and `install-udev-rule` ask of the computer; null
    // means there is none (real disks are then refused). Not owned.
    Server(ServerIo io, QString lockFolder, SystemLayer *system = nullptr);

    // Runs until the input ends. 0 after a normal end, 3 when the output is closed.
    int run();

private:
    // The lines of the input, read by a thread of their own so that a `cancel` is
    // heard while a command runs (section 3.6).
    struct Pump
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<LineReader::Item> items;
    };
    class Running;

    bool send(const QJsonObject &event);
    void startPump();
    // Blocks for the next item of the input.
    LineReader::Item nextItem();
    // The next item when one is there already.
    bool tryItem(LineReader::Item &item);
    bool handleLine(const LineReader::Item &item);
    bool handleDiskCommand(const Request &request);
    bool handleCancel(const Request &request);
    // While a command runs (sections 3.6 and 6.12): `cancel` is answered, any
    // other request is refused with `busy`, the end of the input is a cancel.
    void pollInput();

    ServerIo m_io;
    QString m_lockFolder;
    SystemLayer *m_system;
    qint64 m_lastId = 0;
    std::shared_ptr<Pump> m_pump;
    bool m_outputOk = true;
    bool m_inputEnded = false;
    // The request that runs, 0 when none; whether it can still be cancelled and
    // whether a cancel was accepted.
    qint64 m_runningId = 0;
    bool m_cancellable = true;
    bool m_cancelAsked = false;
};

} // namespace tacetos::helper
